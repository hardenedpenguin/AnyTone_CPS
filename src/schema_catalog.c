#include "schema_catalog.h"

#include <string.h>

#include "memmap_d878uv_v400.inc"
#include "memmap_d878uv2_v400.inc"
#include "memmap_d578uv_v121.inc"

static const struct at_schema_id CATALOG[] = {
    {
        .key = "d878uv2_v4.00",
        .model_id = "D878UV2",
        .cps_firmware = "4.00",
        .json_name = "d878uv2_v4.00.json",
        .regions = AT_REGIONS_D878UV2_V400,
        .nregions = AT_REGIONS_D878UV2_V400_COUNT,
    },
    {
        .key = "d878uv_v4.00",
        .model_id = "D878UV",
        .cps_firmware = "4.00",
        .json_name = "d878uv_v4.00.json",
        .regions = AT_REGIONS_D878UV_V400,
        .nregions = AT_REGIONS_D878UV_V400_COUNT,
    },
    {
        .key = "d578uv_v1.21",
        .model_id = "D578UV",
        .cps_firmware = "1.21",
        .json_name = "d578uv_v1.21.json",
        .regions = AT_REGIONS_D578UV_V121,
        .nregions = AT_REGIONS_D578UV_V121_COUNT,
    },
};

static const size_t CATALOG_COUNT = sizeof(CATALOG) / sizeof(CATALOG[0]);

static int model_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

/* Normalize Identify aliases to catalog model_id. */
static const char *canonical_model(const char *model)
{
    if (!model || !model[0])
        return NULL;
    if (model_eq(model, "D878UV2") || model_eq(model, "D878UVII"))
        return "D878UV2";
    if (model_eq(model, "D878UV"))
        return "D878UV";
    if (model_eq(model, "D578UV") || model_eq(model, "D578UV3") ||
        model_eq(model, "D578UVIII"))
        return "D578UV";
    /* D578UV II uses a different FW line; no dedicated schema yet — fall back
     * to the D578UV/III map with a warning at the call site. */
    if (model_eq(model, "D578UV2") || model_eq(model, "D578UVII"))
        return "D578UV";
    return model;
}

const struct at_schema_id *at_schema_catalog(size_t *count)
{
    if (count)
        *count = CATALOG_COUNT;
    return CATALOG;
}

const struct at_schema_id *at_schema_lookup(const char *model)
{
    const char *canon = canonical_model(model);
    if (!canon)
        return NULL;

    for (size_t i = 0; i < CATALOG_COUNT; i++) {
        if (model_eq(CATALOG[i].model_id, canon))
            return &CATALOG[i];
    }
    return NULL;
}
