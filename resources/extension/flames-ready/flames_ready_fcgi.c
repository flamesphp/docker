/*
 * flames_ready_fcgi.c
 *
 * Minimal FastCGI server used by xflames_ready_handle_request().
 *
 * Apache talks to this worker via mod_proxy_fcgi → Unix/TCP socket.
 * Because the PHP-CLI process never restarts, static class properties
 * and any PHP-level state survive across requests — exactly like
 * FrankenPHP's worker mode.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "php.h"
#include "SAPI.h"
#include "php_variables.h"

#include "flames_ready_fcgi.h"

/* =========================================================================
 * Internal FastCGI record layout
 * ========================================================================= */

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  requestIdB1;
    uint8_t  requestIdB0;
    uint8_t  contentLengthB1;
    uint8_t  contentLengthB0;
    uint8_t  paddingLength;
    uint8_t  reserved;
} fcgi_header_t;

/* =========================================================================
 * Low-level I/O helpers
 * ========================================================================= */

static int fcgi_read_exact(int fd, void *buf, size_t n)
{
    size_t  done = 0;
    ssize_t r;
    while (done < n) {
        r = read(fd, (char *)buf + done, n - done);
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

static int fcgi_write_exact(int fd, const void *buf, size_t n)
{
    size_t  done = 0;
    ssize_t w;
    while (done < n) {
        w = write(fd, (const char *)buf + done, n - done);
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return 0;
}

/* =========================================================================
 * FastCGI name-value pair parser
 * ========================================================================= */

/*
 * Decode a FastCGI length field (1 or 4 bytes).
 * Advances *pos by the number of bytes consumed.
 * Returns -1 if the buffer is too short.
 */
static int fcgi_decode_len(
    const uint8_t *buf, size_t buf_len,
    size_t *pos, uint32_t *out)
{
    if (*pos >= buf_len) return -1;
    if ((buf[*pos] >> 7) == 0) {
        *out = buf[(*pos)++];
    } else {
        if (*pos + 4 > buf_len) return -1;
        *out = ((uint32_t)(buf[*pos]   & 0x7f) << 24)
             | ((uint32_t) buf[*pos+1]         << 16)
             | ((uint32_t) buf[*pos+2]         <<  8)
             |  (uint32_t) buf[*pos+3];
        *pos += 4;
    }
    return 0;
}

/*
 * Parse all name-value pairs from a FCGI_PARAMS content buffer
 * and insert them into the HashTable *ht.
 */
static void fcgi_parse_params(
    const uint8_t *buf, size_t len, HashTable *ht)
{
    size_t   pos = 0;
    uint32_t name_len, value_len;

    while (pos < len) {
        if (fcgi_decode_len(buf, len, &pos, &name_len)  < 0) break;
        if (fcgi_decode_len(buf, len, &pos, &value_len) < 0) break;
        if (pos + name_len + value_len > len)               break;

        const char *name  = (const char *)buf + pos;
        pos += name_len;
        const char *value = (const char *)buf + pos;
        pos += value_len;

        zval zv;
        ZVAL_STRINGL(&zv, value, value_len);
        zend_hash_str_update(ht, name, name_len, &zv);
    }
}

/* =========================================================================
 * Socket management
 * ========================================================================= */

int flames_ready_fcgi_open_socket(const char *path)
{
    int fd;

    /* Numeric string → TCP socket on 0.0.0.0:port */
    if (path[0] >= '0' && path[0] <= '9') {
        int port = atoi(path);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons((uint16_t)port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd); return -1;
        }
    } else {
        /* Unix socket */
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        unlink(path); /* remove stale socket */

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd); return -1;
        }

        /* Allow Apache (running as a different user) to connect */
        chmod(path, 0777);
    }

    if (listen(fd, SOMAXCONN) < 0) {
        close(fd); return -1;
    }

    return fd;
}

int flames_ready_fcgi_accept(int server_fd)
{
    int conn;
    do {
        conn = accept(server_fd, NULL, NULL);
    } while (conn < 0 && errno == EINTR);
    return conn;
}

