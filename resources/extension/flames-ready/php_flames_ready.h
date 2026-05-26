/*
 * Flames Ready - PHP Extension
 * Persistent worker mode for PHP + Apache, inspired by FrankenPHP.
 *
 * Keeps PHP workers alive between requests with pre-loaded classes,
 * calling user-registered load/reset callbacks at the right lifecycle points.
 */

#ifndef PHP_FLAMES_READY_H
#define PHP_FLAMES_READY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"
#include "flames_ready_fcgi.h"


extern zend_module_entry flames_ready_module_entry;
#define phpext_flames_ready_ptr &flames_ready_module_entry

#define PHP_FLAMES_READY_VERSION "1.0.0"
#define PHP_FLAMES_READY_EXTNAME "flames_ready"

#ifdef PHP_WIN32
#   define PHP_FLAMES_READY_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_FLAMES_READY_API __attribute__((visibility("default")))
#else
#   define PHP_FLAMES_READY_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* -----------------------------------------------------------------------
 * Callback entry: a class + static method pair stored in persistent memory.
 * ----------------------------------------------------------------------- */
typedef struct _flames_ready_callback {
    char   *class_name;
    size_t  class_len;
    char   *method_name;
    size_t  method_len;
} flames_ready_callback;

/* -----------------------------------------------------------------------
 * Module globals (persist for the entire life of a worker process).
 * ----------------------------------------------------------------------- */
ZEND_BEGIN_MODULE_GLOBALS(flames_ready)
    /* reset callbacks – called after each handled request */
    flames_ready_callback *reset_callbacks;
    int                    reset_count;
    int                    reset_cap;

    /* load callbacks – called once when the worker becomes ready */
    flames_ready_callback *load_callbacks;
    int                    load_count;
    int                    load_cap;

    /* state flags */
    zend_bool  initialized;    /* load callbacks already invoked          */
    zend_bool  worker_mode;    /* INI: flames_ready.worker_mode            */
    zend_bool  preload_once;   /* INI: flames_ready.preload_once (default 1) */
    zend_long  max_requests;   /* INI: flames_ready.max_requests (0 = inf) */
    zend_long  request_count;  /* total requests handled by this worker    */
    char      *socket_path;    /* INI: flames_ready.socket                 */
ZEND_END_MODULE_GLOBALS(flames_ready)

#define FLAMES_READY_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(flames_ready, v)

#if defined(ZTS) && defined(COMPILE_DL_FLAMES_READY)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

/* -----------------------------------------------------------------------
 * Arginfo – PHP 8.x style with full type information.
 * ----------------------------------------------------------------------- */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(
    arginfo_xflames_ready_register_reset, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, class,  IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, method, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(
    arginfo_xflames_ready_register_load, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, class,  IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, method, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(
    arginfo_xflames_ready_handle_request, 0, 1, IS_LONG, 0)
    ZEND_ARG_INFO(0, handler)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(
    arginfo_xflames_ready_is_ready, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(
    arginfo_xflames_ready_get_request_count, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */
PHP_FUNCTION(xflames_ready_register_reset);
PHP_FUNCTION(xflames_ready_register_load);
PHP_FUNCTION(xflames_ready_handle_request);
PHP_FUNCTION(xflames_ready_is_ready);
PHP_FUNCTION(xflames_ready_get_request_count);

#endif /* PHP_FLAMES_READY_H */
