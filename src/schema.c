#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "schema.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct at_field {
    char *name;
    char *type;   /* int, enum, string */
    char *format; /* unsigned, signed, bcd, ascii */
    char *endian; /* little, big */
    unsigned bit_offset;
    unsigned bit_width;
    cJSON *items; /* enum items array or NULL */
};

struct at_element {
    char *name;
    uint32_t address;
    uint32_t size;
    int nfields;
    struct at_field *fields;
    int bank_count; /* >1 => consecutive packed copies */
};

struct at_schema {
    char *model;
    char *firmware;
    char *name;
    int nelements;
    struct at_element *elements;
    char **optional_names;
    size_t noptional;
    cJSON *root; /* keep alive for enum item pointers if needed */
};

static uint64_t get_bits_le(const uint8_t *data, size_t size, unsigned bit_off,
                            unsigned bit_width)
{
    if (bit_width == 0 || bit_width > 64)
        return 0;
    uint64_t val = 0;
    for (unsigned i = 0; i < bit_width; i++) {
        unsigned b = bit_off + i;
        unsigned byte = b / 8;
        unsigned bit = b % 8;
        if (byte >= size)
            break;
        if (data[byte] & (1u << bit))
            val |= (1ull << i);
    }
    return val;
}

static void set_bits_le(uint8_t *data, size_t size, unsigned bit_off,
                        unsigned bit_width, uint64_t val)
{
    for (unsigned i = 0; i < bit_width; i++) {
        unsigned b = bit_off + i;
        unsigned byte = b / 8;
        unsigned bit = b % 8;
        if (byte >= size)
            break;
        if (val & (1ull << i))
            data[byte] |= (uint8_t)(1u << bit);
        else
            data[byte] &= (uint8_t)~(1u << bit);
    }
}

static uint64_t read_field_value(const uint8_t *data, size_t size,
                                 const struct at_field *f)
{
    unsigned nbytes = (f->bit_width + 7) / 8;
    /* Aligned byte fields: prefer endian-aware byte extract */
    if ((f->bit_offset % 8) == 0 && (f->bit_width % 8) == 0 && nbytes >= 1 &&
        nbytes <= 8) {
        unsigned byte = f->bit_offset / 8;
        if (byte + nbytes > size)
            return 0;
        if (f->format && strcmp(f->format, "bcd") == 0 && nbytes == 4) {
            return at_bcd8_be_get(data + byte);
        }
        if (f->endian && strcmp(f->endian, "big") == 0) {
            uint64_t v = 0;
            for (unsigned i = 0; i < nbytes; i++)
                v = (v << 8) | data[byte + i];
            return v;
        }
        uint64_t v = 0;
        for (unsigned i = 0; i < nbytes; i++)
            v |= ((uint64_t)data[byte + i]) << (8 * i);
        return v;
    }
    return get_bits_le(data, size, f->bit_offset, f->bit_width);
}

static void write_field_value(uint8_t *data, size_t size, const struct at_field *f,
                              uint64_t val)
{
    unsigned nbytes = (f->bit_width + 7) / 8;
    if ((f->bit_offset % 8) == 0 && (f->bit_width % 8) == 0 && nbytes >= 1 &&
        nbytes <= 8) {
        unsigned byte = f->bit_offset / 8;
        if (byte + nbytes > size)
            return;
        if (f->format && strcmp(f->format, "bcd") == 0 && nbytes == 4) {
            at_bcd8_be_set(data + byte, (uint32_t)val);
            return;
        }
        if (f->endian && strcmp(f->endian, "big") == 0) {
            for (unsigned i = 0; i < nbytes; i++)
                data[byte + nbytes - 1 - i] = (uint8_t)((val >> (8 * i)) & 0xff);
            return;
        }
        for (unsigned i = 0; i < nbytes; i++)
            data[byte + i] = (uint8_t)((val >> (8 * i)) & 0xff);
        return;
    }
    set_bits_le(data, size, f->bit_offset, f->bit_width, val);
}

static struct at_element *find_element(struct at_schema *sch, const char *name)
{
    for (int i = 0; i < sch->nelements; i++) {
        if (strcmp(sch->elements[i].name, name) == 0)
            return &sch->elements[i];
    }
    return NULL;
}

