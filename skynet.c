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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_skynet.h"
#include "cjson/json.h"

/* If you declare any globals in php_skynet.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(skynet)
*/

#define METHOD_CALL 1
#define METHOD_CAST 2
#define METHOD_SEND 3

static int le_skynet_sock;
static zend_class_entry *skynet_ce;
static zend_class_entry *skynet_exception_ce;
static zend_class_entry *spl_ce_RuntimeException = NULL;

/* True global resources - no need for thread safety here */
static int le_skynet;

/* {{{ skynet_functions[]
 *
 * Every user visible function must have an entry in skynet_functions[].
 */
const zend_function_entry skynet_functions[] = {
	/*PHP_FE(confirm_skynet_compiled,	NULL)		For testing, remove later. */
	PHP_ME(Skynet, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Skynet, connect, NULL, ZEND_ACC_PUBLIC)
	/*PHP_ME(Skynet, print_array, NULL, ZEND_ACC_PUBLIC)*/
	PHP_ME(Skynet, rpc, NULL, ZEND_ACC_PUBLIC)
	PHP_FE_END	/* Must be the last line in skynet_functions[] */
};
/* }}} */

/* {{{ skynet_module_entry
 */
zend_module_entry skynet_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"skynet",
	skynet_functions,
	PHP_MINIT(skynet),
	PHP_MSHUTDOWN(skynet),
	PHP_RINIT(skynet),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(skynet),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(skynet),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_SKYNET
ZEND_GET_MODULE(skynet)
#endif

/**
 * redis_sock_create
 */
PHPAPI RedisSock* skynet_sock_create(char *host, int host_len, unsigned short port,
                                                                       long timeout)
{
    SkynetSock *skynet_sock;

    skynet_sock         = emalloc(sizeof *skyent_sock);
    skynet_sock->host   = emalloc(host_len + 1);
    skynet_sock->stream = NULL;
    skynet_sock->status = SKYNET_SOCK_STATUS_DISCONNECTED;

    memcpy(skynet_sock->host, host, host_len);
    skynet_sock->host[host_len] = '\0';

    skynet_sock->port    = port;
    skynet_sock->timeout = timeout;

    return skynet_sock;
}

/**
 * redis_sock_connect
 */
PHPAPI int skynet_sock_connect(SkynetSock *skynet_sock TSRMLS_DC)
{
    struct timeval tv;
    char *host = NULL, *hash_key = NULL, *errstr = NULL;
    int host_len, err = 0;

    if (skynet_sock->stream != NULL) {
        skynet_sock_disconnect(skynet_sock TSRMLS_CC);
    }

    tv.tv_sec  = skynet_sock->timeout;
    tv.tv_usec = 0;

    host_len = spprintf(&host, 0, "%s:%d", skynet_sock->host, skynet_sock->port);

    skyent_sock->stream = php_stream_xport_create(host, host_len, ENFORCE_SAFE_MODE,
                                                 STREAM_XPORT_CLIENT
                                                 | STREAM_XPORT_CONNECT,
                                                 hash_key, &tv, NULL, &errstr, &err
                                                );

    efree(host);

    if (!skynet_sock->stream) {
        efree(errstr);
        return -1;
    }

    php_stream_auto_cleanup(skynet_sock->stream);

    php_stream_set_option(skynet_sock->stream, 
                          PHP_STREAM_OPTION_READ_TIMEOUT,
                          0, &tv);
    php_stream_set_option(skynet_sock->stream,
                          PHP_STREAM_OPTION_WRITE_BUFFER,
                          PHP_STREAM_BUFFER_NONE, NULL);

    skynet_sock->status = REDIS_SOCK_STATUS_CONNECTED;

    return 0;
}

/**
 * redis_sock_server_open
 */
PHPAPI int skynet_sock_server_open(SkynetSock *skynet_sock, int force_connect TSRMLS_DC)
{
    int res = -1;

    switch (skynet_sock->status) {
        case SKYNET_SOCK_STATUS_DISCONNECTED:
            return skynet_sock_connect(skynet_sock TSRMLS_CC);
        case REDIS_SOCK_STATUS_CONNECTED:
            res = 0;
        break;
        case REDIS_SOCK_STATUS_UNKNOWN:
            if (force_connect > 0 && redis_sock_connect(skynet_sock TSRMLS_CC) < 0) {
                res = -1;
            } else {
                res = 0;

                skynet_sock->status = REDIS_SOCK_STATUS_CONNECTED;
            }
        break;
    }

    return res;
}

