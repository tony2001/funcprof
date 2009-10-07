/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2009 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Antony Dovgal <tony@daylessday.org>                          |
  +----------------------------------------------------------------------+
*/

/* $Id: funcprof.c,v 1.1 2009/10/07 09:38:13 tony Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_funcprof.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_constants.h"
#include "zend_compile.h"
#include "zend_extensions.h"

ZEND_DECLARE_MODULE_GLOBALS(funcprof)

#ifdef COMPILE_DL_FUNCPROF
ZEND_GET_MODULE(funcprof)
#endif

int funcprof_execute_initialized = 0;

void (*funcprof_old_execute)(zend_op_array *op_array TSRMLS_DC);
void funcprof_execute(zend_op_array *op_array TSRMLS_DC);
void (*funcprof_old_execute_internal)(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC);
void funcprof_execute_internal(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC);

static char *mt_get_function_name(TSRMLS_D) /* {{{ */
{
	char *current_fname = NULL;
	char *class_name, *fname;
	zend_bool free_fname = 0;
	int class_name_len, fname_len;
	zend_execute_data *exec_data = EG(current_execute_data);

#if PHP_MAJOR_VERSION > 4
	char *space;

	class_name = get_active_class_name(&space TSRMLS_CC);

	if (space[0] == '\0') {
		current_fname = get_active_function_name(TSRMLS_C);
	} else {
		fname = get_active_function_name(TSRMLS_C);
		if (fname) {
			class_name_len = strlen(class_name);
			fname_len = strlen(fname);

			current_fname = emalloc(class_name_len + 2 + fname_len + 1);
			free_fname = 1;

			memcpy(current_fname, class_name, class_name_len);
			memcpy(current_fname + class_name_len, "::", 2);
			memcpy(current_fname + class_name_len + 2, fname, fname_len);
			current_fname[class_name_len + 2 + fname_len] = '\0';
		}
	}
#else
	class_name = "";
	if (exec_data && exec_data->object.ptr && Z_OBJCE_P(exec_data->object.ptr)) {
		class_name = Z_OBJCE_P(exec_data->object.ptr)->name;
	}

	if (class_name[0] == '\0') {
		current_fname = get_active_function_name(TSRMLS_C);
	} else {
		fname = get_active_function_name(TSRMLS_C);
		if (fname) {
			class_name_len = strlen(class_name);
			fname_len = strlen(fname);

			current_fname = emalloc(class_name_len + 2 + fname_len + 1);
			free_fname = 1;

			memcpy(current_fname, class_name, class_name_len);
			memcpy(current_fname + class_name_len, "::", 2);
			memcpy(current_fname + class_name_len + 2, fname, fname_len);
			current_fname[class_name_len + 2 + fname_len] = '\0';
		}
	}
#endif

	if (!current_fname) {
		current_fname = "main";
	}

	if (!free_fname && !strcmp("main", current_fname)) {

		if (exec_data && exec_data->opline && exec_data->opline->op2.op_type == IS_UNUSED) {
			switch (Z_LVAL(exec_data->opline->op2.u.constant)) {
				case ZEND_REQUIRE_ONCE:
					current_fname = "require_once";
					break;
				case ZEND_INCLUDE:
					current_fname = "include";
					break;
				case ZEND_REQUIRE:
					current_fname = "require";
					break;
				case ZEND_INCLUDE_ONCE:
					current_fname = "include_once";
					break;
				case ZEND_EVAL:
					current_fname = "eval";
					break;
			}
		}
	}

	if (!free_fname) {
		return estrdup(current_fname);
	} else {
		return current_fname;
	}
}
/* }}} */