/* =========================================================================
 * Request reading
 * ========================================================================= */

int flames_ready_fcgi_read_request(int conn_fd, flames_ready_fcgi_request_t *req)
{
    fcgi_header_t hdr;
    int           done_params = 0, done_stdin = 0;

    req->request_id = 0;
    req->body       = NULL;
    req->body_len   = 0;
    array_init(&req->params);

    while (!done_params || !done_stdin) {
        if (fcgi_read_exact(conn_fd, &hdr, sizeof(hdr)) < 0) return -1;

        uint16_t content_len = ((uint16_t)hdr.contentLengthB1 << 8)
                             |  (uint16_t)hdr.contentLengthB0;
        uint16_t request_id  = ((uint16_t)hdr.requestIdB1 << 8)
                             |  (uint16_t)hdr.requestIdB0;
        uint8_t  padding     = hdr.paddingLength;

        if (req->request_id == 0) req->request_id = request_id;

        uint8_t *content = NULL;
        if (content_len > 0) {
            content = emalloc(content_len);
            if (fcgi_read_exact(conn_fd, content, content_len) < 0) {
                efree(content);
                return -1;
            }
        }
        if (padding > 0) {
            uint8_t pad[255];
            fcgi_read_exact(conn_fd, pad, padding);
        }

        switch (hdr.type) {
            case FCGI_BEGIN_REQUEST:
                /* role/flags – nothing to store */
                if (content) efree(content);
                break;

            case FCGI_PARAMS:
                if (content_len == 0) {
                    done_params = 1;
                } else {
                    fcgi_parse_params(content, content_len,
                                      Z_ARRVAL(req->params));
                    efree(content);
                }
                break;

            case FCGI_STDIN:
                if (content_len == 0) {
                    done_stdin = 1;
                } else {
                    req->body = erealloc(req->body,
                                         req->body_len + content_len + 1);
                    memcpy(req->body + req->body_len, content, content_len);
                    req->body_len += content_len;
                    req->body[req->body_len] = '\0';
                    efree(content);
                }
                break;

            default:
                if (content) efree(content);
                break;
        }
    }

    return 0;
}

/* =========================================================================
 * Minimal URL-decode + query-string parser
 * (avoids dependency on ext/standard headers unavailable at build time)
 * ========================================================================= */

static char *fr_url_decode(const char *str, size_t len, size_t *out_len)
{
    char  *result = emalloc(len + 1);
    size_t j = 0, i;

    for (i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            char hi = str[i + 1], lo = str[i + 2];
            int  hi_v = (hi >= '0' && hi <= '9') ? hi - '0'
                      : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                      : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
            int  lo_v = (lo >= '0' && lo <= '9') ? lo - '0'
                      : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                      : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
            if (hi_v >= 0 && lo_v >= 0) {
                result[j++] = (char)((hi_v << 4) | lo_v);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            result[j++] = ' ';
            continue;
        }
        result[j++] = str[i];
    }
    result[j] = '\0';
    *out_len = j;
    return result;
}

static void fr_parse_query(const char *qs, size_t qs_len, zval *arr)
{
    const char *pos = qs;
    const char *end = qs + qs_len;

    while (pos < end) {
        const char *amp = (const char *)memchr(pos, '&', (size_t)(end - pos));
        if (!amp) amp = end;

        const char *eq = (const char *)memchr(pos, '=', (size_t)(amp - pos));

        size_t key_len, val_len;
        char  *key, *val;

        if (eq) {
            key = fr_url_decode(pos,      (size_t)(eq - pos),      &key_len);
            val = fr_url_decode(eq + 1,   (size_t)(amp - eq - 1),  &val_len);
        } else {
            key = fr_url_decode(pos,      (size_t)(amp - pos),     &key_len);
            val = estrndup("", 0);
            val_len = 0;
        }

        if (key_len > 0) {
            zval zv;
            ZVAL_STRINGL(&zv, val, val_len);
            zend_hash_str_update(Z_ARRVAL_P(arr), key, key_len, &zv);
        }

        efree(key);
        efree(val);
        pos = amp + 1;
    }
}

