#ifndef ANYTONE_UTIL_H
#define ANYTONE_UTIL_H

#include <stddef.h>
#include <stdint.h>

uint32_t at_get_u32_be(const uint8_t *p);
uint32_t at_get_u32_le(const uint8_t *p);
uint16_t at_get_u16_le(const uint8_t *p);
void at_put_u32_be(uint8_t *p, uint32_t v);
void at_put_u32_le(uint8_t *p, uint32_t v);
void at_put_u16_le(uint8_t *p, uint16_t v);

/* 8-digit BCD big-endian → integer (e.g. frequency/10 Hz or DMR ID). */
uint32_t at_bcd8_be_get(const uint8_t *p);
void at_bcd8_be_set(uint8_t *p, uint32_t val);

uint8_t at_get_bits(uint8_t byte, unsigned start, unsigned width);
void at_set_bits(uint8_t *byte, unsigned start, unsigned width, uint8_t val);

int at_bitmap_get(const uint8_t *map, unsigned idx);          /* 1 = set */
void at_bitmap_set(uint8_t *map, unsigned idx, int on);
int at_inv_bitmap_get(const uint8_t *map, unsigned idx);      /* 1 = encoded */
void at_inv_bitmap_set(uint8_t *map, unsigned idx, int on);

/* Read NUL-padded ASCII into out (NUL-terminated, max out_sz-1). */
void at_read_ascii(const uint8_t *src, size_t n, char *out, size_t out_sz);
void at_write_ascii(uint8_t *dst, size_t n, const char *s);

#endif /* ANYTONE_UTIL_H */