static void php_funcprof_parse_ignore_funcs(TSRMLS_D) /* {{{ */
{
	char *tmp, *for_free, *start = NULL;
	int dummy = 1, start_len;

	if (!FUNCPROF_G(ignore_functions) || FUNCPROF_G(ignore_functions)[0] == '\0') {
		return;
	}

	tmp = estrdup(FUNCPROF_G(ignore_functions));
	for_free = tmp;
	while(*tmp) {
		switch (*tmp) {
			case ' ':
			case ',':
				if (start) {
					*tmp = '\0';
					start_len = strlen(start);

					if (start_len) {
						zend_str_tolower(start, start_len);
						zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), start, start_len + 1, (void *)&dummy, sizeof(int), NULL);
					}
					start = NULL;
				}
				break;
			default:
				if (!start) {
					start = tmp;
				}
				break;
		}
		tmp++;
	}
	if (start) {
		start_len = strlen(start);

		if (start_len) {
			zend_str_tolower(start, start_len);
			zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), start, start_len + 1, (void *)&dummy, sizeof(int), NULL);
		}
	}
	efree(for_free);
}
/* }}} */

static void php_funcprof_init_globals(zend_funcprof_globals *funcprof_globals) /* {{{ */
{
	memset(funcprof_globals, 0, sizeof(zend_funcprof_globals));
}
/* }}} */

/* {{{ PHP_INI
 */
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("funcprof.enabled",         "0", PHP_INI_SYSTEM, OnUpdateBool, enabled, zend_funcprof_globals, funcprof_globals)
    STD_PHP_INI_ENTRY("funcprof.ignore_functions", "", PHP_INI_SYSTEM, OnUpdateString, ignore_functions, zend_funcprof_globals, funcprof_globals)
