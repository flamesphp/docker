/*
 * Flames Ready - PHP Extension
 *
 * Enables FrankenPHP-style persistent worker behaviour inside a
 * standard Apache + PHP-FPM (or mod_php) stack:
 *
 *   - xflames_ready_register_load('Class', 'method')
 *       Registers a static method that is called ONCE when the worker
 *       process becomes active (bootstraps the application).
 *
 *   - xflames_ready_register_reset('Class', 'method')
 *       Registers a static method that is called AFTER every handled
 *       request to wipe per-request state (globals, caches, etc.).
 *
 *   - xflames_ready_handle_request(callable $handler): int
 *       Enters the worker loop: invokes load callbacks once, then loops
 *       calling $handler() and reset callbacks until $handler returns
 *       false or max_requests is reached.  Returns total requests handled.
 *
 * Lifecycle integration with Apache / PHP-FPM:
 *   RINIT  – load callbacks are invoked automatically on the first
 *            request of each worker process (when preload_once = On).
 *   RSHUTDOWN – reset callbacks are invoked automatically after every
 *               request in non-worker-mode.
 *
 * INI settings:
 *   flames_ready.worker_mode  = 0|1  (default 0)
 *   flames_ready.preload_once = 0|1  (default 1)
 *   flames_ready.max_requests = N    (default 0 = unlimited)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "php_flames_ready.h"
#include "SAPI.h"
#include "zend_smart_str.h"

/* =========================================================================
 * Module globals
 * ========================================================================= */

ZEND_DECLARE_MODULE_GLOBALS(flames_ready)

/* =========================================================================
 * INI entries
 * ========================================================================= */

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY(
        "flames_ready.worker_mode",  "0",
        PHP_INI_SYSTEM, OnUpdateBool,
        worker_mode, zend_flames_ready_globals, flames_ready_globals)
    STD_PHP_INI_ENTRY(
        "flames_ready.preload_once", "1",
        PHP_INI_SYSTEM, OnUpdateBool,
        preload_once, zend_flames_ready_globals, flames_ready_globals)
    STD_PHP_INI_ENTRY(
        "flames_ready.max_requests", "0",
        PHP_INI_SYSTEM, OnUpdateLong,
        max_requests, zend_flames_ready_globals, flames_ready_globals)
    STD_PHP_INI_ENTRY(
        "flames_ready.socket", "/var/run/flames-ready/worker.sock",
        PHP_INI_SYSTEM, OnUpdateString,
        socket_path, zend_flames_ready_globals, flames_ready_globals)
    STD_PHP_INI_ENTRY(
        "flames_ready.workers", "0",
        PHP_INI_SYSTEM, OnUpdateLong,
        workers, zend_flames_ready_globals, flames_ready_globals)
PHP_INI_END()

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* Free all entries in a persistent callback array. */
static void flames_ready_free_callbacks(
    flames_ready_callback **cbs,
    int *count,
    int *cap)
{
    int i;
    for (i = 0; i < *count; i++) {
        if ((*cbs)[i].class_name)  pefree((*cbs)[i].class_name,  1);
        if ((*cbs)[i].method_name) pefree((*cbs)[i].method_name, 1);
    }
    if (*cbs) pefree(*cbs, 1);
    *cbs   = NULL;
    *count = 0;
    *cap   = 0;
}

/* Append a new callback to a persistent callback array. */
static void flames_ready_push_callback(
    flames_ready_callback **cbs,
    int *count,
    int *cap,
    const char *class_name,  size_t class_len,
    const char *method_name, size_t method_len)
{
    if (*count >= *cap) {
        int new_cap = (*cap == 0) ? 8 : (*cap * 2);
        *cbs = perealloc(*cbs, sizeof(flames_ready_callback) * new_cap, 1);
        *cap = new_cap;
    }
    (*cbs)[*count].class_name  = pestrndup(class_name,  class_len,  1);
    (*cbs)[*count].class_len   = class_len;
    (*cbs)[*count].method_name = pestrndup(method_name, method_len, 1);
    (*cbs)[*count].method_len  = method_len;
    (*count)++;
}

/*
 * Invoke every callback in the array as a static method call
 * (equivalent to ClassName::methodName()).
 *
 * Returns SUCCESS or FAILURE on the first failing call.
 */
