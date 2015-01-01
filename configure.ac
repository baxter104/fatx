AC_PREREQ([2.69])
AC_CONFIG_MACRO_DIR([m4])
AC_INIT([fatx], [1.7], [baxter@about.org], [fatx], [http://sourceforge.net/projects/fatx/])
AM_INIT_AUTOMAKE
: ${CFLAGS=""}
: ${CXXFLAGS=""}
AC_ARG_ENABLE([dbg], [  --enable-dbg            build with additional debugging code], [CPPFLAGS+='-D DEBUG '])
AC_ARG_ENABLE([gdb], [  --enable-gdb            build with -g and low optimisation], [CXXFLAGS+='-g '; CPPFLAGS+='-O1 '], [CPPFLAGS+='-O6 '])
AC_ARG_ENABLE([static], [  --enable-static         link libraries statically], [LDFLAGS+='-static '])
AC_ARG_ENABLE([xbe], [  --enable-xbe            produce .xbe files], [
	case "${enableval}" in
	yes)	xbe=true ;;
	no)		xbe=false ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --enable-xbe]) ;;
	esac
])
AM_CONDITIONAL([xbe], [test x$xbe = xtrue])
PKG_CHECK_MODULES([fuse], [fuse], [], [CPPFLAGS+='-D_FILE_OFFSET_BITS=64 -D NO_FUSE_CALL '])
AM_CONDITIONAL([fuse],  [test -n "$fuse_LIBS"])
AM_MAINTAINER_MODE([enable])
AC_PROG_AWK
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_0X
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_HEADER_STDC
AC_CHECK_LIB([boost_program_options], [main], [AC_DEFINE([HAVE_LIBBOOST_PROGRAM_OPTIONS], [1], [Define if you have boost_program_options])])
AC_CHECK_LIB([pthread], [main], [AC_DEFINE([HAVE_LIBPTHREAD], [1], [Define if you have pthread])])
AC_CHECK_HEADERS([fcntl.h string.h unistd.h])
AC_CHECK_HEADER_STDBOOL
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT64_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset])
DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)
DX_INIT_DOXYGEN([$PACKAGE_NAME], [doxygen.cfg], [doc])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
