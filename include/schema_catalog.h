#ifndef ANYTONE_SCHEMA_CATALOG_H
#define ANYTONE_SCHEMA_CATALOG_H

#include "image.h"

#include <stddef.h>

/* One supported codeplug schema (model + CPS/firmware map). */
struct at_schema_id {
    const char *key;          /* e.g. "d878uv2_v4.00" */
    const char *model_id;     /* radio Identify model, e.g. "D878UV2" */
    const char *cps_firmware; /* CPS/schema firmware, e.g. "4.00" */
    const char *json_name;    /* file under web/schema/ */
    const struct at_region *regions;
    size_t nregions;
};

/* Resolve best schema for a radio model string from Identify.
 * Prefers exact model match; falls back to family default when known.
 * Returns NULL if nothing suitable is compiled in. */
const struct at_schema_id *at_schema_lookup(const char *model);

/* Enumerate all compiled schemas (NULL-terminated via *count). */
const struct at_schema_id *at_schema_catalog(size_t *count);

#endif
