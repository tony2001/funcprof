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

/* $Id: php_funcprof.h,v 1.1 2009/10/07 09:38:13 tony Exp $ */

#ifndef PHP_FUNCPROF_H
#define PHP_FUNCPROF_H

#define PHP_FUNCPROF_VERSION "0.2.0-dev"

extern zend_module_entry funcprof_module_entry;
#define phpext_funcprof_ptr &funcprof_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif

ZEND_BEGIN_MODULE_GLOBALS(funcprof)
	zend_bool enabled;
	char *ignore_functions;
	HashTable ignore_funcs_hash;
	zval *begin_callback;
	char *begin_callback_name;
	zval *end_callback;
	char *end_callback_name;
	zval *begin_callback_int;
	char *begin_callback_name_int;
	zval *end_callback_int;
	char *end_callback_name_int;
	zend_bool in_callback;
ZEND_END_MODULE_GLOBALS(funcprof)

#ifdef ZTS
#define FUNCPROF_G(v) TSRMG(funcprof_globals_id, zend_funcprof_globals *, v)
#else
#define FUNCPROF_G(v) (funcprof_globals.v)
#endif

#endif	/* PHP_FUNCPROF_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
