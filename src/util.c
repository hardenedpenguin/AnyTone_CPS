#include "util.h"

#include <string.h>

uint32_t at_get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t at_get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t at_get_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void at_put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

void at_put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

void at_put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

uint32_t at_bcd8_be_get(const uint8_t *p)
{
    uint32_t val = at_get_u32_be(p);
    return (val & 0xf) + ((val >> 4) & 0xf) * 10u + ((val >> 8) & 0xf) * 100u +
           ((val >> 12) & 0xf) * 1000u + ((val >> 16) & 0xf) * 10000u +
           ((val >> 20) & 0xf) * 100000u + ((val >> 24) & 0xf) * 1000000u +
           ((val >> 28) & 0xf) * 10000000u;
}

void at_bcd8_be_set(uint8_t *p, uint32_t val)
{
    uint32_t bcd = 0;
    for (int i = 0; i < 8; i++) {
        bcd |= (val % 10) << (i * 4);
        val /= 10;
    }
    at_put_u32_be(p, bcd);
}

uint8_t at_get_bits(uint8_t byte, unsigned start, unsigned width)
{
    return (uint8_t)((byte >> start) & ((1u << width) - 1u));
}

void at_set_bits(uint8_t *byte, unsigned start, unsigned width, uint8_t val)
{
    uint8_t mask = (uint8_t)(((1u << width) - 1u) << start);
    *byte = (uint8_t)((*byte & ~mask) | ((val << start) & mask));
}

int at_bitmap_get(const uint8_t *map, unsigned idx)
{
    return (map[idx / 8] & (1u << (idx % 8))) ? 1 : 0;
}

void at_bitmap_set(uint8_t *map, unsigned idx, int on)
{
    if (on)
        map[idx / 8] |= (uint8_t)(1u << (idx % 8));
    else
        map[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

int at_inv_bitmap_get(const uint8_t *map, unsigned idx)
{
    return (map[idx / 8] & (1u << (idx % 8))) ? 0 : 1;
}

void at_inv_bitmap_set(uint8_t *map, unsigned idx, int on)
{
    if (on)
        map[idx / 8] &= (uint8_t)~(1u << (idx % 8));
    else
        map[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

void at_read_ascii(const uint8_t *src, size_t n, char *out, size_t out_sz)
{
    size_t i, lim = (n < out_sz - 1) ? n : out_sz - 1;
    for (i = 0; i < lim; i++) {
        uint8_t c = src[i];
        /* 0x00 / 0xff pad unused slots; keep printable ASCII only */
        if (c == 0 || c == 0xff || c < 0x20 || c >= 0x7f)
            break;
        out[i] = (char)c;
    }
    out[i] = '\0';
}

void at_write_ascii(uint8_t *dst, size_t n, const char *s)
{
    memset(dst, 0, n);
    if (!s)
        return;
    for (size_t i = 0; i < n && s[i]; i++)
        dst[i] = (uint8_t)s[i];
}
