#include "protocol.h"
#include "serial.h"

#include <stdio.h>
#include <string.h>

uint8_t at_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += data[i];
    return (uint8_t)(sum & 0xff);
}

const char *at_model_friendly(const char *model)
{
    if (!model)
        return "Unknown";
    if (strcmp(model, "D878UV") == 0)
        return "Anytone AT-D878UV";
    if (strcmp(model, "D878UV2") == 0 || strcmp(model, "D878UVII") == 0)
        return "Anytone AT-D878UV II";
    if (strcmp(model, "D578UV") == 0 || strcmp(model, "D578UV3") == 0 ||
        strcmp(model, "D578UVIII") == 0)
        return "Anytone AT-D578UV / III";
    if (strcmp(model, "D578UV2") == 0 || strcmp(model, "D578UVII") == 0)
        return "Anytone AT-D578UV II";
    if (strcmp(model, "D868UVE") == 0)
        return "Anytone AT-D868UVE";
    if (strcmp(model, "D168UV") == 0)
        return "Anytone AT-D168UV";
    return model;
}

int at_enter_program(int fd)
{
    const char cmd[] = "PROGRAM";
    uint8_t resp[8];

    at_serial_flush(fd);
    if (at_serial_write_all(fd, cmd, sizeof(cmd) - 1) != 0)
        return -1;
    if (at_serial_read_exact(fd, resp, 3, AT_SERIAL_DEFAULT_TIMEOUT_MS) != 0)
        return -1;
    /* Expect "QX\x06" */
    if (resp[0] != 'Q' || resp[1] != 'X' || resp[2] != 0x06) {
        fprintf(stderr, "unexpected PROGRAM response: %02x %02x %02x\n",
                resp[0], resp[1], resp[2]);
        return -1;
    }
    return 0;
}

int at_leave_program(int fd)
{
    const char cmd[] = "END";
    uint8_t ack;

    if (at_serial_write_all(fd, cmd, sizeof(cmd) - 1) != 0)
        return -1;
    if (at_serial_read_exact(fd, &ack, 1, AT_SERIAL_DEFAULT_TIMEOUT_MS) != 0)
        return -1;
    if (ack != 0x06) {
        fprintf(stderr, "unexpected END response: %02x\n", ack);
        return -1;
    }
    return 0;
}

int at_identify(int fd, struct at_radio_info *info)
{
    uint8_t cmd = 0x02;
    uint8_t resp[16];

    memset(info, 0, sizeof(*info));
    if (at_serial_write_all(fd, &cmd, 1) != 0)
        return -1;
    if (at_serial_read_exact(fd, resp, sizeof(resp), AT_SERIAL_DEFAULT_TIMEOUT_MS) != 0)
        return -1;

    /* RadioInfoResponse: 'I' + model[7] + bands + version[6] + 0x06 */
    if (resp[0] != 'I' || resp[15] != 0x06) {
        fprintf(stderr, "unexpected identify response (prefix=%02x eot=%02x)\n",
                resp[0], resp[15]);
        return -1;
    }

    memcpy(info->model, &resp[1], 7);
    info->model[7] = '\0';
    /* Trim trailing NULs already handled; strip trailing spaces. */
    for (int i = 6; i >= 0 && (info->model[i] == '\0' || info->model[i] == ' '); i--)
        info->model[i] = '\0';

    info->bands = resp[8];
    memcpy(info->version, &resp[9], 6);
    info->version[6] = '\0';
    for (int i = 5; i >= 0 && (info->version[i] == '\0' || info->version[i] == ' '); i--)
        info->version[i] = '\0';

    return 0;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

int at_mem_read(int fd, uint32_t addr, void *buf, size_t nbytes, unsigned chunk_size)
{
    uint8_t *out = buf;

    if (chunk_size < 1 || chunk_size > AT_READ_CHUNK_MAX)
        chunk_size = AT_READ_CHUNK_MAX;

    size_t left = nbytes;
    while (left > 0) {
        unsigned n = (unsigned)((left < chunk_size) ? left : chunk_size);
        uint8_t req[6];
        req[0] = 'R';
        put_be32(&req[1], addr);
        req[5] = (uint8_t)n;

        if (at_serial_write_all(fd, req, sizeof(req)) != 0)
            return -1;

        /* Response: 'W' + addr(4) + len(1) + data(n) + sum(1) + ack(1) */
        size_t rlen = 1 + 4 + 1 + n + 1 + 1;
        uint8_t resp[1 + 4 + 1 + AT_READ_CHUNK_MAX + 1 + 1];
        if (at_serial_read_exact(fd, resp, rlen, AT_SERIAL_DEFAULT_TIMEOUT_MS) != 0)
            return -1;

        if (resp[0] != 'W' || resp[rlen - 1] != 0x06)
            return -1;

        uint32_t got_addr = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) |
                            ((uint32_t)resp[3] << 8) | (uint32_t)resp[4];
        if (got_addr != addr || resp[5] != (uint8_t)n)
            return -1;

        uint8_t sum = at_checksum(&resp[1], 4 + 1 + n);
        if (sum != resp[6 + n])
            return -1;

        memcpy(out, &resp[6], n);
        out += n;
        addr += n;
        left -= n;
    }
    return 0;
}

int at_mem_write(int fd, uint32_t addr, const void *buf, size_t nbytes)
{
    const uint8_t *in = buf;
    size_t left = nbytes;

    while (left > 0) {
        uint8_t block[AT_WRITE_CHUNK];
        unsigned n = (unsigned)((left < AT_WRITE_CHUNK) ? left : AT_WRITE_CHUNK);

        if (n < AT_WRITE_CHUNK) {
            /* Radio only accepts 16-byte writes; preserve trailing bytes. */
            if (at_mem_read(fd, addr, block, AT_WRITE_CHUNK, AT_WRITE_CHUNK) != 0)
                return -1;
            memcpy(block, in, n);
        } else {
            memcpy(block, in, AT_WRITE_CHUNK);
        }

        uint8_t frame[1 + 4 + 1 + AT_WRITE_CHUNK + 1 + 1];
        frame[0] = 'W';
        put_be32(&frame[1], addr);
        frame[5] = AT_WRITE_CHUNK;
        memcpy(&frame[6], block, AT_WRITE_CHUNK);
        frame[6 + AT_WRITE_CHUNK] = at_checksum(&frame[1], 4 + 1 + AT_WRITE_CHUNK);
        frame[7 + AT_WRITE_CHUNK] = 0x06;

        if (at_serial_write_all(fd, frame, 8 + AT_WRITE_CHUNK) != 0)
            return -1;

        uint8_t ack;
        if (at_serial_read_exact(fd, &ack, 1, AT_SERIAL_DEFAULT_TIMEOUT_MS) != 0)
            return -1;
        if (ack != 0x06)
            return -1;

        in += n;
        addr += AT_WRITE_CHUNK;
        left -= n;
    }
    return 0;
}
