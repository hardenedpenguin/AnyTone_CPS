#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "serial.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Known Anytone USB CDC ACM IDs (from qdmr). */
static const struct {
    uint16_t vid;
    uint16_t pid;
} at_usb_ids[] = {
    { 0x28e9, 0x018a }, /* GD32 (878 / many handhelds) */
    { 0x2e3c, 0x5740 }, /* STM32 (some 578 revisions) */
};

static int read_sysfs_hex(const char *path, unsigned *out)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    unsigned v = 0;
    int n = fscanf(f, "%x", &v);
    fclose(f);
    if (n != 1)
        return -1;
    *out = v;
    return 0;
}

static int tty_matches_anytone(const char *ttyname)
{
    char path[256];
    unsigned vid = 0, pid = 0;

    /* /sys/class/tty/ttyACM0/device/../../idVendor */
    snprintf(path, sizeof(path), "/sys/class/tty/%s/device/../idVendor", ttyname);
    if (read_sysfs_hex(path, &vid) != 0) {
        snprintf(path, sizeof(path), "/sys/class/tty/%s/device/../../idVendor", ttyname);
        if (read_sysfs_hex(path, &vid) != 0)
            return 0;
    }

    snprintf(path, sizeof(path), "/sys/class/tty/%s/device/../idProduct", ttyname);
    if (read_sysfs_hex(path, &pid) != 0) {
        snprintf(path, sizeof(path), "/sys/class/tty/%s/device/../../idProduct", ttyname);
        if (read_sysfs_hex(path, &pid) != 0)
            return 0;
    }

    for (size_t i = 0; i < sizeof(at_usb_ids) / sizeof(at_usb_ids[0]); i++) {
        if (vid == at_usb_ids[i].vid && pid == at_usb_ids[i].pid)
            return 1;
    }
    return 0;
}

int at_serial_detect(char **out, int out_cap)
{
    DIR *d = opendir("/sys/class/tty");
    if (!d)
        return -1;

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < out_cap) {
        if (strncmp(ent->d_name, "ttyACM", 6) != 0)
            continue;
        if (!tty_matches_anytone(ent->d_name))
            continue;

        char *path = malloc(16 + strlen(ent->d_name));
        if (!path)
            break;
        sprintf(path, "/dev/%s", ent->d_name);
        out[n++] = path;
    }
    closedir(d);
    return n;
}

int at_serial_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    /* Baud is ignored by CDC ACM; set a high rate for completeness. */
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    /* Switch to blocking for simpler timed I/O via select. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    at_serial_flush(fd);
    return fd;
}

void at_serial_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

void at_serial_flush(int fd)
{
    tcflush(fd, TCIOFLUSH);
}

int at_serial_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        p += n;
        left -= (size_t)n;
    }
    return 0;
}

static int wait_readable(int fd, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    for (;;) {
        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        return r; /* 0 = timeout, 1 = ready */
    }
}

int at_serial_read_exact(int fd, void *buf, size_t len, int timeout_ms)
{
    uint8_t *p = buf;
    size_t got = 0;
    while (got < len) {
        int w = wait_readable(fd, timeout_ms);
        if (w < 0)
            return -1;
        if (w == 0)
            return -1; /* timeout */
        ssize_t n = read(fd, p + got, len - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

int at_serial_read_some(int fd, void *buf, size_t maxlen, int timeout_ms)
{
    int w = wait_readable(fd, timeout_ms);
    if (w < 0)
        return -1;
    if (w == 0)
        return 0;
    ssize_t n = read(fd, buf, maxlen);
    if (n < 0) {
        if (errno == EINTR)
            return 0;
        return -1;
    }
    return (int)n;
}