/**
 * redis_sock_disconnect
 */
PHPAPI int skynet_sock_disconnect(SkynetSock *skynet_sock TSRMLS_DC)
{
    int res = 0;

    if (skynet_sock->stream != NULL) {
        skynet_sock_write(skynet_sock, "{'service': 'PHPLOG', 'method': 3, 'handle':'text', 'param': {'0':'QUIT'}}");

        skynet_sock->status = SKYNET_SOCK_STATUS_DISCONNECTED;
        php_stream_close(skynet_sock->stream);
        skynet_sock->stream = NULL;

        res = 1;
    }

    return res;
}

/**
 * redis_sock_read
 */
PHPAPI char *skynet_sock_read(SkynetSock *skynet_sock, int *buf_len TSRMLS_DC)
{
    char inbuf[1024], response[1024], *s;
    int length;

    s = php_stream_gets(skynet_sock->stream, inbuf, 1024);
    s = estrndup(s, (strlen(s)-2));

    switch(s[0]) {
        case '-':
            printf("error");
        break;
        case '+':
        case ':':    
            /* Single Line Reply */
            strcpy(response, s);
        break;
        case '$':
            /* Bulk Reply */
            length = strlen(s) - 1;

            char *bytes = (char*)malloc(sizeof(char) * length);

            strncpy(bytes, s + 1, length);

            strcpy(response, skynet_sock_read_bulk_reply(skynet_sock, atoi(bytes)));
        break;
        default:
            printf("protocol error, got '%c' as reply type byte\n", s[0]);
       }

       return response;
}

/**
 * redis_sock_read_bulk_reply
 */
PHPAPI char *skynet_sock_read_bulk_reply(SkynetSock *skynet_sock, int bytes)
{
    char inbuf[1024], response[1024];
    int buf_len;

    if (bytes == -1) {
        strcpy(response, "nil");
    } else {
        char * reply = malloc(bytes);
        reply = php_stream_gets(skynet_sock->stream, inbuf, 1024);

        reply = estrndup(reply, (strlen(reply)-2));
        strcpy(response, reply);

        efree(reply);
    }

    return response;
}

/**
 * redis_sock_read_multibulk_reply
 */
PHPAPI int skynet_sock_read_multibulk_reply(INTERNAL_FUNCTION_PARAMETERS,
                                      SkynetSock *skynet_sock, int *buf_len TSRMLS_DC)
{
    char inbuf[1024], response[1024], *s;
    int length, response_len;

    s = php_stream_gets(skynet_sock->stream, inbuf, 1024);
    s = estrndup(s, (strlen(s)-2));

    if (s[0] != '*') {
        return -1;
    }

    length = strlen(s) - 1;

    char *sNumElems = (char*)malloc(sizeof(char) * length);

    strncpy(sNumElems, s + 1, length);

    int numElems = atoi(sNumElems);

    array_init(return_value);

    zval *member;
    zval trim_zv;
    MAKE_STD_ZVAL(member);

    while (numElems > 0) {
        s = redis_sock_read(skynet_sock, &response_len);

        add_next_index_string(return_value, s, 1);
        numElems--;
    }

    return 0;
}

/**
 * redis_sock_write
 */
PHPAPI int skynet_sock_write(SkynetSock *skynet_sock, char *cmd)
{
  uint8_t head[2];
  int n = strlen(cmd);
  head[0] = (n >> 8) & 0xff;
  head[1] = n & 0xff;
  // php_stream_putc(redis_sock->stream, head);
  php_stream_write(skynet_sock->stream, (char *)&head[0], sizeof(uint8_t) * 2);
  php_stream_write(skynet_sock->stream, cmd, strlen(cmd));
  return 0;
}

/**
 * redis_sock_get
 */
