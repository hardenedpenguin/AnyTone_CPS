#ifndef ANYTONE_SERIAL_H
#define ANYTONE_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#define AT_SERIAL_DEFAULT_TIMEOUT_MS 2000

int at_serial_open(const char *path);
void at_serial_close(int fd);
int at_serial_write_all(int fd, const void *buf, size_t len);
int at_serial_read_exact(int fd, void *buf, size_t len, int timeout_ms);
int at_serial_read_some(int fd, void *buf, size_t maxlen, int timeout_ms);
void at_serial_flush(int fd);

/* Scan /dev for candidate ACM devices matching known Anytone USB IDs.
 * Returns number of paths written into out[] (max out_cap). Paths are malloc'd. */
int at_serial_detect(char **out, int out_cap);

#endif /* ANYTONE_SERIAL_H */