struct at_schema *at_schema_load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[sz] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return NULL;

    struct at_schema *sch = calloc(1, sizeof(*sch));
    if (!sch) {
        cJSON_Delete(root);
        return NULL;
    }
    sch->root = root;

    cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "model");
    cJSON *fw = cJSON_GetObjectItemCaseSensitive(root, "firmware");
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(m))
        sch->model = strdup(m->valuestring);
    if (cJSON_IsString(fw))
        sch->firmware = strdup(fw->valuestring);
    if (cJSON_IsString(nm))
        sch->name = strdup(nm->valuestring);

    cJSON *els = cJSON_GetObjectItemCaseSensitive(root, "elements");
    if (cJSON_IsArray(els)) {
        sch->nelements = cJSON_GetArraySize(els);
        sch->elements = calloc((size_t)sch->nelements, sizeof(*sch->elements));
        int i = 0;
        cJSON *el;
        cJSON_ArrayForEach(el, els) {
            if (i >= sch->nelements)
                break;
            struct at_element *e = &sch->elements[i++];
            cJSON *n = cJSON_GetObjectItemCaseSensitive(el, "name");
            cJSON *a = cJSON_GetObjectItemCaseSensitive(el, "address");
            cJSON *s = cJSON_GetObjectItemCaseSensitive(el, "size");
            if (cJSON_IsString(n))
                e->name = strdup(n->valuestring);
            if (cJSON_IsNumber(a))
                e->address = (uint32_t)a->valuedouble;
            if (cJSON_IsNumber(s))
                e->size = (uint32_t)s->valuedouble;
            cJSON *bc = cJSON_GetObjectItemCaseSensitive(el, "bank_count");
            e->bank_count = cJSON_IsNumber(bc) ? (int)bc->valuedouble : 1;
            if (e->bank_count < 1)
                e->bank_count = 1;
            cJSON *fields = cJSON_GetObjectItemCaseSensitive(el, "fields");
            if (!cJSON_IsArray(fields))
                continue;
            e->nfields = cJSON_GetArraySize(fields);
            e->fields = calloc((size_t)e->nfields, sizeof(*e->fields));
            int fi = 0;
            cJSON *field;
            cJSON_ArrayForEach(field, fields) {
                if (fi >= e->nfields)
                    break;
                struct at_field *ff = &e->fields[fi++];
                cJSON *fn = cJSON_GetObjectItemCaseSensitive(field, "name");
                cJSON *ty = cJSON_GetObjectItemCaseSensitive(field, "type");
                cJSON *fmt = cJSON_GetObjectItemCaseSensitive(field, "format");
                cJSON *en = cJSON_GetObjectItemCaseSensitive(field, "endian");
                cJSON *bo = cJSON_GetObjectItemCaseSensitive(field, "bit_offset");
                cJSON *bw = cJSON_GetObjectItemCaseSensitive(field, "bit_width");
                if (cJSON_IsString(fn))
                    ff->name = strdup(fn->valuestring);
                if (cJSON_IsString(ty))
                    ff->type = strdup(ty->valuestring);
                if (cJSON_IsString(fmt))
                    ff->format = strdup(fmt->valuestring);
                if (cJSON_IsString(en))
                    ff->endian = strdup(en->valuestring);
                if (cJSON_IsNumber(bo))
                    ff->bit_offset = (unsigned)bo->valuedouble;
                if (cJSON_IsNumber(bw))
                    ff->bit_width = (unsigned)bw->valuedouble;
                ff->items = cJSON_GetObjectItemCaseSensitive(field, "items");
            }
        }
        sch->nelements = i;
    }

    cJSON *cats = cJSON_GetObjectItemCaseSensitive(root, "categories");
    if (cJSON_IsArray(cats)) {
        cJSON *cat;
        cJSON_ArrayForEach(cat, cats) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(cat, "id");
            if (!cJSON_IsString(id) || strcmp(id->valuestring, "optional_settings") != 0)
                continue;
            cJSON *names = cJSON_GetObjectItemCaseSensitive(cat, "elements");
            if (!cJSON_IsArray(names))
                break;
            sch->noptional = (size_t)cJSON_GetArraySize(names);
            sch->optional_names = calloc(sch->noptional, sizeof(char *));
            size_t j = 0;
            cJSON *nm2;
            cJSON_ArrayForEach(nm2, names) {
                if (j >= sch->noptional)
                    break;
                if (cJSON_IsString(nm2))
                    sch->optional_names[j++] = strdup(nm2->valuestring);
            }
            sch->noptional = j;
            break;
        }
    }

    return sch;
}

void at_schema_free(struct at_schema *sch)
{
    if (!sch)
        return;
    for (int i = 0; i < sch->nelements; i++) {
        struct at_element *e = &sch->elements[i];
        for (int f = 0; f < e->nfields; f++) {
            free(e->fields[f].name);
            free(e->fields[f].type);
            free(e->fields[f].format);
            free(e->fields[f].endian);
        }
        free(e->fields);
        free(e->name);
    }
    free(sch->elements);
    for (size_t i = 0; i < sch->noptional; i++)
        free(sch->optional_names[i]);
    free(sch->optional_names);
    free(sch->model);
    free(sch->firmware);
    free(sch->name);
    cJSON_Delete(sch->root);
    free(sch);
}

const char *at_schema_model(const struct at_schema *sch)
{
    return sch && sch->model ? sch->model : "";
}

const char *at_schema_firmware(const struct at_schema *sch)
{
    return sch && sch->firmware ? sch->firmware : "";
}

const char **at_schema_optional_names(const struct at_schema *sch, size_t *count)
{
    if (!sch) {
        *count = 0;
        return NULL;
    }
    *count = sch->noptional;
    return (const char **)sch->optional_names;
}