PHPAPI int skynet_sock_get(zval *id, SkynetSock **skynet_sock TSRMLS_DC)
{
    zval **socket;
    int resource_type;

    if (Z_TYPE_P(id) != IS_OBJECT || zend_hash_find(Z_OBJPROP_P(id), "socket",
                                  sizeof("socket"), (void **) &socket) == FAILURE) {
        return -1;
    }

    *skynet_sock = (SkynetSock *) zend_list_find(Z_LVAL_PP(socket), &resource_type);

    if (!*skynet_sock || resource_type != le_redis_sock) {
        return -1;
    }

    return Z_LVAL_PP(socket);
}


/**
 * redis_free_socket
 */
PHPAPI void skynet_free_socket(SkynetSock *skyent_sock)
{
    efree(skynet_sock->host);
    efree(skynet_sock);
}

/**
 * {{{ redis_json_implementioned
 */

/*
 * private API for redis_array_to_json
 */
PHPAPI static int parray_to_json_append_element(char *key, zval *value, struct json_object *jo);

PHPAPI static int parray_to_json_append_array(char *key, zval *z_array, struct json_object *jo)
{
   int count, i;
   zval **z_item;
   char strkey[256] = {0};
   struct json_object *sub_jo = json_object_new_object(); // new {}

   // 获取数组大小
   count = zend_hash_num_elements(Z_ARRVAL_P(z_array));
   // 将数组的内部指针指向第一个单元
   zend_hash_internal_pointer_reset(Z_ARRVAL_P(z_array)); 
  
   for (i = 0; i < count; i ++) {
      char* ikey;
      int idx;
      // 获取当前数据
      zend_hash_get_current_data(Z_ARRVAL_P(z_array), (void**) &z_item);
    
      // 获取当前的key
      if (HASH_KEY_IS_STRING == zend_hash_get_current_key(Z_ARRVAL_P(z_array), &ikey, &idx, 0)) {
	 parray_to_json_append_element(ikey, *z_item, sub_jo);
      } else {						
	 sprintf(strkey, "%d", idx);
	 // php_printf("number key is %s:%d <br>", strkey, idx);
	 parray_to_json_append_element(&strkey[0], *z_item, sub_jo);
	 memset(strkey, 0, sizeof(strkey));
      }
      
      // 将数组中的内部指针向前移动一位
      zend_hash_move_forward(Z_ARRVAL_P(z_array));
   }

   json_object_object_add(jo, key, sub_jo);
}

PHPAPI static int parray_to_json_append_element(char *key, zval *value, struct json_object *jo)
{
   struct json_object *tmp_jo = NULL;
   switch (Z_TYPE_P(value)) {
   case IS_NULL:
      /* NULLs are echoed as nothing */
      break;
   case IS_BOOL:
      tmp_jo = json_object_new_bool(Z_BVAL_P(value));
      if (NULL != tmp_jo) json_object_object_add(jo, key, tmp_jo);
      break;
   case IS_LONG:
      tmp_jo = json_object_new_int(Z_LVAL_P(value));
      if (NULL != tmp_jo) json_object_object_add(jo, key, tmp_jo);
      break;
   case IS_DOUBLE:
      tmp_jo = json_object_new_double(Z_LVAL_P(value));
      if (NULL != tmp_jo) json_object_object_add(jo, key, tmp_jo);
      break;
   case IS_STRING:
      tmp_jo = json_object_new_string(Z_STRVAL_P(value));
      if (NULL != tmp_jo) json_object_object_add(jo, key, tmp_jo);
      break;
   case IS_RESOURCE:
      break;
   case IS_ARRAY:
      parray_to_json_append_array(key, value, jo);
      break;
   case IS_OBJECT:
      break;
   default:
      /* Should never happen in practice,
       * but it's dangerous to make assumptions
       */
      break;
   }
} 

/*
 */

