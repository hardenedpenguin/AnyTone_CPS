#include "anytone.h"
#include "codeplug.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Implemented in serve.c */
int at_cmd_serve(const char *device, unsigned chunk, const char *webroot, int port);
int at_cmd_ui(const char *device, unsigned chunk, const char *webroot, int port);

static int g_verbose;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "anytone " ANYTONE_VERSION " — Linux programmer for Anytone D878UV / D878UV II / D578UV\n"
            "\n"
            "Usage:\n"
            "  %s detect\n"
            "  %s info [-d DEVICE]\n"
            "  %s peek -a ADDR -n SIZE [-d DEVICE] [-c CHUNK]\n"
            "  %s read -a ADDR -n SIZE -o FILE [-d DEVICE] [-c CHUNK]\n"
            "  %s write -a ADDR -i FILE [-d DEVICE]\n"
            "  %s dump -o FILE.ATCP [-d DEVICE] [-c CHUNK]\n"
            "  %s restore -i FILE.ATCP [-d DEVICE] [--force]\n"
            "  %s serve [-d DEVICE] [-p PORT] [--web DIR]\n"
            "  %s ui [-d DEVICE] [-p PORT] [--web DIR]\n"
            "\n"
            "Options:\n"
            "  -d, --device PATH   Serial device (default: auto-detect)\n"
            "  -a, --addr HEX      Radio memory address (e.g. 0x02500000)\n"
            "  -n, --size N        Byte count (decimal or 0xHEX)\n"
            "  -o, --output FILE   Output path\n"
            "  -i, --input FILE    Input path\n"
            "  -c, --chunk N       Read chunk size 1..255 (default 255)\n"
            "  -p, --port N        HTTP port (serve default 8780; ui default ephemeral)\n"
            "      --web DIR       UI static files directory\n"
            "      --force         Restore even if model string differs\n"
            "  -v, --verbose\n"
            "  -h, --help\n"
            "\n"
            "Desktop CPS:  %s ui\n"
            "Browser CPS:  %s serve   →  http://127.0.0.1:8780/\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0,
            argv0, argv0);
}

static unsigned long parse_ulong(const char *s)
{
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno || !end || *end != '\0') {
        fprintf(stderr, "invalid number: %s\n", s);
        exit(2);
    }
    return v;
}

static char *auto_device(void)
{
    char *devs[8];
    int n = at_serial_detect(devs, 8);
    if (n <= 0) {
        fprintf(stderr, "no Anytone radio detected (looking for ttyACM + known USB IDs)\n"
                        "try: %s info -d /dev/ttyACM0\n", "anytone");
        return NULL;
    }
    if (n > 1 && g_verbose) {
        fprintf(stderr, "multiple devices found; using %s\n", devs[0]);
        for (int i = 1; i < n; i++)
            free(devs[i]);
    } else {
        for (int i = 1; i < n; i++)
            free(devs[i]);
    }
    return devs[0];
}

static int open_radio(const char *device, int *fd_out, char **owned_path)
{
    *owned_path = NULL;
    const char *path = device;
    if (!path) {
        *owned_path = auto_device();
        if (!*owned_path)
            return -1;
        path = *owned_path;
    }
    int fd = at_serial_open(path);
    if (fd < 0) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (g_verbose)
        fprintf(stderr, "opened %s\n", path);
    *fd_out = fd;
    return 0;
}

static void progress_cb(uint64_t done, uint64_t total, void *ud)
{
    (void)ud;
    int pct = total ? (int)((done * 100) / total) : 100;
    fprintf(stderr, "\r%3d%%  %llu / %llu bytes", pct,
            (unsigned long long)done, (unsigned long long)total);
    if (done >= total)
        fputc('\n', stderr);
    fflush(stderr);
}

