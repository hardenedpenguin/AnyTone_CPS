#ifndef ANYTONE_PROTOCOL_H
#define ANYTONE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define AT_READ_CHUNK_MAX  255
#define AT_WRITE_CHUNK     16

struct at_radio_info {
    char model[8];      /* e.g. "D878UV", "D878UV2", "D578UV" */
    char version[8];    /* e.g. "V100" */
    uint8_t bands;      /* band-plan code */
};

const char *at_model_friendly(const char *model);

int at_enter_program(int fd);
int at_leave_program(int fd);
int at_identify(int fd, struct at_radio_info *info);

/* Read nbytes from radio memory at addr into buf. Uses up to chunk_size per
 * transfer (1..255). Returns 0 on success. */
int at_mem_read(int fd, uint32_t addr, void *buf, size_t nbytes, unsigned chunk_size);

/* Write nbytes from buf to radio memory at addr. Transfers are 16 bytes
 * (CPS-compatible). Returns 0 on success. */
int at_mem_write(int fd, uint32_t addr, const void *buf, size_t nbytes);

uint8_t at_checksum(const uint8_t *data, size_t len);

#endif /* ANYTONE_PROTOCOL_H */
