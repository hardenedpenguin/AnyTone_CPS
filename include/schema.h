#ifndef ANYTONE_SCHEMA_H
#define ANYTONE_SCHEMA_H

#include "cJSON.h"
#include "image.h"

#include <stddef.h>
#include <stdint.h>

struct at_schema;

struct at_schema *at_schema_load_file(const char *path);
void at_schema_free(struct at_schema *sch);

const char *at_schema_model(const struct at_schema *sch);
const char *at_schema_firmware(const struct at_schema *sch);

/* Decode a named absolute element into a JSON object of field → value.
 * Returns a new cJSON object (caller frees) or NULL. */
cJSON *at_schema_decode_element(const struct at_schema *sch, const char *name,
                                const struct at_image *img);

/* Apply JSON object fields onto the named element in the image. */
int at_schema_encode_element(const struct at_schema *sch, const char *name,
                             struct at_image *img, const cJSON *fields);

/* Decode all Optional Settings–category elements into one object:
 * { "General Settings": {...}, "Boot Settings": {...}, ... } */
cJSON *at_schema_decode_optional(const struct at_schema *sch,
                                 const struct at_image *img);

int at_schema_encode_optional(const struct at_schema *sch, struct at_image *img,
                              const cJSON *optional);

/* List element names in the optional-settings category (NULL-terminated).
 * Pointers owned by schema. */
const char **at_schema_optional_names(const struct at_schema *sch, size_t *count);

#endif
