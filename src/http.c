#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct at_http_server {
    int sfd;
    int port;
    int threaded;
    volatile int stop;
    pthread_t thr;
    char root[1024];
    at_http_api_fn api;
    void *ud;
};

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "application/octet-stream";
    if (strcmp(dot, ".html") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)
        return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)
        return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".svg") == 0)
        return "image/svg+xml";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".json") == 0)
        return "application/json";
    if (strcmp(dot, ".ico") == 0)
        return "image/x-icon";
    return "application/octet-stream";
}

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_response(int fd, int status, const char *ct, const void *body,
                         size_t body_len)
{
    const char *reason = status == 200   ? "OK"
                         : status == 400 ? "Bad Request"
                         : status == 404 ? "Not Found"
                         : status == 405 ? "Method Not Allowed"
                         : status == 500 ? "Internal Server Error"
                                         : "Error";
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
                     "\r\n",
                     status, reason, ct ? ct : "text/plain", body_len);
    if (send_all(fd, hdr, (size_t)n) != 0)
        return -1;
    if (body_len && body)
        return send_all(fd, body, body_len);
    return 0;
}

static int read_request(int fd, char **req_out, size_t *len_out)
{
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return -1;
    for (;;) {
        if (len + 2048 > cap) {
            cap *= 2;
            char *nbuf = realloc(buf, cap);
            if (!nbuf) {
                free(buf);
                return -1;
            }
            buf = nbuf;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            return -1;
        }
        if (n == 0)
            break;
        len += (size_t)n;
        buf[len] = '\0';
        char *hdr_end = strstr(buf, "\r\n\r\n");
        if (hdr_end) {
            size_t hdr_len = (size_t)(hdr_end - buf) + 4;
            size_t content_len = 0;
            char *cl = strcasestr(buf, "Content-Length:");
            if (cl && cl < hdr_end)
                content_len = (size_t)strtoul(cl + 15, NULL, 10);
            while (len < hdr_len + content_len) {
                if (len + 2048 > cap) {
                    cap *= 2;
                    char *nbuf = realloc(buf, cap);
                    if (!nbuf) {
                        free(buf);
                        return -1;
                    }
                    buf = nbuf;
                }
                n = read(fd, buf + len, cap - len - 1);
                if (n <= 0)
                    break;
                len += (size_t)n;
                buf[len] = '\0';
            }
            break;
        }
        if (len > 16 * 1024 * 1024) {
            free(buf);
            return -1;
        }
    }
    *req_out = buf;
    *len_out = len;
    return 0;
}

static int serve_file(int fd, const char *root, const char *url_path)
{
    char path[1024];
    if (strcmp(url_path, "/") == 0)
        url_path = "/index.html";
    if (strstr(url_path, ".."))
        return send_response(fd, 400, "text/plain", "bad path", 8);

    snprintf(path, sizeof(path), "%s%s", root, url_path);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        if (strcmp(url_path, "/favicon.ico") == 0)
            return send_response(fd, 204, "image/x-icon", "", 0);
        return send_response(fd, 404, "text/plain", "not found", 9);
    }

    struct stat st;
    if (fstat(f, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(f);
        return send_response(fd, 404, "text/plain", "not found", 9);
    }
    char *body = malloc((size_t)st.st_size);
    if (!body) {
        close(f);
        return send_response(fd, 500, "text/plain", "oom", 3);
    }
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t n = read(f, body + got, (size_t)st.st_size - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    close(f);
    int rc = send_response(fd, 200, mime_for(path), body, got);
    free(body);
    return rc;
}

static void handle_client(int cfd, const char *root, at_http_api_fn api, void *ud)
{
    char *req = NULL;
    size_t req_len = 0;
    if (read_request(cfd, &req, &req_len) != 0) {
        close(cfd);
        return;
    }

    char method[16] = {0}, path[512] = {0};
    if (sscanf(req, "%15s %511s", method, path) != 2) {
        send_response(cfd, 400, "text/plain", "bad request", 11);
        free(req);
        close(cfd);
        return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(cfd, 200, "text/plain", "", 0);
        free(req);
        close(cfd);
        return;
    }

    char *hdr_end = strstr(req, "\r\n\r\n");
    const char *body = hdr_end ? hdr_end + 4 : "";
    size_t body_len = hdr_end ? req_len - (size_t)(hdr_end + 4 - req) : 0;

    if (strncmp(path, "/api/", 5) == 0) {
        const char *ct = "application/json";
        char *resp = NULL;
        size_t resp_len = 0;
        int status = api(method, path, body, body_len, &ct, &resp, &resp_len, ud);
        if (!resp) {
            resp = strdup("{\"ok\":false}");
            resp_len = strlen(resp);
            if (status == 200)
                status = 500;
        }
        send_response(cfd, status, ct, resp, resp_len);
        free(resp);
    } else if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        serve_file(cfd, root, path);
    } else {
        send_response(cfd, 405, "text/plain", "method not allowed", 18);
    }

    free(req);
    close(cfd);
}

