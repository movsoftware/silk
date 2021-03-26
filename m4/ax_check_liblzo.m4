dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_liblzo.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")


# ---------------------------------------------------------------------------
# AX_CHECK_LIBLZO
#
#    Try to find the lzo library.
#
#    Version 2 of LZO (released May-2005) puts the headers into an
#    "lzo" subdirectory (DarwinPorts uses an lzo2 subdirectory) while
#    version 1 did not.
#
#    Substitutions: SK_ENABLE_LZO
#    Output defines: ENABLE_LZO, LZO_HEADER_NAME
#
AC_DEFUN([AX_CHECK_LIBLZO],[
    ENABLE_LZO=0

    lzo_header_names="lzo2/lzo1x.h lzo/lzo1x.h lzo1x.h"
    lzo_library_names="lzo2 lzo"

    AC_ARG_WITH([lzo],
        AS_HELP_STRING([--with-lzo=LZO_DIR],
            [specify location of the LZO file compression library; find "lzo2/lzo1x.h", "lzo/lzo1x.h", or "lzo1x.h", in LZO_DIR/include/; find "liblzo2.so" or "liblzo.so" in LZO_DIR/lib/ [auto]]),
        [
            if test "x$withval" != "xyes"
            then
                lzo_dir="$withval"
                lzo_includes="$lzo_dir/include"
                lzo_libraries="$lzo_dir/lib"
            fi
    ])
    AC_ARG_WITH([lzo-includes],[AS_HELP_STRING([--with-lzo-includes=DIR],
            [find "lzo1x.h" in DIR/ (overrides LZO_DIR/include/ and disables searching lzo2 and lzo subdirectories)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                lzo_dir=no
            elif test "x$withval" != "xyes"
            then
                lzo_includes="$withval"
                lzo_header_names="lzo1x.h"
            fi
    ])
    AC_ARG_WITH([lzo-libraries],[AS_HELP_STRING([--with-lzo-libraries=DIR],
            [find "liblzo2.so" or "liblzo.so" in DIR/ (overrides LZO_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                lzo_dir=no
            elif test "x$withval" != "xyes"
            then
                lzo_libraries="$withval"
            fi
    ])

    ENABLE_LZO=0
    if test "x$lzo_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$lzo_libraries" != "x"
        then
            LZO_LDFLAGS="-L$lzo_libraries"
            LDFLAGS="$LZO_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$lzo_includes" != "x"
        then
            LZO_CFLAGS="-I$lzo_includes"
            CPPFLAGS="$LZO_CFLAGS $sk_save_CPPFLAGS"
        fi

        for sk_lzo_hdr in $lzo_header_names
        do
            AC_CHECK_HEADER([$sk_lzo_hdr], [
                sk_lzo_hdr="<$sk_lzo_hdr>"
                ENABLE_LZO=1
                break])
        done

        if test "x$ENABLE_LZO" = "x1"
        then
            #AC_MSG_CHECKING([for version of lzo])
            AC_PREPROC_IFELSE([AC_LANG_PROGRAM([[
#include $sk_lzo_hdr
#if LZO_VERSION < 0x2000
#  error "version 1"
#endif
]])],
               , [lzo_library_names="lzo"])

            ENABLE_LZO=0

            # Loop over the library names
            AC_SEARCH_LIBS([lzo1x_1_15_compress],[$lzo_library_names],[ENABLE_LZO=1])
            if test "x$ENABLE_LZO" = "x1"
            then
                case "(X$ac_cv_search_lzo1x_1_15_compress" in *X-l*)
                    LZO_LDFLAGS="$LZO_LDFLAGS $ac_cv_search_lzo1x_1_15_compress" ;;
                esac
            fi
        fi

        if test "x$ENABLE_LZO" = "x1"
        then
            AC_MSG_CHECKING([usability of lzo library and headers])
            LDFLAGS="$sk_save_LDFLAGS"
            LIBS="$LZO_LDFLAGS $sk_save_LIBS"
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM([
#include $sk_lzo_hdr
                ],[
const lzo_bytep src;
lzo_bytep dst;
lzo_uint src_len;
lzo_uintp dst_len;
lzo_voidp wrkmem;

lzo1x_1_15_compress (src, src_len, dst, dst_len, wrkmem);
                ])], [AC_MSG_RESULT([yes])], [
                    AC_MSG_RESULT([no])
                    ENABLE_LZO=0])
        fi

        if test "x$ENABLE_LZO" = "x1"
        then
            [sk_lzo_asm_hdr=`echo "$sk_lzo_hdr" | sed 's/lzo1x/lzo_asm/' | sed 's/[<>]//g'`]
            AC_CHECK_HEADER($sk_lzo_asm_hdr, [
                sk_lzo_asm_hdr="<$sk_lzo_asm_hdr>"
                AC_CHECK_FUNCS([lzo1x_decompress_asm_fast_safe])
            ])
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_LZO" = "x0"
    then
        LZO_LDFLAGS=
        LZO_CFLAGS=
    else
        AC_DEFINE_UNQUOTED([LZO_HEADER_NAME], [$sk_lzo_hdr],
            [When SK_ENABLE_LZO is set, this is the path to the lzo1x.h header file])
        AC_DEFINE_UNQUOTED([LZO_ASM_HEADER_NAME], [$sk_lzo_asm_hdr],
            [When SK_HAVE_LZO1X_DECOMPRESS_ASM_FAST_SAFE is defined, this is the path to the lzo_asm.h header file])
    fi

    AC_DEFINE_UNQUOTED([ENABLE_LZO], [$ENABLE_LZO],
        [Define to 1 to build with support for LZO compression.
         Define to 0 otherwise.  Requires the liblzo or liblzo2 library
         and the <lzo1x.h> header file.])
    AC_SUBST([SK_ENABLE_LZO],[$ENABLE_LZO])
])# AX_CHECK_LIBLZO

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
