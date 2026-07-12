#ifndef ANYTONE_HTTP_H
#define ANYTONE_HTTP_H

#include <stddef.h>

/* Minimal HTTP/1.1 server for the CPS UI.
 * root_dir is where static files (web/) live.
 * on_api(method, path, body, body_len, &resp_ct, &resp_body, &resp_len, ud)
 *   returns HTTP status; owns resp_body via malloc if non-NULL. */
typedef int (*at_http_api_fn)(const char *method, const char *path,
                              const char *body, size_t body_len,
                              const char **resp_ct, char **resp_body,
                              size_t *resp_len, void *ud);

struct at_http_server;

/* Bind and listen. port 0 = ephemeral; actual port written to *out_port if set.
 * Returns NULL on failure. */
struct at_http_server *at_http_server_create(const char *bind_host, int port,
                                             const char *root_dir,
                                             at_http_api_fn api, void *ud,
                                             int *out_port);

int at_http_server_port(const struct at_http_server *srv);

/* Blocking accept loop on the current thread (until stop). */
int at_http_server_run(struct at_http_server *srv);

/* Run accept loop on a background thread. */
int at_http_server_start(struct at_http_server *srv);

/* Unblock run/start and join the background thread if any. Safe to call once. */
void at_http_server_stop(struct at_http_server *srv);

void at_http_server_destroy(struct at_http_server *srv);

/* Convenience: create + run + destroy (legacy serve path). */
int at_http_serve(const char *bind_host, int port, const char *root_dir,
                  at_http_api_fn api, void *ud);

#endif