static int flames_ready_invoke_callbacks(
    flames_ready_callback *cbs,
    int count,
    const char *type)
{
    int   i;
    zval  retval, callable, class_zv, method_zv;

    for (i = 0; i < count; i++) {
        array_init(&callable);
        ZVAL_STRINGL(&class_zv,  cbs[i].class_name,  cbs[i].class_len);
        ZVAL_STRINGL(&method_zv, cbs[i].method_name, cbs[i].method_len);
        add_next_index_zval(&callable, &class_zv);
        add_next_index_zval(&callable, &method_zv);

        if (call_user_function(NULL, NULL, &callable, &retval, 0, NULL)
                == FAILURE) {
            php_error_docref(NULL, E_WARNING,
                "Flames Ready: failed to invoke %s callback %s::%s",
                type,
                cbs[i].class_name,
                cbs[i].method_name);
            zval_ptr_dtor(&callable);
            return FAILURE;
        }

        zval_ptr_dtor(&retval);
        zval_ptr_dtor(&callable);
    }

    return SUCCESS;
}

/* =========================================================================
 * PHP functions
 * ========================================================================= */

/* {{{ xflames_ready_register_reset(string $class, string $method): bool
 *
 * Register a static method to be called after each handled request.
 * The method should release per-request state (clear caches, reset
 * static properties, etc.) so the next request starts clean.
 *
 * Can be called multiple times to register several reset methods.
 * Registration persists for the entire lifetime of the worker process.
 */
PHP_FUNCTION(xflames_ready_register_reset)
{
    char   *class_name,  *method_name;
    size_t  class_len,    method_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(class_name, class_len)
        Z_PARAM_STRING(method_name, method_len)
    ZEND_PARSE_PARAMETERS_END();

    flames_ready_push_callback(
        &FLAMES_READY_G(reset_callbacks),
        &FLAMES_READY_G(reset_count),
        &FLAMES_READY_G(reset_cap),
        class_name,  class_len,
        method_name, method_len);

    RETURN_TRUE;
}
/* }}} */

/* {{{ xflames_ready_register_load(string $class, string $method): bool
 *
 * Register a static method to be called once when the worker is ready
 * to serve its first request.  Use this to bootstrap the application:
 * load configuration, instantiate shared services, warm up caches, etc.
 *
 * In non-worker-mode the callback is invoked immediately upon registration
 * on the first request (the PHP script is already running at that point,
 * so all classes are defined).  On subsequent requests is_ready() returns
 * true and the caller should skip calling this function entirely.
 *
 * Can be called multiple times; methods are invoked in registration order.
 */
