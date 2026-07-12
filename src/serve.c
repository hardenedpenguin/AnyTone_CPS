#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "anytone.h"
#include "codeplug.h"
#include "desktop.h"
#include "http.h"
#include "schema.h"
#include "schema_catalog.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct serve_state {
    char *device;
    unsigned chunk;
    struct at_image image;
    struct at_codeplug codeplug;
    cJSON *optional; /* optional_settings object */
    struct at_schema *schema;
    int have_image;
    int have_codeplug;
    char webroot[1024];
    char schema_path[1024];
};

static int serve_load_schema_for_model(struct serve_state *st, const char *model);

static char *json_error(const char *msg)
{
    char *s = malloc(strlen(msg) + 64);
    if (!s)
        return NULL;
    /* Escape quotes crudely */
    sprintf(s, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return s;
}

static void serve_progress(uint64_t done, uint64_t total, void *ud)
{
    (void)ud;
    int pct = total ? (int)((done * 100) / total) : 100;
    fprintf(stderr, "\r%3d%%  %llu / %llu bytes", pct,
            (unsigned long long)done, (unsigned long long)total);
    if (done >= total)
        fputc('\n', stderr);
    fflush(stderr);
}

static void refresh_optional(struct serve_state *st)
{
    if (st->optional) {
        cJSON_Delete(st->optional);
        st->optional = NULL;
    }
    if (st->schema && st->have_image)
        st->optional = at_schema_decode_optional(st->schema, &st->image);
}

static char *json_ok_codeplug(struct serve_state *st)
{
    char *cp_json = at_codeplug_to_json(&st->codeplug);
    if (!cp_json)
        return NULL;

    cJSON *wrap = cJSON_CreateObject();
    cJSON_AddBoolToObject(wrap, "ok", 1);
    cJSON *cp = cJSON_Parse(cp_json);
    free(cp_json);
    if (!cp) {
        cJSON_Delete(wrap);
        return NULL;
    }
    if (st->optional)
        cJSON_AddItemToObject(cp, "optional_settings", cJSON_Duplicate(st->optional, 1));
    else
        cJSON_AddObjectToObject(cp, "optional_settings");
    if (st->schema) {
        cJSON_AddStringToObject(cp, "schema_model", at_schema_model(st->schema));
        cJSON_AddStringToObject(cp, "schema_firmware", at_schema_firmware(st->schema));
    }
    cJSON_AddItemToObject(wrap, "codeplug", cp);
    char *out = cJSON_PrintUnformatted(wrap);
    cJSON_Delete(wrap);
    return out;
}

static int apply_codeplug_json(struct serve_state *st, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (!root)
        return -1;

    cJSON *opt = cJSON_GetObjectItemCaseSensitive(root, "optional_settings");
    if (opt) {
        cJSON_DetachItemViaPointer(root, opt);
        if (st->optional)
            cJSON_Delete(st->optional);
        st->optional = opt;
    }

    char *without_opt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!without_opt)
        return -1;

    struct at_codeplug tmp;
    at_codeplug_init(&tmp);
    int rc = at_codeplug_from_json(&tmp, without_opt);
    free(without_opt);
    if (rc != 0)
        return -1;
    at_codeplug_free(&st->codeplug);
    st->codeplug = tmp;
    st->have_codeplug = 1;
    return 0;
}

static int load_from_radio(struct serve_state *st)
{
    int fd;
    char *owned = NULL;
    const char *path = st->device;
    if (!path) {
        char *devs[4];
        int n = at_serial_detect(devs, 4);
        if (n <= 0)
            return -1;
        owned = devs[0];
        for (int i = 1; i < n; i++)
            free(devs[i]);
        path = owned;
    }

    fd = at_serial_open(path);
    free(owned);
    if (fd < 0)
        return -1;

    if (at_enter_program(fd) != 0) {
        at_serial_close(fd);
        return -1;
    }

    struct at_radio_info info;
    if (at_identify(fd, &info) != 0) {
        at_leave_program(fd);
        at_serial_close(fd);
        return -1;
    }

    at_image_free(&st->image);
    at_image_init(&st->image);
    memcpy(st->image.model, info.model, sizeof(st->image.model));
    memcpy(st->image.fw_version, info.version, sizeof(st->image.fw_version));
    st->image.bands = info.bands;

    if (serve_load_schema_for_model(st, info.model) != 0)
        fprintf(stderr, "warning: continuing with previous/default schema\n");

    size_t nreg = 0;
    const struct at_region *regs = at_regions_for_model(info.model, &nreg);
    const struct at_schema_id *sid = at_schema_lookup(info.model);
    fprintf(stderr, "Reading %zu regions from %s (%s) using CPS %s map...\n",
            nreg, info.model, at_model_friendly(info.model),
            sid ? sid->cps_firmware : (st->schema ? at_schema_firmware(st->schema) : "?"));
    int rc = at_dump_regions(fd, regs, nreg, &st->image, st->chunk, serve_progress, NULL);
    if (rc == 0)
        fprintf(stderr, "Read complete.\n");
    at_leave_program(fd);
    at_serial_close(fd);
    if (rc != 0)
        return -1;

    st->have_image = 1;
    if (at_codeplug_from_image(&st->codeplug, &st->image) != 0)
        return -1;
    st->have_codeplug = 1;
    refresh_optional(st);
    return 0;
}