/* =========================================================================
 * PHP superglobal population
 * ========================================================================= */

static void fr_set_global_from_string(int global_id,
                                       const char *key, size_t key_len,
                                       const char *val, size_t val_len)
{
    zval *global = &PG(http_globals)[global_id];
    zval  zv;
    ZVAL_STRINGL(&zv, val, val_len);
    zend_hash_str_update(Z_ARRVAL_P(global), key, key_len, &zv);
}

void flames_ready_fcgi_populate_globals(flames_ready_fcgi_request_t *req)
{
    /* Build fresh arrays and inject via EG(symbol_table).
     * We avoid PG(http_globals) entirely: in PHP-CLI those slots are
     * not guaranteed to be initialised as arrays, causing segfaults. */
    zval sv, gv, pv, cv, rv;
    array_init(&sv);
    array_init(&gv);
    array_init(&pv);
    array_init(&cv);
    array_init(&rv);

    /* ── $_SERVER: all FastCGI params ───────────────────────────────── */
    zend_string *k;
    zval        *v;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(req->params), k, v) {
        if (k && Z_TYPE_P(v) == IS_STRING) {
            zval copy;
            ZVAL_STR_COPY(&copy, Z_STR_P(v));
            zend_hash_update(Z_ARRVAL(sv), k, &copy);
        }
    } ZEND_HASH_FOREACH_END();

    /* ── $_GET ──────────────────────────────────────────────────────── */
    zval *qs_zv = zend_hash_str_find(Z_ARRVAL(req->params),
                                      "QUERY_STRING", sizeof("QUERY_STRING") - 1);
    if (qs_zv && Z_TYPE_P(qs_zv) == IS_STRING && Z_STRLEN_P(qs_zv) > 0) {
        fr_parse_query(Z_STRVAL_P(qs_zv), Z_STRLEN_P(qs_zv), &gv);
    }

    /* ── $_POST ─────────────────────────────────────────────────────── */
    zval *ct_zv = zend_hash_str_find(Z_ARRVAL(req->params),
                                      "CONTENT_TYPE", sizeof("CONTENT_TYPE") - 1);
    if (req->body_len > 0 && ct_zv && Z_TYPE_P(ct_zv) == IS_STRING &&
        strncasecmp(Z_STRVAL_P(ct_zv),
                    "application/x-www-form-urlencoded",
                    sizeof("application/x-www-form-urlencoded") - 1) == 0) {
        fr_parse_query(req->body, req->body_len, &pv);
    }

    /* ── $_COOKIE ───────────────────────────────────────────────────── */
    zval *hc_zv = zend_hash_str_find(Z_ARRVAL(req->params),
                                      "HTTP_COOKIE", sizeof("HTTP_COOKIE") - 1);
    if (hc_zv && Z_TYPE_P(hc_zv) == IS_STRING && Z_STRLEN_P(hc_zv) > 0) {
        size_t cl = Z_STRLEN_P(hc_zv);
        char  *cs = estrndup(Z_STRVAL_P(hc_zv), cl);
        char  *p  = cs;
        while ((p = strstr(p, "; ")) != NULL) { p[0] = '&'; p[1] = ' '; }
        fr_parse_query(cs, cl, &cv);
        efree(cs);
    }

    /* ── $_REQUEST = COOKIE + POST + GET ────────────────────────────── */
    zend_hash_merge(Z_ARRVAL(rv), Z_ARRVAL(cv), zval_add_ref, 0);
    zend_hash_merge(Z_ARRVAL(rv), Z_ARRVAL(pv), zval_add_ref, 0);
    zend_hash_merge(Z_ARRVAL(rv), Z_ARRVAL(gv), zval_add_ref, 0);

    /* ── Inject into global symbol table ────────────────────────────── */
    zend_hash_str_update(&EG(symbol_table), "_SERVER",  sizeof("_SERVER")  - 1, &sv);
    zend_hash_str_update(&EG(symbol_table), "_GET",     sizeof("_GET")     - 1, &gv);
    zend_hash_str_update(&EG(symbol_table), "_POST",    sizeof("_POST")    - 1, &pv);
    zend_hash_str_update(&EG(symbol_table), "_COOKIE",  sizeof("_COOKIE")  - 1, &cv);
    zend_hash_str_update(&EG(symbol_table), "_REQUEST", sizeof("_REQUEST") - 1, &rv);

    /* ── SG(request_info) ───────────────────────────────────────────── */
    SG(request_info).query_string   = qs_zv ? Z_STRVAL_P(qs_zv) : "";
    SG(request_info).content_length = (zend_long)req->body_len;

    zval *rm_zv = zend_hash_str_find(Z_ARRVAL(req->params),
                                      "REQUEST_METHOD", sizeof("REQUEST_METHOD") - 1);
    SG(request_info).request_method = rm_zv ? Z_STRVAL_P(rm_zv) : "GET";

    zval *ru_zv = zend_hash_str_find(Z_ARRVAL(req->params),
                                      "REQUEST_URI", sizeof("REQUEST_URI") - 1);
    SG(request_info).request_uri = ru_zv ? Z_STRVAL_P(ru_zv) : "/";
}