PHP_FUNCTION(xflames_ready_register_load)
{
    char   *class_name,  *method_name;
    size_t  class_len,    method_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(class_name, class_len)
        Z_PARAM_STRING(method_name, method_len)
    ZEND_PARSE_PARAMETERS_END();

    flames_ready_push_callback(
        &FLAMES_READY_G(load_callbacks),
        &FLAMES_READY_G(load_count),
        &FLAMES_READY_G(load_cap),
        class_name,  class_len,
        method_name, method_len);

    /*
     * In non-worker-mode, invoke this callback immediately.
     * RINIT fires before the PHP script runs (classes not yet defined),
     * so we invoke here instead – the script is already executing and
     * all class definitions are available.
     */
    if (!FLAMES_READY_G(worker_mode) && FLAMES_READY_G(preload_once)) {
        int idx = FLAMES_READY_G(load_count) - 1;
        flames_ready_invoke_callbacks(
            &FLAMES_READY_G(load_callbacks)[idx],
            1,
            "load");
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ xflames_ready_handle_request(callable $handler): int
 *
 * Persistent FastCGI worker loop (FrankenPHP-style).
 *
 * Binds a FastCGI socket (flames_ready.socket INI), accepts connections
 * from Apache (mod_proxy_fcgi), and for each request:
 *   1. Populates $_SERVER / $_GET / $_POST / $_COOKIE.
 *   2. Calls $handler().
 *   3. Sends the captured output as an HTTP response.
 *   4. Calls reset callbacks.
 *
 * Because this is a PHP-CLI process that never restarts, static class
 * properties and any PHP-level state persist across requests — exactly
 * like FrankenPHP worker mode.
 *
 * Returns the total number of requests handled when the loop ends.
 */
/* Worker accept loop – runs inside each forked child process.
 * Never returns under normal operation; exits via _exit() when done. */
static void flames_ready_worker_loop(int server_fd, zval *handler)
{
    zend_long max     = FLAMES_READY_G(max_requests);
    zend_long handled = 0;

    /* Each child invokes its own load callbacks once. */
    if (!FLAMES_READY_G(initialized)) {
        flames_ready_invoke_callbacks(
            FLAMES_READY_G(load_callbacks),
            FLAMES_READY_G(load_count),
            "load");
        FLAMES_READY_G(initialized) = 1;
    }

    while (1) {
        if (max > 0 && handled >= max) break;

        int conn_fd = flames_ready_fcgi_accept(server_fd);
        if (conn_fd < 0) { usleep(10000); continue; }

        flames_ready_fcgi_request_t req;
        if (flames_ready_fcgi_read_request(conn_fd, &req) < 0) {
            close(conn_fd); continue;
        }

        flames_ready_fcgi_populate_globals(&req);
        php_output_start_user(NULL, 0, PHP_OUTPUT_HANDLER_STDFLAGS);

        zval retval;
        ZVAL_UNDEF(&retval);
        call_user_function(NULL, NULL, handler, &retval, 0, NULL);
        zval_ptr_dtor(&retval);

        if (EG(exception)) zend_clear_exception();

        zval ob_content;
        ZVAL_UNDEF(&ob_content);
        php_output_get_contents(&ob_content);
        php_output_discard();

        const char *body     = "";
        size_t      body_len = 0;
        if (Z_TYPE(ob_content) == IS_STRING) {
            body     = Z_STRVAL(ob_content);
            body_len = Z_STRLEN(ob_content);
        }

        smart_str headers_buf = {0};
        int status_code = SG(sapi_headers).http_response_code;
        if (status_code == 0) status_code = 200;
        smart_str_append_printf(&headers_buf, "Status: %d\r\n", status_code);

        zend_bool has_ct = 0;
        zend_llist_element *el;
        for (el = SG(sapi_headers).headers.head; el; el = el->next) {
            sapi_header_struct *sh = (sapi_header_struct *)el->data;
            smart_str_appendl(&headers_buf, sh->header, sh->header_len);
            smart_str_appendl(&headers_buf, "\r\n", 2);
            if (strncasecmp(sh->header, "Content-Type", 12) == 0) has_ct = 1;
        }
        if (!has_ct)
            smart_str_appends(&headers_buf, "Content-Type: text/html; charset=UTF-8\r\n");
        smart_str_append_printf(&headers_buf, "Content-Length: %zu\r\n", body_len);
        smart_str_0(&headers_buf);

        flames_ready_fcgi_send_response(
            conn_fd, req.request_id,
            ZSTR_VAL(headers_buf.s), ZSTR_LEN(headers_buf.s),
            body, body_len);

        smart_str_free(&headers_buf);
        zval_ptr_dtor(&ob_content);
        flames_ready_fcgi_request_free(&req);
        close(conn_fd);

        sapi_header_op(SAPI_HEADER_DELETE_ALL, NULL);
        SG(sapi_headers).http_response_code = 0;

        flames_ready_invoke_callbacks(
            FLAMES_READY_G(reset_callbacks),
            FLAMES_READY_G(reset_count),
            "reset");

        handled++;
        FLAMES_READY_G(request_count)++;
    }
}

/* Fork one worker child; returns the child PID (in parent) or does not
 * return at all (child runs the accept loop and exits). */
static pid_t flames_ready_spawn_worker(int server_fd, zval *handler)
{
    pid_t pid = fork();
    if (pid < 0) return -1; /* fork failed */

    if (pid == 0) {
        /* ── Child process ─────────────────────────────────────────── */
        flames_ready_worker_loop(server_fd, handler);
        _exit(0);
    }

    /* Parent: return child PID */
    return pid;
}

PHP_FUNCTION(xflames_ready_handle_request)
{
    zval *handler;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(handler)
    ZEND_PARSE_PARAMETERS_END();

    if (!zend_is_callable(handler, 0, NULL)) {
        zend_throw_exception_ex(zend_ce_type_error, 0,
            "Flames Ready: argument must be callable");
        RETURN_FALSE;
    }

    /* ── Open the shared FastCGI socket (parent only, before forking) ── */
    const char *sock_path = FLAMES_READY_G(socket_path);
    if (!sock_path || sock_path[0] == '\0')
        sock_path = "/var/run/flames-ready/worker.sock";

    int server_fd = -1;
    int bind_tries = 30;
    while (bind_tries-- > 0) {
        server_fd = flames_ready_fcgi_open_socket(sock_path);
        if (server_fd >= 0) break;
        php_error_docref(NULL, E_WARNING,
            "Flames Ready: socket bind failed (%s), retrying in 1s...",
            strerror(errno));
        sleep(1);
    }
    if (server_fd < 0) {
        php_error_docref(NULL, E_ERROR,
            "Flames Ready: cannot bind FastCGI socket '%s' after retries: %s",
            sock_path, strerror(errno));
        RETURN_FALSE;
    }

    /* ── Determine worker count ─────────────────────────────────────── */
    int num_workers = (int)FLAMES_READY_G(workers);
    if (num_workers <= 0) {
        /* 0 = auto: one worker per logical CPU */
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        num_workers = (cpus > 0) ? (int)cpus : 4;
    }

    /* ── Fork all workers ───────────────────────────────────────────── */
    pid_t *pids = emalloc((size_t)num_workers * sizeof(pid_t));
    for (int i = 0; i < num_workers; i++) {
        pids[i] = flames_ready_spawn_worker(server_fd, handler);
        if (pids[i] < 0) {
            php_error_docref(NULL, E_WARNING,
                "Flames Ready: fork() failed for worker %d: %s",
                i, strerror(errno));
        }
    }

    fprintf(stderr,
        "[Flames Ready] supervisor pid %d – spawned %d worker(s) on '%s'\n",
        (int)getpid(), num_workers, sock_path);
    fflush(stderr);

    /* ── Supervisor loop: monitor and respawn dead workers ──────────── */
    while (1) {
        int   status;
        pid_t died = waitpid(-1, &status, 0);
        if (died < 0) {
            if (errno == EINTR) continue;
            break; /* no more children */
        }

        /* Find the slot and replace the dead worker */
        for (int i = 0; i < num_workers; i++) {
            if (pids[i] == died) {
                fprintf(stderr,
                    "[Flames Ready] worker pid %d exited – respawning...\n",
                    (int)died);
                fflush(stderr);
                pids[i] = flames_ready_spawn_worker(server_fd, handler);
                break;
            }
        }
    }

    close(server_fd);
    efree(pids);
    RETURN_LONG(0);
}
/* }}} */

/* {{{ xflames_ready_is_ready(): bool
 * Returns true if load callbacks have already been invoked for this worker.
 */
PHP_FUNCTION(xflames_ready_is_ready)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(FLAMES_READY_G(initialized));
}
/* }}} */

/* {{{ xflames_ready_get_request_count(): int
 * Returns the total number of requests handled by this worker process.
 */
PHP_FUNCTION(xflames_ready_get_request_count)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_LONG(FLAMES_READY_G(request_count));
}
/* }}} */