static cJSON *decode_fields(const struct at_element *e, const uint8_t *raw)
{
    cJSON *obj = cJSON_CreateObject();
    for (int i = 0; i < e->nfields; i++) {
        struct at_field *f = &e->fields[i];
        if (!f->name)
            continue;
        if (f->type && strcmp(f->type, "string") == 0) {
            unsigned byte = f->bit_offset / 8;
            unsigned nbytes = f->bit_width / 8;
            char tmp[256];
            if (nbytes >= sizeof(tmp))
                nbytes = sizeof(tmp) - 1;
            at_read_ascii(raw + byte, nbytes, tmp, sizeof(tmp));
            cJSON_AddStringToObject(obj, f->name, tmp);
        } else {
            uint64_t v = read_field_value(raw, e->size, f);
            cJSON_AddNumberToObject(obj, f->name, (double)v);
        }
    }
    return obj;
}

static int encode_fields(const struct at_element *e, uint8_t *raw, const cJSON *fields)
{
    for (int i = 0; i < e->nfields; i++) {
        struct at_field *f = &e->fields[i];
        if (!f->name)
            continue;
        cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)fields, f->name);
        if (!v)
            continue;
        if (f->type && strcmp(f->type, "string") == 0 && cJSON_IsString(v)) {
            unsigned byte = f->bit_offset / 8;
            unsigned nbytes = f->bit_width / 8;
            at_write_ascii(raw + byte, nbytes, v->valuestring);
        } else if (cJSON_IsNumber(v) || cJSON_IsBool(v)) {
            write_field_value(raw, e->size, f, (uint64_t)v->valuedouble);
        }
    }
    return 0;
}

cJSON *at_schema_decode_element(const struct at_schema *sch, const char *name,
                                const struct at_image *img)
{
    struct at_element *e = find_element((struct at_schema *)sch, name);
    if (!e || e->size == 0)
        return NULL;
    const uint8_t *raw = at_image_find(img, e->address, e->size);
    if (!raw)
        return NULL;
    return decode_fields(e, raw);
}

int at_schema_encode_element(const struct at_schema *sch, const char *name,
                             struct at_image *img, const cJSON *fields)
{
    struct at_element *e = find_element((struct at_schema *)sch, name);
    if (!e || e->size == 0 || !cJSON_IsObject(fields))
        return -1;
    uint8_t *raw = at_image_find_mut(img, e->address, e->size);
    if (!raw)
        return -1;
    return encode_fields(e, raw, fields);
}

static cJSON *decode_bank(const struct at_element *e, const struct at_image *img)
{
    cJSON *arr = cJSON_CreateArray();
    uint32_t span = e->size * (uint32_t)e->bank_count;
    const uint8_t *base = at_image_find(img, e->address, span);
    if (!base) {
        /* Fall back to first slot only if full bank wasn't dumped */
        const uint8_t *raw = at_image_find(img, e->address, e->size);
        if (raw)
            cJSON_AddItemToArray(arr, decode_fields(e, raw));
        return arr;
    }
    for (int i = 0; i < e->bank_count; i++)
        cJSON_AddItemToArray(arr, decode_fields(e, base + (size_t)i * e->size));
    return arr;
}

static int encode_bank(const struct at_element *e, struct at_image *img, const cJSON *arr)
{
    if (!cJSON_IsArray(arr))
        return -1;
    uint32_t span = e->size * (uint32_t)e->bank_count;
    uint8_t *base = at_image_find_mut(img, e->address, span);
    if (!base)
        return -1;
    int n = cJSON_GetArraySize(arr);
    if (n > e->bank_count)
        n = e->bank_count;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem((cJSON *)arr, i);
        if (cJSON_IsObject(item))
            encode_fields(e, base + (size_t)i * e->size, item);
    }
    return 0;
}

cJSON *at_schema_decode_optional(const struct at_schema *sch,
                                 const struct at_image *img)
{
    cJSON *out = cJSON_CreateObject();
    if (!sch)
        return out;
    for (size_t i = 0; i < sch->noptional; i++) {
        const char *name = sch->optional_names[i];
        struct at_element *e = find_element((struct at_schema *)sch, name);
        if (!e)
            continue;
        if (e->bank_count > 1) {
            cJSON *arr = decode_bank(e, img);
            if (arr)
                cJSON_AddItemToObject(out, name, arr);
        } else {
            cJSON *el = at_schema_decode_element(sch, name, img);
            if (el)
                cJSON_AddItemToObject(out, name, el);
        }
    }
    return out;
}

int at_schema_encode_optional(const struct at_schema *sch, struct at_image *img,
                              const cJSON *optional)
{
    if (!sch || !cJSON_IsObject(optional))
        return -1;
    for (size_t i = 0; i < sch->noptional; i++) {
        const char *name = sch->optional_names[i];
        cJSON *el = cJSON_GetObjectItemCaseSensitive((cJSON *)optional, name);
        if (!el)
            continue;
        struct at_element *e = find_element((struct at_schema *)sch, name);
        if (!e)
            continue;
        if (e->bank_count > 1 && cJSON_IsArray(el))
            encode_bank(e, img, el);
        else if (cJSON_IsObject(el))
            at_schema_encode_element(sch, name, img, el);
    }
    return 0;
}