struct at_http_server *at_http_server_create(const char *bind_host, int port,
                                             const char *root_dir,
                                             at_http_api_fn api, void *ud,
                                             int *out_port)
{
    struct at_http_server *srv = calloc(1, sizeof(*srv));
    if (!srv)
        return NULL;
    srv->sfd = -1;
    srv->api = api;
    srv->ud = ud;
    snprintf(srv->root, sizeof(srv->root), "%s", root_dir ? root_dir : ".");

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        free(srv);
        return NULL;
    }
    int yes = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (!bind_host || strcmp(bind_host, "0.0.0.0") == 0)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, bind_host, &addr.sin_addr) != 1) {
        close(sfd);
        free(srv);
        return NULL;
    }

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(sfd, 16) != 0) {
        close(sfd);
        free(srv);
        return NULL;
    }

    socklen_t alen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr *)&addr, &alen) == 0)
        srv->port = (int)ntohs(addr.sin_port);
    else
        srv->port = port;

    srv->sfd = sfd;
    if (out_port)
        *out_port = srv->port;
    return srv;
}

int at_http_server_port(const struct at_http_server *srv)
{
    return srv ? srv->port : -1;
}

int at_http_server_run(struct at_http_server *srv)
{
    if (!srv || srv->sfd < 0)
        return -1;

    while (!srv->stop) {
        int cfd = accept(srv->sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (srv->stop) {
            close(cfd);
            break;
        }
        handle_client(cfd, srv->root, srv->api, srv->ud);
    }
    return 0;
}

static void *http_thread_main(void *arg)
{
    at_http_server_run((struct at_http_server *)arg);
    return NULL;
}

int at_http_server_start(struct at_http_server *srv)
{
    if (!srv || srv->sfd < 0)
        return -1;
    if (srv->threaded)
        return 0;
    if (pthread_create(&srv->thr, NULL, http_thread_main, srv) != 0)
        return -1;
    srv->threaded = 1;
    return 0;
}

void at_http_server_stop(struct at_http_server *srv)
{
    if (!srv)
        return;
    srv->stop = 1;
    if (srv->sfd >= 0) {
        shutdown(srv->sfd, SHUT_RDWR);
        close(srv->sfd);
        srv->sfd = -1;
    }
    if (srv->threaded) {
        pthread_join(srv->thr, NULL);
        srv->threaded = 0;
    }
}

void at_http_server_destroy(struct at_http_server *srv)
{
    if (!srv)
        return;
    at_http_server_stop(srv);
    free(srv);
}

int at_http_serve(const char *bind_host, int port, const char *root_dir,
                  at_http_api_fn api, void *ud)
{
    int bound = 0;
    struct at_http_server *srv =
        at_http_server_create(bind_host, port, root_dir, api, ud, &bound);
    if (!srv)
        return -1;

    fprintf(stderr, "CPS UI listening on http://%s:%d/\n",
            bind_host && bind_host[0] ? bind_host : "127.0.0.1", bound);

    int rc = at_http_server_run(srv);
    at_http_server_destroy(srv);
    return rc;
}