PHP_INI_END()
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(funcprof)
{
	ZEND_INIT_MODULE_GLOBALS(funcprof, php_funcprof_init_globals, NULL);

	REGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(funcprof)
{
	UNREGISTER_INI_ENTRIES();

	if (funcprof_execute_initialized) {
		zend_execute = funcprof_old_execute;
		zend_execute_internal = funcprof_old_execute_internal;
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(funcprof)
{
	int dummy = 1;

	if (!FUNCPROF_G(enabled)) {
		return SUCCESS;
	}

	zend_hash_init(&FUNCPROF_G(ignore_funcs_hash), 16, NULL, NULL, 0);

	if (!funcprof_execute_initialized) {
		funcprof_old_execute = zend_execute;
		zend_execute = funcprof_execute;

		funcprof_old_execute_internal = zend_execute_internal;
		zend_execute_internal = funcprof_execute_internal;
		funcprof_execute_initialized = 1;
	}

	php_funcprof_parse_ignore_funcs(TSRMLS_C);
	/* always ignore our own funcs */
	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), "funcprof_set_begin_callback", sizeof("funcprof_set_begin_callback"), (void *)&dummy, sizeof(int), NULL);
	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), "funcprof_set_end_callback", sizeof("funcprof_set_end_callback"), (void *)&dummy, sizeof(int), NULL);
	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), "funcprof_set_begin_callback_int", sizeof("funcprof_set_begin_callback_int"), (void *)&dummy, sizeof(int), NULL);
	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), "funcprof_set_end_callback_int", sizeof("funcprof_set_end_callback_int"), (void *)&dummy, sizeof(int), NULL);
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(funcprof)
{
	if (FUNCPROF_G(begin_callback)) {
		zval_ptr_dtor(&FUNCPROF_G(begin_callback));
		efree(FUNCPROF_G(begin_callback_name));
	}
	if (FUNCPROF_G(end_callback)) {
		zval_ptr_dtor(&FUNCPROF_G(end_callback));
		efree(FUNCPROF_G(end_callback_name));
	}

	if (FUNCPROF_G(begin_callback_int)) {
		zval_ptr_dtor(&FUNCPROF_G(begin_callback_int));
		efree(FUNCPROF_G(begin_callback_name_int));
	}
	if (FUNCPROF_G(end_callback_int)) {
		zval_ptr_dtor(&FUNCPROF_G(end_callback_int));
		efree(FUNCPROF_G(end_callback_name_int));
	}

	if (!FUNCPROF_G(enabled)) {
		return SUCCESS;
	}

	zend_hash_destroy(&FUNCPROF_G(ignore_funcs_hash));
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(funcprof)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "funcprof support", "enabled");
	php_info_print_table_row(2, "Revision", "$Revision: 1.1 $");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* {{{ proto bool funcprof_set_begin_callback(mixed callback) */
PHP_FUNCTION(funcprof_set_begin_callback)
{
	zval *callback;
	char *callback_name;
	int name_len, dummy = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_C, "z", &callback) != SUCCESS) {
		return;
	}

	if (!zend_is_callable(callback, 0, &callback_name)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", callback_name);
		efree(callback_name);
		RETURN_FALSE;
	}

	if (FUNCPROF_G(begin_callback)) {
		zval_ptr_dtor(&FUNCPROF_G(begin_callback));
		efree(FUNCPROF_G(begin_callback_name));
		zend_hash_del(&FUNCPROF_G(ignore_funcs_hash), FUNCPROF_G(begin_callback_name), strlen(FUNCPROF_G(begin_callback_name) + 1));
	}
	
	MAKE_STD_ZVAL(FUNCPROF_G(begin_callback))
	*FUNCPROF_G(begin_callback) = *callback;
	zval_copy_ctor(FUNCPROF_G(begin_callback));

	name_len = strlen(callback_name);
	zend_str_tolower(callback_name, name_len);

	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), callback_name, name_len + 1, (void *)&dummy, sizeof(int), NULL);

	FUNCPROF_G(begin_callback_name) = callback_name;
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool funcprof_set_end_callback(mixed callback) */
PHP_FUNCTION(funcprof_set_end_callback)
{
	zval *callback;
	char *callback_name;
	int name_len, dummy = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_C, "z", &callback) != SUCCESS) {
		return;
	}

	if (!zend_is_callable(callback, 0, &callback_name)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", callback_name);
		efree(callback_name);
		RETURN_FALSE;
	}

	if (FUNCPROF_G(end_callback)) {
		zval_ptr_dtor(&FUNCPROF_G(end_callback));
		efree(FUNCPROF_G(end_callback_name));
		zend_hash_del(&FUNCPROF_G(ignore_funcs_hash), FUNCPROF_G(end_callback_name), strlen(FUNCPROF_G(end_callback_name) + 1));
	}
	
	MAKE_STD_ZVAL(FUNCPROF_G(end_callback))
	*FUNCPROF_G(end_callback) = *callback;
	zval_copy_ctor(FUNCPROF_G(end_callback));

	name_len = strlen(callback_name);
	zend_str_tolower(callback_name, name_len);

	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), callback_name, name_len + 1, (void *)&dummy, sizeof(int), NULL);
	
	FUNCPROF_G(end_callback_name) = callback_name;
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool funcprof_set_begin_callback_int(mixed callback) */
PHP_FUNCTION(funcprof_set_begin_callback_int)
{
	zval *callback;
	char *callback_name;
	int name_len, dummy = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_C, "z", &callback) != SUCCESS) {
		return;
	}

	if (!zend_is_callable(callback, 0, &callback_name)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", callback_name);
		efree(callback_name);
		RETURN_FALSE;
	}

	if (FUNCPROF_G(begin_callback_int)) {
		zval_ptr_dtor(&FUNCPROF_G(begin_callback_int));
		efree(FUNCPROF_G(begin_callback_name_int));
		zend_hash_del(&FUNCPROF_G(ignore_funcs_hash), FUNCPROF_G(begin_callback_name_int), strlen(FUNCPROF_G(begin_callback_name_int) + 1));
	}
	
	MAKE_STD_ZVAL(FUNCPROF_G(begin_callback_int));
	*FUNCPROF_G(begin_callback_int) = *callback;
	zval_copy_ctor(FUNCPROF_G(begin_callback_int));

	name_len = strlen(callback_name);
	zend_str_tolower(callback_name, name_len);

	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), callback_name, name_len + 1, (void *)&dummy, sizeof(int), NULL);

	FUNCPROF_G(begin_callback_name_int) = callback_name;
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool funcprof_set_end_callback_int(mixed callback) */
PHP_FUNCTION(funcprof_set_end_callback_int)
{
	zval *callback;
	char *callback_name;
	int name_len, dummy = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_C, "z", &callback) != SUCCESS) {
		return;
	}

	if (!zend_is_callable(callback, 0, &callback_name)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", callback_name);
		efree(callback_name);
		RETURN_FALSE;
	}

	if (FUNCPROF_G(end_callback_int)) {
		zval_ptr_dtor(&FUNCPROF_G(end_callback_int));
		efree(FUNCPROF_G(end_callback_name_int));
		zend_hash_del(&FUNCPROF_G(ignore_funcs_hash), FUNCPROF_G(end_callback_name_int), strlen(FUNCPROF_G(end_callback_name_int) + 1));
	}
	
	MAKE_STD_ZVAL(FUNCPROF_G(end_callback_int));
	*FUNCPROF_G(end_callback_int) = *callback;
	zval_copy_ctor(FUNCPROF_G(end_callback_int));

	name_len = strlen(callback_name);
	zend_str_tolower(callback_name, name_len);

	zend_hash_add(&FUNCPROF_G(ignore_funcs_hash), callback_name, name_len + 1, (void *)&dummy, sizeof(int), NULL);
	
	FUNCPROF_G(end_callback_name_int) = callback_name;
	RETURN_TRUE;
}
/* }}} */

