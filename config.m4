dnl $Id$
dnl config.m4 for extension skynet

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(skynet, for skynet support,
dnl Make sure that the comment is aligned:
dnl [  --with-skynet             Include skynet support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(skynet, whether to enable skynet support,
dnl Make sure that the comment is aligned:
[  --enable-skynet           Enable skynet support])

if test "$PHP_SKYNET" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-skynet -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/skynet.h"  # you most likely want to change this
  dnl if test -r $PHP_SKYNET/$SEARCH_FOR; then # path given as parameter
  dnl   SKYNET_DIR=$PHP_SKYNET
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for skynet files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       SKYNET_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$SKYNET_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the skynet distribution])
  dnl fi

  dnl # --with-skynet -> add include path
  dnl PHP_ADD_INCLUDE($SKYNET_DIR/include)

  dnl # --with-skynet -> check for lib and symbol presence
  dnl LIBNAME=skynet # you may want to change this
  dnl LIBSYMBOL=skynet # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $SKYNET_DIR/lib, SKYNET_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_SKYNETLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong skynet lib version or lib not found])
  dnl ],[
  dnl   -L$SKYNET_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(SKYNET_SHARED_LIBADD)

  PHP_NEW_EXTENSION(skynet, skynet.c, cjson/arraylist.c cjson/debug.c cjson/json_object.c cjson/json_tokener.c  cjson/json_util.c cjson/linkhash.c cjson/printbuf.c , $ext_shared)
fi