static int cmd_detect(void)
{
    char *devs[16];
    int n = at_serial_detect(devs, 16);
    if (n < 0) {
        perror("detect");
        return 1;
    }
    if (n == 0) {
        printf("No Anytone devices found.\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        printf("%s\n", devs[i]);
        free(devs[i]);
    }
    return 0;
}

static int with_program_mode(int fd, int (*fn)(int fd, void *ctx), void *ctx)
{
    if (at_enter_program(fd) != 0) {
        fprintf(stderr, "failed to enter PROGRAM mode (is the radio on and cable connected?)\n");
        return -1;
    }
    int rc = fn(fd, ctx);
    if (at_leave_program(fd) != 0) {
        fprintf(stderr, "warning: failed to leave PROGRAM mode cleanly\n");
        if (rc == 0)
            rc = -1;
    }
    return rc;
}

struct info_ctx {
    struct at_radio_info info;
};

static int do_info(int fd, void *ctx)
{
    struct info_ctx *c = ctx;
    return at_identify(fd, &c->info);
}

static int cmd_info(const char *device)
{
    int fd;
    char *owned = NULL;
    if (open_radio(device, &fd, &owned) != 0) {
        free(owned);
        return 1;
    }
    struct info_ctx c;
    int rc = with_program_mode(fd, do_info, &c);
    at_serial_close(fd);
    free(owned);
    if (rc != 0)
        return 1;

    printf("Model:    %s (%s)\n", c.info.model, at_model_friendly(c.info.model));
    printf("Version:  %s\n", c.info.version);
    printf("Bands:    0x%02x\n", c.info.bands);
    return 0;
}

struct rw_ctx {
    uint32_t addr;
    size_t size;
    unsigned chunk;
    const char *path;
    int is_write;
};

static int do_read(int fd, void *ctx)
{
    struct rw_ctx *c = ctx;
    uint8_t *buf = malloc(c->size);
    if (!buf)
        return -1;
    int rc = at_mem_read(fd, c->addr, buf, c->size, c->chunk);
    if (rc == 0) {
        if (c->path) {
            FILE *f = fopen(c->path, "wb");
            if (!f) {
                perror(c->path);
                rc = -1;
            } else {
                if (fwrite(buf, 1, c->size, f) != c->size)
                    rc = -1;
                fclose(f);
            }
        } else {
            for (size_t i = 0; i < c->size; i++) {
                if (i % 16 == 0)
                    printf("%08x: ", (unsigned)(c->addr + (uint32_t)i));
                printf("%02x%s", buf[i], (i % 16 == 15 || i + 1 == c->size) ? "\n" : " ");
            }
        }
    }
    free(buf);
    return rc;
}

static int do_write(int fd, void *ctx)
{
    struct rw_ctx *c = ctx;
    FILE *f = fopen(c->path, "rb");
    if (!f) {
        perror(c->path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    int rc = at_mem_write(fd, c->addr, buf, (size_t)sz);
    free(buf);
    if (rc == 0 && g_verbose)
        fprintf(stderr, "wrote %ld bytes at 0x%08x\n", sz, c->addr);
    return rc;
}

struct dump_ctx {
    const char *path;
    unsigned chunk;
    struct at_radio_info info;
};

static int do_dump(int fd, void *ctx)
{
    struct dump_ctx *c = ctx;
    if (at_identify(fd, &c->info) != 0)
        return -1;

    size_t nreg = 0;
    const struct at_region *regs = at_regions_for_model(c->info.model, &nreg);
    if (!regs || nreg == 0) {
        fprintf(stderr, "no memory map for model %s\n", c->info.model);
        return -1;
    }

    struct at_image img;
    at_image_init(&img);
    memcpy(img.model, c->info.model, sizeof(img.model));
    memcpy(img.fw_version, c->info.version, sizeof(img.fw_version));
    img.bands = c->info.bands;

    fprintf(stderr, "Dumping %s (%s) — %zu regions...\n",
            c->info.model, at_model_friendly(c->info.model), nreg);

    int rc = at_dump_regions(fd, regs, nreg, &img, c->chunk, progress_cb, NULL);
    if (rc == 0)
        rc = at_image_save(&img, c->path);
    at_image_free(&img);
    if (rc == 0)
        fprintf(stderr, "saved %s\n", c->path);
    return rc;
}

struct restore_ctx {
    const char *path;
    int force;
    struct at_image img;
};

static int do_restore(int fd, void *ctx)
{
    struct restore_ctx *c = ctx;
    struct at_radio_info info;

    if (at_identify(fd, &info) != 0)
        return -1;

    if (!c->force && strcmp(info.model, c->img.model) != 0) {
        fprintf(stderr,
                "model mismatch: radio=%s image=%s (use --force to override)\n",
                info.model, c->img.model);
        return -1;
    }

    fprintf(stderr, "Restoring %u segments to %s...\n", c->img.nseg, info.model);
    return at_restore_image(fd, &c->img, progress_cb, NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(cmd, "detect") == 0)
        return cmd_detect();

    static struct option longopts[] = {
        { "device", required_argument, 0, 'd' },
        { "addr", required_argument, 0, 'a' },
        { "size", required_argument, 0, 'n' },
        { "output", required_argument, 0, 'o' },
        { "input", required_argument, 0, 'i' },
        { "chunk", required_argument, 0, 'c' },
        { "port", required_argument, 0, 'p' },
        { "web", required_argument, 0, 'w' },
        { "force", no_argument, 0, 'f' },
        { "verbose", no_argument, 0, 'v' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 },
    };

    const char *device = NULL;
    const char *out_path = NULL;
    const char *in_path = NULL;
    const char *web_dir = NULL;
    int have_addr = 0, have_size = 0, force = 0;
    uint32_t addr = 0;
    size_t size = 0;
    unsigned chunk = AT_READ_CHUNK_MAX;
    int port = -1; /* command-specific default */

    optind = 2;
    int opt;
    while ((opt = getopt_long(argc, argv, "d:a:n:o:i:c:p:w:vh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'a':
            addr = (uint32_t)parse_ulong(optarg);
            have_addr = 1;
            break;
        case 'n':
            size = (size_t)parse_ulong(optarg);
            have_size = 1;
            break;
        case 'o':
            out_path = optarg;
            break;
        case 'i':
            in_path = optarg;
            break;
        case 'c':
            chunk = (unsigned)parse_ulong(optarg);
            if (chunk < 1 || chunk > AT_READ_CHUNK_MAX) {
                fprintf(stderr, "chunk must be 1..%d\n", AT_READ_CHUNK_MAX);
                return 2;
            }
            break;
        case 'p':
            port = (int)parse_ulong(optarg);
            break;
        case 'w':
            web_dir = optarg;
            break;
        case 'f':
            force = 1;
            break;
        case 'v':
            g_verbose = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 2;
        }
    }

    int fd;
    char *owned = NULL;

    if (strcmp(cmd, "info") == 0)
        return cmd_info(device);

    if (strcmp(cmd, "peek") == 0 || strcmp(cmd, "read") == 0) {
        if (!have_addr || !have_size) {
            fprintf(stderr, "%s requires -a ADDR -n SIZE\n", cmd);
            return 2;
        }
        if (strcmp(cmd, "read") == 0 && !out_path) {
            fprintf(stderr, "read requires -o FILE\n");
            return 2;
        }
        if (open_radio(device, &fd, &owned) != 0) {
            free(owned);
            return 1;
        }
        struct rw_ctx c = {
            .addr = addr,
            .size = size,
            .chunk = chunk,
            .path = (strcmp(cmd, "read") == 0) ? out_path : NULL,
        };
        int rc = with_program_mode(fd, do_read, &c);
        at_serial_close(fd);
        free(owned);
        return rc ? 1 : 0;
    }

    if (strcmp(cmd, "write") == 0) {
        if (!have_addr || !in_path) {
            fprintf(stderr, "write requires -a ADDR -i FILE\n");
            return 2;
        }
        if (open_radio(device, &fd, &owned) != 0) {
            free(owned);
            return 1;
        }
        struct rw_ctx c = { .addr = addr, .path = in_path };
        int rc = with_program_mode(fd, do_write, &c);
        at_serial_close(fd);
        free(owned);
        return rc ? 1 : 0;
    }

    if (strcmp(cmd, "dump") == 0) {
        if (!out_path) {
            fprintf(stderr, "dump requires -o FILE\n");
            return 2;
        }
        if (open_radio(device, &fd, &owned) != 0) {
            free(owned);
            return 1;
        }
        struct dump_ctx c = { .path = out_path, .chunk = chunk };
        int rc = with_program_mode(fd, do_dump, &c);
        at_serial_close(fd);
        free(owned);
        return rc ? 1 : 0;
    }

    if (strcmp(cmd, "restore") == 0) {
        if (!in_path) {
            fprintf(stderr, "restore requires -i FILE\n");
            return 2;
        }
        struct restore_ctx c;
        memset(&c, 0, sizeof(c));
        c.path = in_path;
        c.force = force;
        if (at_image_load(&c.img, in_path) != 0)
            return 1;
        if (open_radio(device, &fd, &owned) != 0) {
            at_image_free(&c.img);
            free(owned);
            return 1;
        }
        int rc = with_program_mode(fd, do_restore, &c);
        at_serial_close(fd);
        free(owned);
        at_image_free(&c.img);
        if (rc == 0)
            fprintf(stderr, "restore complete — radio will apply changes after END\n");
        return rc ? 1 : 0;
    }

    if (strcmp(cmd, "serve") == 0)
        return at_cmd_serve(device, chunk, web_dir, port < 0 ? 8780 : port);

    if (strcmp(cmd, "ui") == 0)
        return at_cmd_ui(device, chunk, web_dir, port < 0 ? 0 : port);

    fprintf(stderr, "unknown command: %s\n", cmd);
    usage(argv[0]);
    return 2;
}