static int encode_image(struct serve_state *st)
{
    /* Schema optional banks first, then structured codeplug tables. Contact /
     * channel bitmaps live in both worlds; codeplug must win on export. */
    if (st->schema && st->optional)
        at_schema_encode_optional(st->schema, &st->image, st->optional);
    return at_codeplug_to_image(&st->codeplug, &st->image);
}

static int write_to_radio(struct serve_state *st)
{
    if (!st->have_image || !st->have_codeplug)
        return -1;
    if (encode_image(st) != 0)
        return -1;

    int fd;
    char *owned = NULL;
    const char *path = st->device;
    if (!path) {
        char *devs[4];
        int n = at_serial_detect(devs, 4);
        if (n <= 0)
            return -1;
        owned = devs[0];
        for (int i = 1; i < n; i++)
            free(devs[i]);
        path = owned;
    }
    fd = at_serial_open(path);
    free(owned);
    if (fd < 0)
        return -1;

    if (at_enter_program(fd) != 0) {
        at_serial_close(fd);
        return -1;
    }
    fprintf(stderr, "Writing codeplug to radio...\n");
    int rc = at_restore_image(fd, &st->image, serve_progress, NULL);
    if (rc == 0)
        fprintf(stderr, "Write complete.\n");
    at_leave_program(fd);
    at_serial_close(fd);
    return rc;
}

