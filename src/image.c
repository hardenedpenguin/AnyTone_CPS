#include "image.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void at_image_init(struct at_image *img)
{
    memset(img, 0, sizeof(*img));
    img->version = AT_IMAGE_VERSION;
}

void at_image_free(struct at_image *img)
{
    if (!img)
        return;
    if (img->segs) {
        for (uint32_t i = 0; i < img->nseg; i++)
            free(img->segs[i].data);
        free(img->segs);
    }
    memset(img, 0, sizeof(*img));
}

int at_image_add_segment(struct at_image *img, uint32_t addr,
                         const void *data, uint32_t size)
{
    struct at_image_segment *ns =
        realloc(img->segs, (img->nseg + 1) * sizeof(*img->segs));
    if (!ns)
        return -1;
    img->segs = ns;

    uint8_t *copy = malloc(size);
    if (!copy)
        return -1;
    memcpy(copy, data, size);

    img->segs[img->nseg].addr = addr;
    img->segs[img->nseg].size = size;
    img->segs[img->nseg].data = copy;
    img->nseg++;
    return 0;
}

static int write_u32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v & 0xff),
        (uint8_t)((v >> 8) & 0xff),
        (uint8_t)((v >> 16) & 0xff),
        (uint8_t)((v >> 24) & 0xff),
    };
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int read_u32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return -1;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
    return 0;
}

int at_image_save(const struct at_image *img, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    if (fwrite(AT_IMAGE_MAGIC, 1, 4, f) != 4)
        goto fail;
    if (write_u32(f, img->version) != 0)
        goto fail;
    if (fwrite(img->model, 1, 8, f) != 8)
        goto fail;
    if (fwrite(img->fw_version, 1, 8, f) != 8)
        goto fail;
    if (fputc(img->bands, f) == EOF)
        goto fail;
    /* 3 bytes reserved */
    if (fputc(0, f) == EOF || fputc(0, f) == EOF || fputc(0, f) == EOF)
        goto fail;
    if (write_u32(f, img->nseg) != 0)
        goto fail;

    for (uint32_t i = 0; i < img->nseg; i++) {
        if (write_u32(f, img->segs[i].addr) != 0)
            goto fail;
        if (write_u32(f, img->segs[i].size) != 0)
            goto fail;
        if (fwrite(img->segs[i].data, 1, img->segs[i].size, f) != img->segs[i].size)
            goto fail;
    }

    fclose(f);
    return 0;
fail:
    fclose(f);
    return -1;
}

int at_image_load(struct at_image *img, const char *path)
{
    at_image_init(img);

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, AT_IMAGE_MAGIC, 4) != 0) {
        fprintf(stderr, "not an ATCP image: %s\n", path);
        goto fail;
    }
    if (read_u32(f, &img->version) != 0 || img->version != AT_IMAGE_VERSION) {
        fprintf(stderr, "unsupported image version\n");
        goto fail;
    }
    if (fread(img->model, 1, 8, f) != 8)
        goto fail;
    if (fread(img->fw_version, 1, 8, f) != 8)
        goto fail;
    int bands = fgetc(f);
    if (bands == EOF)
        goto fail;
    img->bands = (uint8_t)bands;
    if (fgetc(f) == EOF || fgetc(f) == EOF || fgetc(f) == EOF)
        goto fail;
    if (read_u32(f, &img->nseg) != 0)
        goto fail;

    img->segs = calloc(img->nseg, sizeof(*img->segs));
    if (!img->segs)
        goto fail;

    for (uint32_t i = 0; i < img->nseg; i++) {
        if (read_u32(f, &img->segs[i].addr) != 0)
            goto fail;
        if (read_u32(f, &img->segs[i].size) != 0)
            goto fail;
        img->segs[i].data = malloc(img->segs[i].size);
        if (!img->segs[i].data)
            goto fail;
        if (fread(img->segs[i].data, 1, img->segs[i].size, f) != img->segs[i].size)
            goto fail;
    }

    fclose(f);
    return 0;
fail:
    fclose(f);
    at_image_free(img);
    return -1;
}

int at_dump_regions(int fd, const struct at_region *regions, size_t nregions,
                    struct at_image *img, unsigned chunk_size,
                    at_progress_fn progress, void *ud)
{
    uint64_t total = 0, done = 0;
    for (size_t i = 0; i < nregions; i++)
        total += regions[i].size;

    for (size_t i = 0; i < nregions; i++) {
        uint8_t *buf = malloc(regions[i].size);
        if (!buf)
            return -1;
        if (at_mem_read(fd, regions[i].addr, buf, regions[i].size, chunk_size) != 0) {
            free(buf);
            fprintf(stderr, "read failed at 0x%08x (%u bytes)\n",
                    regions[i].addr, regions[i].size);
            return -1;
        }
        if (at_image_add_segment(img, regions[i].addr, buf, regions[i].size) != 0) {
            free(buf);
            return -1;
        }
        free(buf);
        done += regions[i].size;
        if (progress)
            progress(done, total, ud);
    }
    return 0;
}

int at_restore_image(int fd, const struct at_image *img,
                     at_progress_fn progress, void *ud)
{
    uint64_t total = 0, done = 0;
    for (uint32_t i = 0; i < img->nseg; i++)
        total += img->segs[i].size;

    for (uint32_t i = 0; i < img->nseg; i++) {
        if (at_mem_write(fd, img->segs[i].addr, img->segs[i].data,
                         img->segs[i].size) != 0) {
            fprintf(stderr, "write failed at 0x%08x (%u bytes)\n",
                    img->segs[i].addr, img->segs[i].size);
            return -1;
        }
        done += img->segs[i].size;
        if (progress)
            progress(done, total, ud);
    }
    return 0;
}

#include "memmap_d878uv.inc"
#include "schema_catalog.h"

const struct at_region *at_regions_for_model(const char *model, size_t *count)
{
    const struct at_schema_id *id = at_schema_lookup(model);
    if (id) {
        *count = id->nregions;
        return id->regions;
    }
    /* Last-resort legacy family map for unknown models */
    *count = AT_REGIONS_D878UV_COUNT;
    return AT_REGIONS_D878UV;
}

const uint8_t *at_image_find(const struct at_image *img, uint32_t addr,
                             uint32_t need)
{
    for (uint32_t i = 0; i < img->nseg; i++) {
        uint32_t a = img->segs[i].addr;
        uint32_t s = img->segs[i].size;
        if (addr >= a && (addr + need) <= (a + s))
            return img->segs[i].data + (addr - a);
    }
    return NULL;
}

uint8_t *at_image_find_mut(struct at_image *img, uint32_t addr, uint32_t need)
{
    return (uint8_t *)at_image_find(img, addr, need);
}
