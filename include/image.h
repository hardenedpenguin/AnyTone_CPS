#ifndef ANYTONE_IMAGE_H
#define ANYTONE_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "protocol.h"

#define AT_IMAGE_MAGIC   "ATCP"
#define AT_IMAGE_VERSION 1

struct at_region {
    uint32_t addr;
    uint32_t size;
};

struct at_image_segment {
    uint32_t addr;
    uint32_t size;
    uint8_t *data;
};

struct at_image {
    uint32_t version;
    char model[8];
    char fw_version[8];
    uint8_t bands;
    uint32_t nseg;
    struct at_image_segment *segs;
};

void at_image_init(struct at_image *img);
void at_image_free(struct at_image *img);

int at_image_add_segment(struct at_image *img, uint32_t addr,
                         const void *data, uint32_t size);

int at_image_save(const struct at_image *img, const char *path);
int at_image_load(struct at_image *img, const char *path);

/* Progress callback: done_bytes, total_bytes, userdata. */
typedef void (*at_progress_fn)(uint64_t done, uint64_t total, void *ud);

int at_dump_regions(int fd, const struct at_region *regions, size_t nregions,
                    struct at_image *img, unsigned chunk_size,
                    at_progress_fn progress, void *ud);

int at_restore_image(int fd, const struct at_image *img,
                     at_progress_fn progress, void *ud);

const struct at_region *at_regions_for_model(const char *model, size_t *count);

/* Locate bytes covering [addr, addr+need) inside a sparse image. */
const uint8_t *at_image_find(const struct at_image *img, uint32_t addr,
                             uint32_t need);
uint8_t *at_image_find_mut(struct at_image *img, uint32_t addr, uint32_t need);

#endif /* ANYTONE_IMAGE_H */
