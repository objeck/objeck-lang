AC_INIT(objeck, 3.3.5.1, objeck@gmail.com)
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(src/shared/version.h)

# CPU flags
AC_CANONICAL_TARGET
AS_CASE([$host_cpu],
	[i?86], [JIT_DIR=jit/ia32 JIT_LIB=jit_intel_lp32.a CPU=-m32],
	[x86_64], [JIT_DIR=jit/amd64 JIT_LIB=jit_amd_lp64.a CPU=-m64 MEM=-D_X64],
	[AC_MSG_ERROR("Unsupported CPU type")]
)
AC_SUBST([JIT_DIR])
AC_SUBST([JIT_LIB])
AC_SUBST([CPU])
AC_SUBST([MEM])

# platform flags
AC_CANONICAL_HOST
AS_CASE([$host_os],
	[darwin*], [OS=-D_OSX],
	[linux*], [OS=],
	[AC_MSG_ERROR("Unsupported OS")]
)
AC_SUBST([OS])

MY_BIN=$bindir
AC_SUBST([MY_BIN])

MY_DATA=$datarootdir/objeck
AC_SUBST([MY_DATA])

MY_LIB=$libdir
AC_SUBST([MY_LIB])

AM_INIT_AUTOMAKE
AC_PROG_CXX

# FIXME: Replace `main' with a function in `-lssl':
AC_CHECK_LIB([ssl], [main])
# FIXME: Replace `main' with a function in `-lcrypto':
AC_CHECK_LIB([crypto], [main])
# FIXME: Replace `main' with a function in `-ldl':
AC_CHECK_LIB([dl], [main])
# FIXME: Replace `main' with a function in `-lcrypto':
AC_CHECK_LIB([crypto], [main])
# FIXME: Replace `main' with a function in `-odbc':
AC_CHECK_LIB([odbc], [main])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/socket.h sys/time.h sys/timeb.h unistd.h])

AC_OUTPUT(Makefile src/compiler/Makefile src/utilities/Makefile src/vm/Makefile src/vm/os/posix/Makefile src/vm/jit/amd64/Makefile src/vm/jit/ia32/Makefile src/vm/debugger/config/Makefile src/vm/debugger/Makefile src/vm/lib/odbc/Makefile src/vm/lib/openssl/Makefile)

# echo $JIT_DIR
# echo $JIT_LIB
# echo $CPU
# echo $MEM
# echo $OS