static int api_handler(const char *method, const char *path, const char *body,
                       size_t body_len, const char **resp_ct, char **resp_body,
                       size_t *resp_len, void *ud)
{
    struct serve_state *st = ud;
    *resp_ct = "application/json";
    *resp_body = NULL;
    *resp_len = 0;
    (void)body_len;

    if (strcmp(path, "/api/schema") == 0 && strcmp(method, "GET") == 0) {
        FILE *f = fopen(st->schema_path, "rb");
        if (!f) {
            *resp_body = json_error("schema not found");
            *resp_len = strlen(*resp_body);
            return 404;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char *buf = malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return 500;
        }
        fread(buf, 1, (size_t)sz, f);
        fclose(f);
        buf[sz] = '\0';
        *resp_body = buf;
        *resp_len = (size_t)sz;
        return 200;
    }

    if (strcmp(path, "/api/codeplug") == 0 && strcmp(method, "GET") == 0) {
        if (!st->have_codeplug) {
            *resp_body = strdup("{\"ok\":true,\"codeplug\":null}");
        } else {
            *resp_body = json_ok_codeplug(st);
        }
        if (!*resp_body)
            return 500;
        *resp_len = strlen(*resp_body);
        return 200;
    }

    if (strcmp(path, "/api/codeplug") == 0 && strcmp(method, "PUT") == 0) {
        if (apply_codeplug_json(st, body) != 0) {
            *resp_body = json_error("invalid codeplug JSON");
            *resp_len = strlen(*resp_body);
            return 400;
        }
        if (st->have_image) {
            if (encode_image(st) != 0) {
                *resp_body = json_error("failed to encode codeplug into image");
                *resp_len = strlen(*resp_body);
                return 500;
            }
        }
        *resp_body = strdup("{\"ok\":true}");
        *resp_len = strlen(*resp_body);
        return 200;
    }

    if (strcmp(path, "/api/radio/read") == 0 && strcmp(method, "POST") == 0) {
        if (load_from_radio(st) != 0) {
            *resp_body = json_error("failed to read radio (cable, permissions, or timeout)");
            *resp_len = strlen(*resp_body);
            return 500;
        }
        *resp_body = json_ok_codeplug(st);
        if (!*resp_body)
            return 500;
        *resp_len = strlen(*resp_body);
        return 200;
    }

    if (strcmp(path, "/api/radio/write") == 0 && strcmp(method, "POST") == 0) {
        if (body && body[0]) {
            if (apply_codeplug_json(st, body) != 0) {
                *resp_body = json_error("invalid codeplug JSON");
                *resp_len = strlen(*resp_body);
                return 400;
            }
        }
        if (!st->have_image) {
            if (load_from_radio(st) != 0) {
                *resp_body = json_error("no image loaded; read radio first");
                *resp_len = strlen(*resp_body);
                return 400;
            }
            if (body && body[0])
                apply_codeplug_json(st, body);
        }
        if (write_to_radio(st) != 0) {
            *resp_body = json_error("failed to write radio");
            *resp_len = strlen(*resp_body);
            return 500;
        }
        *resp_body = strdup("{\"ok\":true}");
        *resp_len = strlen(*resp_body);
        return 200;
    }

    if (strcmp(path, "/api/import") == 0 && strcmp(method, "POST") == 0) {
        char tmppath[] = "/tmp/anytoneXXXXXX";
        int tfd = mkstemp(tmppath);
        if (tfd < 0) {
            *resp_body = json_error("temp file");
            *resp_len = strlen(*resp_body);
            return 500;
        }
        if (write(tfd, body, body_len) != (ssize_t)body_len) {
            close(tfd);
            unlink(tmppath);
            *resp_body = json_error("write temp");
            *resp_len = strlen(*resp_body);
            return 500;
        }
        close(tfd);
        at_image_free(&st->image);
        if (at_image_load(&st->image, tmppath) != 0) {
            unlink(tmppath);
            *resp_body = json_error("invalid ATCP image");
            *resp_len = strlen(*resp_body);
            return 400;
        }
        unlink(tmppath);
        st->have_image = 1;
        if (serve_load_schema_for_model(st, st->image.model) != 0)
            fprintf(stderr, "warning: import using previous/default schema\n");
        if (at_codeplug_from_image(&st->codeplug, &st->image) != 0) {
            *resp_body = json_error("decode failed");
            *resp_len = strlen(*resp_body);
            return 500;
        }
        st->have_codeplug = 1;
        refresh_optional(st);
        *resp_body = json_ok_codeplug(st);
        *resp_len = strlen(*resp_body);
        return 200;
    }

    if (strcmp(path, "/api/export") == 0 && strcmp(method, "GET") == 0) {
        if (!st->have_image || !st->have_codeplug) {
            *resp_body = json_error("nothing to export");
            *resp_len = strlen(*resp_body);
            return 400;
        }
        if (encode_image(st) != 0)
            return 500;
        char tmppath[] = "/tmp/anytoneXXXXXX";
        int tfd = mkstemp(tmppath);
        if (tfd < 0)
            return 500;
        close(tfd);
        if (at_image_save(&st->image, tmppath) != 0) {
            unlink(tmppath);
            return 500;
        }
        FILE *f = fopen(tmppath, "rb");
        unlink(tmppath);
        if (!f)
            return 500;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char *buf = malloc((size_t)sz);
        if (!buf) {
            fclose(f);
            return 500;
        }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf);
            fclose(f);
            return 500;
        }
        fclose(f);
        *resp_ct = "application/octet-stream";
        *resp_body = buf;
        *resp_len = (size_t)sz;
        return 200;
    }

    *resp_body = json_error("not found");
    *resp_len = strlen(*resp_body);
    return 404;
}

static int try_schema_file(char *out, size_t out_sz, const char *webroot,
                           const char *json_name)
{
    const char *roots[] = {
        "web/schema",
        "/usr/local/share/anytone/web/schema",
        "/home/repos/AnyTone/web/schema",
        NULL,
    };
    for (int i = 0; roots[i]; i++) {
        snprintf(out, out_sz, "%s/%s", roots[i], json_name);
        if (access(out, R_OK) == 0)
            return 0;
    }
    if (webroot && webroot[0]) {
        snprintf(out, out_sz, "%s/schema/%s", webroot, json_name);
        if (access(out, R_OK) == 0)
            return 0;
    }
    return -1;
}