/* =========================================================================
 * Module lifecycle
 * ========================================================================= */

/* {{{ PHP_GINIT_FUNCTION – initialise globals to safe zero/null values */
PHP_GINIT_FUNCTION(flames_ready)
{
#if defined(COMPILE_DL_FLAMES_READY) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    flames_ready_globals->reset_callbacks = NULL;
    flames_ready_globals->reset_count     = 0;
    flames_ready_globals->reset_cap       = 0;
    flames_ready_globals->load_callbacks  = NULL;
    flames_ready_globals->load_count      = 0;
    flames_ready_globals->load_cap        = 0;
    flames_ready_globals->initialized     = 0;
    flames_ready_globals->worker_mode     = 0;
    flames_ready_globals->preload_once    = 1;
    flames_ready_globals->max_requests    = 0;
    flames_ready_globals->request_count   = 0;
    flames_ready_globals->socket_path     = NULL;
    flames_ready_globals->workers         = 0;
}
/* }}} */

/* {{{ PHP_GSHUTDOWN_FUNCTION – free persistent callback arrays */
PHP_GSHUTDOWN_FUNCTION(flames_ready)
{
    flames_ready_free_callbacks(
        &flames_ready_globals->reset_callbacks,
        &flames_ready_globals->reset_count,
        &flames_ready_globals->reset_cap);
    flames_ready_free_callbacks(
        &flames_ready_globals->load_callbacks,
        &flames_ready_globals->load_count,
        &flames_ready_globals->load_cap);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(flames_ready)
{
    REGISTER_INI_ENTRIES();
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(flames_ready)
{
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}
/* }}} */

/*
 * {{{ PHP_RINIT_FUNCTION
 *
 * Called at the start of every request.
 * In non-worker-mode (standard Apache mod_php or PHP-FPM), automatically
 * invoke load callbacks the first time this worker handles a request.
 */
PHP_RINIT_FUNCTION(flames_ready)
{
#if defined(COMPILE_DL_FLAMES_READY) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    /*
     * Nothing to do here. In non-worker-mode, load callbacks are invoked
     * directly inside xflames_ready_register_load() – after the PHP script
     * has started executing and all class definitions are available.
     */
    return SUCCESS;
}
/* }}} */

/*
 * {{{ PHP_RSHUTDOWN_FUNCTION
 *
 * Called at the end of every request.
 * In non-worker-mode, automatically invoke reset callbacks so the next
 * request starts with a clean per-request state.
 */
PHP_RSHUTDOWN_FUNCTION(flames_ready)
{
    if (!FLAMES_READY_G(worker_mode)) {
        flames_ready_invoke_callbacks(
            FLAMES_READY_G(reset_callbacks),
            FLAMES_READY_G(reset_count),
            "reset");
        FLAMES_READY_G(request_count)++;
        /*
         * Mark as initialized after the first complete request.
         * From the next request on, is_ready() returns true and the
         * caller should skip re-registering callbacks.
         */
        FLAMES_READY_G(initialized) = 1;
    }

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(flames_ready)
{
    char buf[32];

    php_info_print_table_start();
    php_info_print_table_header(2, "Flames Ready", "enabled");
    php_info_print_table_row(2, "Version",     PHP_FLAMES_READY_VERSION);
    php_info_print_table_row(2, "Worker Mode",
        FLAMES_READY_G(worker_mode) ? "On" : "Off");

    if (FLAMES_READY_G(max_requests) == 0) {
        php_info_print_table_row(2, "Max Requests", "unlimited");
    } else {
        snprintf(buf, sizeof(buf), ZEND_LONG_FMT,
            FLAMES_READY_G(max_requests));
        php_info_print_table_row(2, "Max Requests", buf);
    }

    snprintf(buf, sizeof(buf), ZEND_LONG_FMT,
        FLAMES_READY_G(request_count));
    php_info_print_table_row(2, "Requests Handled", buf);
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}
/* }}} */

/* =========================================================================
 * Function table & module entry
 * ========================================================================= */

static const zend_function_entry flames_ready_functions[] = {
    PHP_FE(xflames_ready_register_reset,
        arginfo_xflames_ready_register_reset)
    PHP_FE(xflames_ready_register_load,
        arginfo_xflames_ready_register_load)
    PHP_FE(xflames_ready_handle_request,
        arginfo_xflames_ready_handle_request)
    PHP_FE(xflames_ready_is_ready,
        arginfo_xflames_ready_is_ready)
    PHP_FE(xflames_ready_get_request_count,
        arginfo_xflames_ready_get_request_count)
    PHP_FE_END
};

zend_module_entry flames_ready_module_entry = {
    STANDARD_MODULE_HEADER_EX,
    NULL,
    NULL,                          /* deps     */
    PHP_FLAMES_READY_EXTNAME,
    flames_ready_functions,
    PHP_MINIT(flames_ready),
    PHP_MSHUTDOWN(flames_ready),
    PHP_RINIT(flames_ready),
    PHP_RSHUTDOWN(flames_ready),
    PHP_MINFO(flames_ready),
    PHP_FLAMES_READY_VERSION,
    PHP_MODULE_GLOBALS(flames_ready),
    PHP_GINIT(flames_ready),
    PHP_GSHUTDOWN(flames_ready),
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_FLAMES_READY
#   ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#   endif
ZEND_GET_MODULE(flames_ready)
#endif