PHPAPI int redis_array_to_json(zval *z_array, struct json_object *jo)
{
  int count, i;
  zval **z_item;
  char strkey[256] = {0};

  // 获取数组大小
  count = zend_hash_num_elements(Z_ARRVAL_P(z_array));
  // 将数组的内部指针指向第一个单元
  zend_hash_internal_pointer_reset(Z_ARRVAL_P(z_array)); 
  
  for (i = 0; i < count; i ++) {
    char* key;
    int idx;
    
    // 获取当前数据
    zend_hash_get_current_data(Z_ARRVAL_P(z_array), (void**) &z_item);
    
    // 获取当前的key
    if (HASH_KEY_IS_STRING == zend_hash_get_current_key(Z_ARRVAL_P(z_array), &key, &idx, 0)) {
       parray_to_json_append_element(key, *z_item, jo);
    } else {						
       sprintf(strkey, "%d", idx);
       // php_printf("number key is %s:%d <br>", strkey, idx);
       parray_to_json_append_element(&strkey[0], *z_item, jo);
       memset(strkey, 0, sizeof(strkey));
    }
    
    // convert_to_string_ex(z_item);
    // php_printf("[%s %d]%d <br>", __FILE__, __LINE__, Z_TYPE_P(*z_item));
    
    // 将数组中的内部指针向前移动一位
    zend_hash_move_forward(Z_ARRVAL_P(z_array));
  }
  // php_printf("<hr>");
  // char *ret_str = json_object_to_json_string(jo);
  // php_printf("%s <br>", ret_str);
  
  return 1;
}
/**
 * }}}
 */

/* {{{ strnatcmp_ex
 */
PHPAPI int redis_strnatcmp_ex(char const *a, size_t a_len, char const *b, size_t b_len, int fold_case)
{
	unsigned char ca, cb;
	char const *ap, *bp;
	char const *aend = a + a_len,
			   *bend = b + b_len;
	int fractional, result;
	short leading = 1;

	if (a_len == 0 || b_len == 0)
		return a_len - b_len;

	ap = a;
	bp = b;
	while (1) {
		ca = *ap; cb = *bp;

		/* skip over leading zeros */
		while (leading && ca == '0' && (ap+1 < aend) && isdigit(*(ap+1))) {
			ca = *++ap;
		}

		while (leading && cb == '0' && (bp+1 < bend) && isdigit(*(bp+1))) {
			cb = *++bp;
		}

		leading = 0;

		/* Skip consecutive whitespace */
		while (isspace((int)(unsigned char)ca)) {
			ca = *++ap;
		}

		while (isspace((int)(unsigned char)cb)) {
			cb = *++bp;
		}

		/* process run of digits */
		if (isdigit((int)(unsigned char)ca)  &&  isdigit((int)(unsigned char)cb)) {
			fractional = (ca == '0' || cb == '0');

			if (fractional)
				result = compare_left(&ap, aend, &bp, bend);
			else
				result = compare_right(&ap, aend, &bp, bend);

			if (result != 0)
				return result;
			else if (ap == aend && bp == bend)
				/* End of the strings. Let caller sort them out. */
				return 0;
			else {
				/* Keep on comparing from the current point. */
				ca = *ap; cb = *bp;
			}
		}

		if (fold_case) {
			ca = toupper((int)(unsigned char)ca);
			cb = toupper((int)(unsigned char)cb);
		}

		if (ca < cb)
			return -1;
		else if (ca > cb)
			return +1;

		++ap; ++bp;
		if (ap >= aend && bp >= bend)
			/* The strings compare the same.  Perhaps the caller
			   will want to call strcmp to break the tie. */
			return 0;
		else if (ap >= aend)
			return -1;
		else if (bp >= bend)
			return 1;
	}
}
/* }}} */