/* Load (or switch) the optional-settings schema for a radio model. */
static int serve_load_schema_for_model(struct serve_state *st, const char *model)
{
    const struct at_schema_id *id = at_schema_lookup(model);
    if (!id) {
        fprintf(stderr, "warning: no schema catalog entry for model %s\n",
                model ? model : "(null)");
        return -1;
    }

    char path[1024];
    if (try_schema_file(path, sizeof(path), st->webroot, id->json_name) != 0) {
        fprintf(stderr, "warning: schema file %s not found\n", id->json_name);
        return -1;
    }

    /* Already on this schema file */
    if (st->schema && strcmp(st->schema_path, path) == 0)
        return 0;

    struct at_schema *neu = at_schema_load_file(path);
    if (!neu) {
        fprintf(stderr, "warning: failed to parse schema %s\n", path);
        return -1;
    }

    at_schema_free(st->schema);
    st->schema = neu;
    snprintf(st->schema_path, sizeof(st->schema_path), "%s", path);
    fprintf(stderr, "Loaded schema %s (%s CPS %s) for model %s\n", path,
            id->model_id, id->cps_firmware, model ? model : "?");
    return 0;
}

static void serve_state_cleanup(struct serve_state *st)
{
    free(st->device);
    at_image_free(&st->image);
    at_codeplug_free(&st->codeplug);
    if (st->optional)
        cJSON_Delete(st->optional);
    at_schema_free(st->schema);
}

static int serve_state_init(struct serve_state *st, const char *device,
                            unsigned chunk, const char *webroot)
{
    memset(st, 0, sizeof(*st));
    st->device = device ? strdup(device) : NULL;
    st->chunk = chunk ? chunk : AT_READ_CHUNK_MAX;
    at_image_init(&st->image);
    at_codeplug_init(&st->codeplug);

    if (webroot && webroot[0]) {
        snprintf(st->webroot, sizeof(st->webroot), "%s", webroot);
    } else if (access("web/index.html", R_OK) == 0) {
        snprintf(st->webroot, sizeof(st->webroot), "web");
    } else if (access("/usr/local/share/anytone/web/index.html", R_OK) == 0) {
        snprintf(st->webroot, sizeof(st->webroot), "/usr/local/share/anytone/web");
    } else if (access("/home/repos/AnyTone/web/index.html", R_OK) == 0) {
        snprintf(st->webroot, sizeof(st->webroot), "/home/repos/AnyTone/web");
    } else {
        fprintf(stderr, "cannot find web/ UI directory\n");
        serve_state_cleanup(st);
        return -1;
    }

    /* Default schema until a radio/ATCP model is known (878UV II is primary). */
    if (serve_load_schema_for_model(st, "D878UV2") != 0)
        fprintf(stderr, "warning: no default schema loaded\n");
    return 0;
}

int at_cmd_serve(const char *device, unsigned chunk, const char *webroot, int port)
{
    struct serve_state st;
    if (serve_state_init(&st, device, chunk, webroot) != 0)
        return 1;

    int rc = at_http_serve("127.0.0.1", port, st.webroot, api_handler, &st);
    serve_state_cleanup(&st);
    return rc ? 1 : 0;
}

int at_cmd_ui(const char *device, unsigned chunk, const char *webroot, int port)
{
    if (!at_desktop_available()) {
        fprintf(stderr,
                "Desktop UI requires WebKitGTK. Falling back to browser mode.\n"
                "Run: anytone serve   then open http://127.0.0.1:%d/\n",
                port > 0 ? port : 8780);
        return at_cmd_serve(device, chunk, webroot, port > 0 ? port : 8780);
    }

    struct serve_state st;
    if (serve_state_init(&st, device, chunk, webroot) != 0)
        return 1;

    /* Default for ui: ephemeral port so a running `serve` is not disturbed. */
    int want_port = port;
    int bound = 0;
    struct at_http_server *srv =
        at_http_server_create("127.0.0.1", want_port, st.webroot, api_handler, &st,
                              &bound);
    if (!srv) {
        fprintf(stderr, "failed to bind HTTP listener on port %d\n", want_port);
        serve_state_cleanup(&st);
        return 1;
    }

    if (at_http_server_start(srv) != 0) {
        fprintf(stderr, "failed to start HTTP thread\n");
        at_http_server_destroy(srv);
        serve_state_cleanup(&st);
        return 1;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", bound);
    fprintf(stderr, "Desktop CPS at %s\n", url);

    int rc = at_desktop_run(url, "Anytone Codeplug Studio");
    at_http_server_stop(srv);
    at_http_server_destroy(srv);
    serve_state_cleanup(&st);
    return rc ? 1 : 0;
}