/* =========================================================================
 * Response sending
 * ========================================================================= */

static int fcgi_write_record(int fd, uint8_t type, uint16_t req_id,
                              const char *data, uint16_t len)
{
    fcgi_header_t hdr;
    hdr.version         = FCGI_VERSION_1;
    hdr.type            = type;
    hdr.requestIdB1     = (req_id >> 8) & 0xff;
    hdr.requestIdB0     =  req_id       & 0xff;
    hdr.contentLengthB1 = (len   >> 8) & 0xff;
    hdr.contentLengthB0 =  len         & 0xff;
    hdr.paddingLength   = 0;
    hdr.reserved        = 0;

    if (fcgi_write_exact(fd, &hdr, sizeof(hdr)) < 0) return -1;
    if (len > 0 && fcgi_write_exact(fd, data, len) < 0) return -1;
    return 0;
}

int flames_ready_fcgi_send_response(
    int conn_fd, uint16_t request_id,
    const char *headers, size_t headers_len,
    const char *body,    size_t body_len)
{
    /* Build full CGI response: headers already end with \r\n, add one more \r\n
     * as the blank line separator, then body.
     * (headers + "\r\n" + body  →  "Header: v\r\n...\r\n" + "\r\n" + body) */
    size_t total_len = headers_len + 2 + body_len;
    char  *full      = emalloc(total_len + 1);
    memcpy(full, headers, headers_len);
    memcpy(full + headers_len, "\r\n", 2);
    if (body_len > 0) memcpy(full + headers_len + 4, body, body_len);

    /* Send in 65535-byte FastCGI STDOUT chunks */
    size_t sent = 0;
    int    rc   = 0;
    while (sent < total_len) {
        size_t   chunk = total_len - sent;
        if (chunk > 65535) chunk = 65535;
        if (fcgi_write_record(conn_fd, FCGI_STDOUT, request_id,
                              full + sent, (uint16_t)chunk) < 0) {
            rc = -1; break;
        }
        sent += chunk;
    }
    efree(full);

    /* Empty STDOUT → signals end of response body */
    fcgi_write_record(conn_fd, FCGI_STDOUT, request_id, NULL, 0);

    /* END_REQUEST */
    uint8_t end_body[8] = {0, 0, 0, rc == 0 ? 0 : 1,
                           FCGI_REQUEST_COMPLETE, 0, 0, 0};
    fcgi_write_record(conn_fd, FCGI_END_REQUEST, request_id,
                      (char *)end_body, sizeof(end_body));
    return rc;
}

/* =========================================================================
 * Cleanup
 * ========================================================================= */

void flames_ready_fcgi_request_free(flames_ready_fcgi_request_t *req)
{
    zval_ptr_dtor(&req->params);
    if (req->body) {
        efree(req->body);
        req->body     = NULL;
        req->body_len = 0;
    }
}