/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("skynet.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_skynet_globals, skynet_globals)
    STD_PHP_INI_ENTRY("skynet.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_skynet_globals, skynet_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_skynet_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_skynet_init_globals(zend_skynet_globals *skynet_globals)
{
	skynet_globals->global_value = 0;
	skynet_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(skynet)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	zend_class_entry skynet_class_entry;
    INIT_CLASS_ENTRY(skynet_class_entry, "Skynet", skynet_functions);
    skynet_ce = zend_register_internal_class(&skynet_class_entry TSRMLS_CC);

    zend_class_entry skynet_exception_class_entry;
    INIT_CLASS_ENTRY(skynet_exception_class_entry, "SkynetException", NULL);
	skynet_exception_ce = zend_register_internal_class_ex(
        &skynet_exception_class_entry,
        skynet_get_exception_base(0 TSRMLS_CC),
        NULL TSRMLS_CC
    );

    le_skynet_sock = zend_register_list_destructors_ex(
        skynet_destructor_redis_sock,
        NULL,
        skynet_sock_name, module_number
    );

    add_constant_long(skynet_ce, "CALL", METHOD_CALL);
    add_constant_long(skynet_ce, "CAST", METHOD_CAST);
    add_constant_long(skynet_ce, "SEND", METHOD_SEND);
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(skynet)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(skynet)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(skynet)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(skynet)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "skynet support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* {{{ proto Redis Redis::__construct()
    Public constructor */
PHP_METHOD(Skynet, __construct)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} */

/* {{{ proto boolean Redis::connect(string host, int port [, int timeout])
 */
PHP_METHOD(Skynet, connect)
{
    zval *object;
    int host_len, id;
    char *host = NULL;
    long port;

    struct timeval timeout = {5L, 0L};
    SkynetSock *skynet_sock  = NULL;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Osl|l",
                                     &object, skynet_ce, &host, &host_len, &port,
                                     &timeout.tv_sec) == FAILURE) {
       RETURN_FALSE;
    }

    if (timeout.tv_sec < 0L || timeout.tv_sec > INT_MAX) {
        zend_throw_exception(skynet_exception_ce, "Invalid timeout", 0 TSRMLS_CC);
        RETURN_FALSE;
    }

    skynet_sock = skynet_sock_create(host, host_len, port, timeout.tv_sec);

    if (skynet_sock_server_open(skynet_sock, 1 TSRMLS_CC) < 0) {
        skynet_free_socket(skynet_sock);
        zend_throw_exception_ex(
            skynet_exception_ce,
            0 TSRMLS_CC,
            "Can't connect to %s:%d",
            host,
            port
        );
        RETURN_FALSE;
    }

    id = zend_list_insert(skynet_sock, le_skynet_sock);
    add_property_resource(object, "socket", id);

    RETURN_TRUE;
}
/* }}} */


/* {{{ proto boolean Redis::rpc(service, method, param)
 */
PHP_METHOD(Skynet, rpc)
{
  zval *object;
  SkynetSock *skynet_sock = NULL;
  int method_type; // char *method_str = NULL;
  char *service_name = NULL; int service_len; 
  zval *param_array = NULL;
  char *handle = NULL; int handle_len;

  if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oslsa",
				   &object, skynet_ce, &service_name, &service_len, &method_type, &handle, &handle_len, &param_array) == FAILURE) {
    RETURN_FALSE;
  }

  if (skynet_sock_get(object, &skynet_sock TSRMLS_CC) < 0) {
    RETURN_FALSE;
  }
  
  struct json_object *skynet_jo = json_object_new_object();
  json_object_object_add(skynet_jo, "service", json_object_new_string(service_name));
  /*
  if (METHOD_CALL == method_type) {
    method_str = "CALL";
  } else if (METHOD_CAST == method_type) {
    method_str = "CAST";
  } else if (METHOD_SEND == method_type) {
    method_str = "SEND";
  } else { 
    RETURN_FALSE;
  }
  */
  if (method_type < METHOD_CALL || method_type > METHOD_SEND) RETURN_FALSE;
  json_object_object_add(skynet_jo, "method", json_object_new_int(method_type));

  json_object_object_add(skynet_jo, "handle", json_object_new_string(handle));
  
  json_object *skynet_param_jo = json_object_new_object();
  if (!redis_array_to_json(param_array, skynet_param_jo)) {
    RETURN_FALSE;
  }
    
  json_object_object_add(skynet_jo, "param", skynet_param_jo);
  char *json_str = json_object_to_json_string(skynet_jo);
  
  if (skynet_sock_write(skynet_sock, json_str) < 0) {
    RETURN_FALSE;
  }
  
  RETURN_TRUE;
  /* if (redis_sock_get(object, &redis_sock TSRMLS_CC) < 0) { */
  /*     RETURN_FALSE; */
  /* } */

  /* if (redis_sock_disconnect(redis_sock TSRMLS_CC)) { */
  /*     RETURN_TRUE; */
  /* } */

  /* RETURN_FALSE; */
}
/* }}} */

/* Remove the following function when you have succesfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_skynet_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(confirm_skynet_compiled)
{
	char *arg = NULL;
	int arg_len, len;
	char *strg;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &arg, &arg_len) == FAILURE) {
		return;
	}

	len = spprintf(&strg, 0, "Congratulations! You have successfully modified ext/%.78s/config.m4. Module %.78s is now compiled into PHP.", "skynet", arg);
	RETURN_STRINGL(strg, len, 0);
}
/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
