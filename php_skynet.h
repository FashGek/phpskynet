/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2012 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_SKYNET_H
#define PHP_SKYNET_H

#include "cjson/json.h"

extern zend_module_entry skynet_module_entry;
#define phpext_skynet_ptr &skynet_module_entry

#ifdef PHP_WIN32
#	define PHP_SKYNET_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_SKYNET_API __attribute__ ((visibility("default")))
#else
#	define PHP_SKYNET_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_METHOD(Redis, __construct);
PHP_METHOD(Redis, connect);
PHP_METHOD(Redis, rpc);

PHP_MINIT_FUNCTION(skynet);
PHP_MSHUTDOWN_FUNCTION(skynet);
PHP_RINIT_FUNCTION(skynet);
PHP_RSHUTDOWN_FUNCTION(skynet);
PHP_MINFO_FUNCTION(skynet);

PHP_FUNCTION(confirm_skynet_compiled);	/* For testing, remove later. */

/* {{{ struct RedisSock */
typedef struct SkynetSock_ {
    php_stream     *stream;
    char           *host;
    unsigned short port;
    long           timeout;
    int            failed;
    int            status;
} SkynetSock;
/* }}} */

#define skynet_sock_name "Skynet Socket Buffer"

#define SKYNET_SOCK_STATUS_FAILED 0
#define SKYNET_SOCK_STATUS_DISCONNECTED 1
#define SKYNET_SOCK_STATUS_UNKNOWN 2
#define SKYNET_SOCK_STATUS_CONNECTED 3

/* {{{ internal function protos */
PHPAPI RedisSock* skynet_sock_create(char *host, int host_len, unsigned short port, long timeout);
PHPAPI int skynet_sock_connect(RedisSock *redis_sock TSRMLS_DC);
PHPAPI int skynet_sock_disconnect(RedisSock *redis_sock TSRMLS_DC);
PHPAPI int skynet_sock_server_open(RedisSock *redis_sock, int TSRMLS_DC);
PHPAPI char * skynet_sock_read(RedisSock *redis_sock, int *buf_len TSRMLS_DC);
PHPAPI char * skynet_sock_read_bulk_reply(RedisSock *redis_sock, int bytes);
PHPAPI int skynet_sock_read_multibulk_reply(INTERNAL_FUNCTION_PARAMETERS, RedisSock *redis_sock, int *buf_len TSRMLS_DC);
PHPAPI int skynet_sock_write(RedisSock *redis_sock, char *cmd);
PHPAPI void skynet_free_socket(RedisSock *redis_sock);

// PHPAPI void redis_array_to_json(zval *array, struct json_object *jo);
// PHPAPI zval* redis_json_to_array(struct json_object *jo);　
PHPAPI int skynet_array_to_json(zval *z_array, struct json_object *jo);
PHPAPI zval* skynet_json_to_array(struct json_object *jo);

// class API
PHPAPI void add_constant_long(zend_class_entry *ce, char *name, int value);

#define skynet_strnatcmp(a, b) \
	strnatcmp_ex(a, strlen(a), b, strlen(b), 0)
#define skynet_strnatcasecmp(a, b) \
	strnatcmp_ex(a, strlen(a), b, strlen(b), 1)
PHPAPI int skynet_strnatcmp_ex(char const *a, size_t a_len, char const *b, size_t b_len, int fold_case);

/* }}} */

/* 
  	Declare any global variables you may need between the BEGIN
	and END macros here:     

ZEND_BEGIN_MODULE_GLOBALS(skynet)
	long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(skynet)
*/

/* In every utility function you add that needs to use variables 
   in php_skynet_globals, call TSRMLS_FETCH(); after declaring other 
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as SKYNET_G(variable).  You are 
   encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/

#ifdef ZTS
#define SKYNET_G(v) TSRMG(skynet_globals_id, zend_skynet_globals *, v)
#else
#define SKYNET_G(v) (skynet_globals.v)
#endif

#endif	/* PHP_SKYNET_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
