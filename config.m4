dnl $Id: config.m4,v 1.1 2009/10/07 09:38:13 tony Exp $

PHP_ARG_ENABLE(funcprof, whether to enable funcprof support,
[  --enable-funcprof           Enable funcprof support])

if test "$PHP_FUNCPROF" != "no"; then
  PHP_NEW_EXTENSION(funcprof, funcprof.c, $ext_shared)
fi