/* {{{ funcprof_functions[]
 */
zend_function_entry funcprof_functions[] = {
	PHP_FE(funcprof_set_begin_callback, NULL)
	PHP_FE(funcprof_set_end_callback, NULL)
	PHP_FE(funcprof_set_begin_callback_int, NULL)
	PHP_FE(funcprof_set_end_callback_int, NULL)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ funcprof_module_entry
 */
zend_module_entry funcprof_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"funcprof",
	funcprof_functions,
	PHP_MINIT(funcprof),
	PHP_MSHUTDOWN(funcprof),
	PHP_RINIT(funcprof),
	PHP_RSHUTDOWN(funcprof),
	PHP_MINFO(funcprof),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_FUNCPROF_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

static void funcprof_call_callback(zval *callback TSRMLS_DC) /* {{{ */
{
	zval *funcname;
	zval **args[1];
	zval *retval = NULL;
	char *fname, *lc_fname;
	int fname_len;

	if (!callback || FUNCPROF_G(in_callback)) {
		return;
	}

	fname = mt_get_function_name(TSRMLS_C);
	fname_len = strlen(fname);
	lc_fname = estrndup(fname, fname_len);
	zend_str_tolower(lc_fname, fname_len);

	if (zend_hash_exists(&FUNCPROF_G(ignore_funcs_hash), lc_fname, fname_len + 1) != 0) {
		/* ignored function */
		efree(fname);
		efree(lc_fname);
		return;
	}

	MAKE_STD_ZVAL(funcname);
	ZVAL_STRINGL(funcname, fname, fname_len, 0);

	args[0] = &funcname;
	FUNCPROF_G(in_callback) = 1;
	if (call_user_function_ex(EG(function_table), NULL, callback, &retval, 1, args, 0, NULL TSRMLS_CC) == SUCCESS && retval) {
		zval_ptr_dtor(&retval);
	} else {
		/* error */	
	}
	FUNCPROF_G(in_callback) = 0;
	zval_ptr_dtor(&funcname);
	efree(lc_fname);
}
/* }}} */


void funcprof_execute(zend_op_array *op_array TSRMLS_DC) /* {{{ */
{
	if (!FUNCPROF_G(begin_callback) && !FUNCPROF_G(end_callback)) {
		funcprof_old_execute(op_array TSRMLS_CC);
	} else {
		funcprof_call_callback(FUNCPROF_G(begin_callback) TSRMLS_CC);
		funcprof_old_execute(op_array TSRMLS_CC);
		funcprof_call_callback(FUNCPROF_G(end_callback) TSRMLS_CC);
	}
}
/* }}} */

void funcprof_execute_internal(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC) /* {{{ */
{
	if (!FUNCPROF_G(begin_callback_int) && !FUNCPROF_G(end_callback_int)) {
		execute_internal(current_execute_data, return_value_used TSRMLS_CC);
	} else {
		funcprof_call_callback(FUNCPROF_G(begin_callback_int) TSRMLS_CC);
		execute_internal(current_execute_data, return_value_used TSRMLS_CC);
		funcprof_call_callback(FUNCPROF_G(end_callback_int) TSRMLS_CC);
	}
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
