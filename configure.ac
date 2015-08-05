# Process this file with autoconf to produce a configure script.
AC_INIT(Yate, 5.5.1)
AC_CONFIG_SRCDIR([README])
AC_PREREQ(2.52)

if echo "$ac_confdir/$ac_pwd" | grep -q '[[[:space:]\\\$\"'\'']]'; then
    AC_ERROR([Unsupported character(s) in source or current directory name])
fi

PACKAGE_RELEASE="1"
PACKAGE_STATUS="devel"
PACKAGE_REVISION=`svn info 2>/dev/null | sed -n 's,^Revision: *,,p'`
test -z "$PACKAGE_REVISION" && PACKAGE_REVISION=`cat "$srcdir/packing/revision.txt" 2>/dev/null`

PACKAGE_VERSION_MAJOR="${PACKAGE_VERSION%%.*}"
PACKAGE_VERSION_MINOR="${PACKAGE_VERSION#*.}"
PACKAGE_VERSION_MINOR="${PACKAGE_VERSION_MINOR%.*}"
PACKAGE_VERSION_BUILD="${PACKAGE_VERSION##*.}"
PACKAGE_VERSION_RELEASE="$((0+${PACKAGE_RELEASE}))"
AC_SUBST(PACKAGE_VERSION_MAJOR)
AC_SUBST(PACKAGE_VERSION_MINOR)
AC_SUBST(PACKAGE_VERSION_BUILD)
AC_SUBST(PACKAGE_VERSION_RELEASE)
AC_SUBST(PACKAGE_RELEASE)
AC_SUBST(PACKAGE_STATUS)
AC_SUBST(PACKAGE_REVISION)

# We may need the host OS type but avoid the overhead of AC_CANONICAL_SYSTEM
AC_MSG_CHECKING([for local operating system type])
uname_os=`uname -s`
test -x "$uname_os" && uname_os=Unknown
AC_MSG_RESULT([$uname_os])

# Check if we should look for alternate library names
AC_ARG_WITH(archlib,AC_HELP_STRING([--with-archlib=NAME],[use NAME as library directory name]),[ac_cv_use_archlib=$withval])
AC_MSG_CHECKING([for libraries directory name])
archname=`dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null`
test -z "$archname" && archname=`uname -m 2>/dev/null`
test -z "$archname" && archname=`arch -k 2>/dev/null`
test -z "$archname" && archname=`arch 2>/dev/null`
ARCHLIB="lib"
case "x$archname" in
    x*-*-*)
	ARCHLIB="lib/$archname"
	;;
    x*x86_64*|xppc64*|xaarch64)
	ARCHLIB=lib64
	;;
esac
case "x$ac_cv_use_archlib" in
    x|xyes)
	;;
    xno)
	ARCHLIB="lib"
	;;
    *)
	ARCHLIB="$ac_cv_use_archlib"
esac
test -d "/usr/$ARCHLIB" || ARCHLIB="lib"
AC_MSG_RESULT([$ARCHLIB])
AC_SUBST(ARCHLIB)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_AWK

AC_MSG_CHECKING([for sed command to use])
# sed expression to convert version into something comparable lexicographically
csed='sed'
vsed='s/\(^\|\.\)\([[0-9]]\)\($\|\.\)/\10\2\3/g'
if [[ -n "`echo a | sed 's/a\|b//'`" ]]; then
    csed='sed -E'
    vsed='s/(^|\.)([[0-9]])($|\.)/\10\2\3/g'
fi
AC_MSG_RESULT([$csed])

AC_C_BIGENDIAN([CFLAGS="$CFLAGS -DBIGENDIAN"])

# Check if gcc style printf argument check is supported
HAVE_GCC_FORMAT_CHECK=""
AC_MSG_CHECKING([for gcc printf format typechecks])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror"
AC_TRY_COMPILE([],[void f(void*x,const char*f,...) __attribute__((format(printf,2,3))); ],[ac_cv_format_checks=yes],[ac_cv_format_checks=no])
CFLAGS="$SAVE_CFLAGS"
if [[ "x$ac_cv_format_checks" = "xyes" ]]; then
    HAVE_GCC_FORMAT_CHECK="-DHAVE_GCC_FORMAT_CHECK"
fi
AC_MSG_RESULT([$ac_cv_format_checks])

# Check if disabling warnings for overloaded virtual functions is available
HAVE_NO_OVERLOAD_VIRT_WARN=""
AC_MSG_CHECKING([for -Wno-overloaded-virtual flag])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="-Wno-overloaded-virtual -Werror"
AC_TRY_COMPILE([],[],
[ac_cv_no_overload_virt=yes],[ac_cv_no_overload_virt=no])
CFLAGS="$SAVE_CFLAGS"
if [[ "x$ac_cv_no_overload_virt" = "xyes" ]]; then
    HAVE_NO_OVERLOAD_VIRT_WARN="-Wno-overloaded-virtual"
fi
AC_MSG_RESULT([$ac_cv_no_overload_virt])

GCC_UNRESOLVED_SYMBOLS="-Wl,--unresolved-symbols=ignore-in-shared-libs"
ld_unresolved_symbols=no
AC_MSG_CHECKING([if ld supports reporting unresolved symbols])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror $GCC_UNRESOLVED_SYMBOLS"
AC_TRY_LINK([],[],[ld_unresolved_symbols=yes],[GCC_UNRESOLVED_SYMBOLS=""])
CFLAGS="$SAVE_CFLAGS"
AC_MSG_RESULT([$ld_unresolved_symbols])

# Check if { instruction } blocks can return a value
HAVE_BLOCK_RETURN=no
AC_ARG_ENABLE(strings,AC_HELP_STRING([--enable-strings],[Create static Strings (default: yes)]),want_strings=$enableval,want_strings=yes)
if [[ "x$want_strings" = "xyes" ]]; then
AC_MSG_CHECKING([if instruction blocks return values])
AC_TRY_COMPILE([],[return ({static int ret = 1; ret;});],HAVE_BLOCK_RETURN="yes")
AC_MSG_RESULT([$HAVE_BLOCK_RETURN])
fi
if [[ "x$HAVE_BLOCK_RETURN" = "xyes" ]]; then
HAVE_BLOCK_RETURN="-DHAVE_BLOCK_RETURN"
else
HAVE_BLOCK_RETURN=""
fi
AC_SUBST(HAVE_BLOCK_RETURN)

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h semaphore.h arpa/inet.h netdb.h netinet/in.h sys/ioctl.h sys/socket.h sys/time.h], , [AC_MSG_ERROR([This header file is required.])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_HEADER_TIME

# Checks for library functions.
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([malloc gettimeofday inet_ntoa memmove strerror], , [AC_MSG_ERROR([This function is required.])])

HAVE_GETCWD=""
AC_CHECK_FUNCS([getcwd], [HAVE_GETCWD="-DHAVE_GETCWD"])
AC_SUBST(HAVE_GETCWD)

HAVE_PRCTL=""
AC_MSG_CHECKING([for prctl])
have_prctl="no"
AC_TRY_COMPILE([
#include <unistd.h>
#include <sys/prctl.h>
],[
prctl(1,2,3,4,5);
],have_prctl="yes")
AC_MSG_RESULT([$have_prctl])
if [[ "$have_prctl" = "yes" ]]; then
HAVE_PRCTL="-DHAVE_PRCTL"
fi
AC_SUBST(HAVE_PRCTL)

HAVE_SOCKADDR_LEN=""
AC_MSG_CHECKING([for sockaddr.sa_len presence])
AC_TRY_COMPILE([
#include <sys/socket.h>
],[
struct sockaddr saddr;
saddr.sa_len = sizeof(struct sockaddr);
],
have_sockaddr_len="yes",
have_sockaddr_len="no"
)
if [[ "$have_sockaddr_len" = "yes" ]]; then
HAVE_SOCKADDR_LEN="-DHAVE_SOCKADDR_LEN"
fi
AC_SUBST(HAVE_SOCKADDR_LEN)
AC_MSG_RESULT([$have_sockaddr_len])

HAVE_GMTOFF=""
AC_MSG_CHECKING([for tm.tm_gmtoff presence])
have_gmtoff="no"
AC_TRY_COMPILE([
#include <time.h>
],[
struct tm t;
t.tm_gmtoff = 3600;
],have_gmtoff="yes")
AC_MSG_RESULT([$have_gmtoff])
if [[ "$have_gmtoff" = "yes" ]]; then
HAVE_GMTOFF="-DHAVE_GMTOFF"
fi
AC_SUBST(HAVE_GMTOFF)

HAVE_INT_TZ=""
AC_MSG_CHECKING([for integer timezone variable])
have_tzint="no"
AC_TRY_COMPILE([
#include <time.h>
],[
long tz = -timezone;
],have_tzint="yes")
AC_MSG_RESULT([$have_tzint])
if [[ "$have_tzint" = "yes" ]]; then
HAVE_INT_TZ="-DHAVE_INT_TZ"
fi
AC_SUBST(HAVE_INT_TZ)

HAVE_POLL=no
AC_ARG_ENABLE(poll,AC_HELP_STRING([--enable-poll],[Use poll() on sockets (default: yes)]),want_poll=$enableval,want_poll=yes)
if [[ "x$want_poll" = "xyes" ]]; then
AC_MSG_CHECKING([for poll])
AC_TRY_COMPILE([#include <poll.h>
],[
struct pollfd fds;
poll(&fds,1,1);
],HAVE_POLL=yes)
AC_MSG_RESULT([$HAVE_POLL])
fi
AC_SUBST(HAVE_POLL)

AC_CACHE_SAVE

SAVE_LIBS="$LIBS"
# Checks for required libraries.
THREAD_LIB=""
AC_CHECK_LIB([pthread], [pthread_mutexattr_settype], [THREAD_LIB="-lpthread"])
if [[ "x$THREAD_LIB" = "x" ]]; then
AC_CHECK_LIB([c], [pthread_mutexattr_settype], , [AC_MSG_ERROR([This function is required.])])
fi
AC_SUBST(THREAD_LIB)

AC_MSG_CHECKING([for dlopen in default libs])
AC_LANG_SAVE
AC_LANG_C
AC_TRY_LINK([#include <dlfcn.h>],[dlopen("",0);],[have_dl=yes],[have_dl=no])
AC_LANG_RESTORE
AC_MSG_RESULT([$have_dl])
if [[ "x$have_dl" != "xyes" ]]; then
AC_CHECK_LIB([dl], [dlopen], , [AC_MSG_ERROR([This function is required.])])
DLOPEN_LIB="-ldl"
fi
AC_SUBST(DLOPEN_LIB)
# Restore the system libs
LIBS="$SAVE_LIBS"

MUTEX_HACK=""
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror"
SAVE_LIBS="$LIBS"
LIBS="$LIBS $THREAD_LIB"

AC_MSG_CHECKING([for pthread_mutexattr_settype declaration])
AC_TRY_COMPILE([
#include <unistd.h>
#include <pthread.h>
],[
pthread_mutexattr_t attr;
pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE_NP);
],
have_mutex_settype="yes",
have_mutex_settype="no"
)
if [[ "$have_mutex_settype" = "no" ]]; then
MUTEX_HACK="-DMUTEX_HACK"
fi
AC_MSG_RESULT([$have_mutex_settype])

AC_MSG_CHECKING([for pthread_mutex_timedlock])
AC_TRY_LINK([
#include <unistd.h>
#include <pthread.h>
#include <time.h>
],[
pthread_mutex_t mutex;
struct timespec tout;
pthread_mutex_timedlock(&mutex,&tout);
],
have_mutex_timedlock="yes",
have_mutex_timedlock="no"
)
if [[ "$have_mutex_timedlock" = "yes" ]]; then
MUTEX_HACK="$MUTEX_HACK -DHAVE_TIMEDLOCK"
fi
AC_MSG_RESULT([$have_mutex_timedlock])

AC_MSG_CHECKING([for sem_timedwait])
AC_TRY_LINK([
#include <semaphore.h>
#include <time.h>
],[
sem_t sem;
struct timespec tout;
sem_timedwait(&sem,&tout);
],
have_sem_timedwait="yes",
have_sem_timedwait="no"
)
if [[ "$have_sem_timedwait" = "yes" ]]; then
MUTEX_HACK="$MUTEX_HACK -DHAVE_TIMEDWAIT"
fi
AC_MSG_RESULT([$have_sem_timedwait])

CFLAGS="$SAVE_CFLAGS"
LIBS="$SAVE_LIBS"
AC_LANG_RESTORE
AC_SUBST(MUTEX_HACK)

NETDB_FLAGS=""
AC_CHECK_FUNC([inet_ntop],[NETDB_FLAGS="$NETDB_FLAGS -DHAVE_NTOP"])
AC_CHECK_FUNC([inet_pton],[NETDB_FLAGS="$NETDB_FLAGS -DHAVE_PTON"])
AC_CHECK_FUNC([gethostbyname_r],[NETDB_FLAGS="$NETDB_FLAGS -DHAVE_GHBN_R"])
AC_CHECK_FUNC([gethostbyname2_r],[NETDB_FLAGS="$NETDB_FLAGS -DHAVE_GHBN2_R"])
AC_CHECK_FUNC([gethostbyname2],[NETDB_FLAGS="$NETDB_FLAGS -DHAVE_GHBN2"])
AC_SUBST(NETDB_FLAGS)

THREAD_KILL=""
AC_MSG_CHECKING([for pthread_kill_other_threads_np declaration])
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror"
AC_TRY_COMPILE([
#include <unistd.h>
#include <pthread.h>
],[
pthread_kill_other_threads_np();
],
have_pthread_kill="yes",
have_pthread_kill="no"
)
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
if [[ "$have_pthread_kill" = "yes" ]]; then
THREAD_KILL="-DTHREAD_KILL"
fi
AC_MSG_RESULT([$have_pthread_kill])
AC_SUBST(THREAD_KILL)

# Check for compile options
INLINE_FLAGS=""
AC_ARG_ENABLE(inline,AC_HELP_STRING([--enable-inline],[Enable inlining of functions]),want_inline=$enableval,want_inline=auto)
AC_MSG_CHECKING([whether to use inline functions])
case "x$want_inline" in
    xyes)
	INLINE_FLAGS="-finline -Winline"
	;;
    xno)
	INLINE_FLAGS="-fno-inline"
	;;
    xauto)
	;;
    *)
	AC_ERROR([Invalid argument passed to --enable-inline])
	;;
esac
AC_MSG_RESULT([$want_inline])
AC_SUBST(INLINE_FLAGS)

# Check for atomic integer operations
ATOMIC_OPS=""
AC_ARG_ENABLE(atomics,AC_HELP_STRING([--enable-atomics],[Enable atomic integer operations (default: yes)]),want_atomics=$enableval,want_atomics=yes)
if [[ "x$want_atomics" != "xno" ]]; then
AC_MSG_CHECKING([whether to use atomic integer operations])
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror"
AC_TRY_LINK([],[
int i = 0;
int j = __sync_add_and_fetch(&i,1);
return __sync_fetch_and_sub(&j,1);
],
ATOMIC_OPS="-DATOMIC_OPS",
want_atomics="no (missing -march ?)"
)
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
AC_MSG_RESULT([$want_atomics])
fi
AC_SUBST(ATOMIC_OPS)


# Check for sse2 operations
SSE2_OPS=no
AC_ARG_ENABLE(sse2,AC_HELP_STRING([--enable-sse2],[Enable sse2 operations (default: no)]),want_sse2=$enableval,want_sse2=no)
AC_MSG_CHECKING([whether to use sse2 operations])
if [[ "x$want_sse2" != "xno" ]]; then
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror -msse2"
AC_TRY_LINK([#include <stdint.h>
#include <math.h>
#include<emmintrin.h>
],[
float ef[2][2];
const __m128 k1e_10f = _mm_set1_ps(1e-10f);
_mm_storeu_ps(&ef[0][0], k1e_10f);
return (int) ef[0][0];
],
SSE2_OPS="yes",
want_sse2="no (missing -march ?)"
)
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
fi
AC_MSG_RESULT([$want_sse2])
AC_SUBST(SSE2_OPS)


FDSIZE_HACK=""
AC_ARG_WITH(fdsize,AC_HELP_STRING([--with-fdsize=NNNN],[set FD_SIZE to NNNN (default 8192)]),[ac_cv_use_fdsize=$withval],[ac_cv_use_fdsize=8192])
if [[ "x$ac_cv_use_fdsize" != "xno" ]]; then
FDSIZE_HACK="-DFDSIZE_HACK=$ac_cv_use_fdsize"
AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILE([#include <features.h>
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
#include <bits/types.h>
#else
#error FD_SETSIZE cannot be set
#endif],[if (__FD_SETSIZE>10) return 0;],[],[FDSIZE_HACK=""])
AC_LANG_RESTORE
if [[ "x$FDSIZE_HACK" = "x" -a "x$HAVE_POLL" != "xyes" ]]; then
AC_ERROR([Cannot set FD_SETSIZE on this platform, use --without-fdsize])
fi
fi
if [[ "x$HAVE_POLL" = "xyes" ]]; then
FDSIZE_HACK="-DHAVE_POLL $FDSIZE_HACK"
fi
AC_SUBST(FDSIZE_HACK)

HAVE_SCTP=no
HAVE_SCTP_NETINET=no
SCTP_FLAGS=""
AC_ARG_ENABLE(sctp,AC_HELP_STRING([--enable-sctp],[Enable SCTP sockets (default: no)]),want_sctp=$enableval,want_sctp=no)
if [[ "x$want_sctp" = "xyes" ]]; then
AC_LANG_SAVE
AC_LANG_C
AC_MSG_CHECKING([for basic SCTP support])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>],[int fd = socket(PF_INET,SOCK_SEQPACKET,IPPROTO_SCTP);],[HAVE_SCTP=yes])
AC_MSG_RESULT([$HAVE_SCTP])
AC_MSG_CHECKING([for netinet/sctp.h usability])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>],[sctp_assoc_t assoc; int fd = socket(PF_INET,SOCK_SEQPACKET,IPPROTO_SCTP); assoc = 1;],[HAVE_SCTP_NETINET=yes])
AC_MSG_RESULT([$HAVE_SCTP_NETINET])
if [[ "x$HAVE_SCTP_NETINET" != "xno" ]]; then
# including netinet/sctp.h fixes SCTP support so enable it
HAVE_SCTP=yes
SCTP_FLAGS="-DHAVE_SCTP -DHAVE_SCTP_NETINET"
sctp_connectx_new=no
AC_MSG_CHECKING([whether sctp_connectx has 4 arguments])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>],[sctp_connectx(0,0,0,0);],[sctp_connectx_new=yes])
AC_MSG_RESULT([$sctp_connectx_new])
if [[ "x$sctp_connectx_new" = "xyes" ]]; then
SCTP_FLAGS="$SCTP_FLAGS -DHAVE_SCTP_CONNECTX_4"
fi
sctp_setadaptation=no
AC_MSG_CHECKING([whether sctp_setadaptation struct is present])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>],[struct sctp_setadaptation sctp_adapt;],[sctp_setadaptation=yes])
AC_MSG_RESULT([$sctp_setadaptation])
if [[ "x$sctp_setadaptation" = "xyes" ]]; then
SCTP_FLAGS="$SCTP_FLAGS -DHAVE_SETADAPTATION_STRUCT"
fi
sctp_sack_info=no
AC_MSG_CHECKING([whether sctp_sack_info struct is present])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>],[struct sctp_sack_info assoc_value;],[sctp_sack_info=yes])
AC_MSG_RESULT([$sctp_sack_info])
if [[ "x$sctp_sack_info" = "xyes" ]]; then
SCTP_FLAGS="$SCTP_FLAGS -DHAVE_SACK_INFO_STRUCT"
else
sctp_assoc_value=no
AC_MSG_CHECKING([whether sctp_assoc_value struct is present])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>],[struct sctp_assoc_value assoc_value;],[sctp_assoc_value=yes])
AC_MSG_RESULT([$sctp_assoc_value])
if [[ "x$sctp_assoc_value" = "xyes" ]]; then
SCTP_FLAGS="$SCTP_FLAGS -DHAVE_ASSOC_VALUE_STRUCT"
fi
fi
else
if [[ "x$HAVE_SCTP" != "xno" ]]; then
SCTP_FLAGS="-DHAVE_SCTP"
fi
fi
AC_LANG_RESTORE
fi
AC_SUBST(HAVE_SCTP)
AC_SUBST(HAVE_SCTP_NETINET)
AC_SUBST(SCTP_FLAGS)

# Checks for optional libraries.

HAVE_RESOLV=no
RESOLV_INC="-DNO_RESOLV"
RESOLV_LIB=""
AC_ARG_ENABLE(resolver,AC_HELP_STRING([--enable-resolver],[Use resolver if available (default: yes)]),want_resolver=$enableval,want_resolver=yes)
if [[ "x$want_resolver" = "xyes" ]]; then
AC_MSG_CHECKING([for resolver library])
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror $GCC_UNRESOLVED_SYMBOLS"
SAVE_LIBS="$LIBS"
AC_TRY_LINK([#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>],[res_init();],[HAVE_RESOLV=yes])
if [[ "x$HAVE_RESOLV" = "xno" ]]; then
LIBS="$LIBS -lresolv"
AC_TRY_LINK([#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>],[res_init();],
[HAVE_RESOLV=yes RESOLV_LIB=-lresolv])
fi
LIBS="$SAVE_LIBS"
if [[ "x$HAVE_RESOLV" != "xno" -a "x$RESOLV_LIB" = "x" ]]; then
AC_TRY_LINK([#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>],[unsigned char x[2]; res_query("",0,0,x,0);],[],[RESOLV_LIB=-lresolv])
fi
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
AC_MSG_RESULT([$HAVE_RESOLV $RESOLV_LIB])
if [[ "x$HAVE_RESOLV" = "xyes" ]]; then
RESOLV_INC="-DNO_DN_SKIPNAME"
HAVE_DN_SKIPNAME="no"
AC_MSG_CHECKING([for dn_skipname])
AC_LANG_SAVE
AC_LANG_C
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror $GCC_UNRESOLVED_SYMBOLS"
SAVE_LIBS="$LIBS"
LIBS="$LIBS $RESOLV_LIB"
AC_TRY_LINK([#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>],[unsigned char*p=0; dn_skipname(p,p);],
[HAVE_DN_SKIPNAME=yes RESOLV_INC=""])
LIBS="$SAVE_LIBS"
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
AC_MSG_RESULT([$HAVE_DN_SKIPNAME])
fi
fi
AC_SUBST(HAVE_RESOLV)
AC_SUBST(RESOLV_INC)
AC_SUBST(RESOLV_LIB)

INTERNAL_REGEX=yes
AC_ARG_ENABLE(internalregex,AC_HELP_STRING([--enable-internalregex],[Use internal regexp library (default: not on Linux)]),want_internalregex=$enableval,want_internalregex=auto)
AC_MSG_CHECKING([whether to use the internal regexp library])
case "x$want_internalregex" in
    xno|xdisable|xnot)
	INTERNAL_REGEX=no
	;;
    xyes|xenable)
	;;
    xauto)
	if [[ "x$uname_os" = "xLinux" ]]; then
	    INTERNAL_REGEX=no
	fi
	;;
    *)
	AC_ERROR([Invalid argument passed to --enable-internalregex])
	;;
esac
AC_MSG_RESULT([$INTERNAL_REGEX])
if [[ "x$INTERNAL_REGEX" != "xyes" ]]; then
AC_CHECK_FUNCS([regcomp], , [INTERNAL_REGEX=yes])
fi
AC_SUBST(INTERNAL_REGEX)

# check for Mac OS X Support
MACOSX_INC=""
HAVE_MACOSX_SUPPORT=no
OBJCFLAGS=""
if [[ "x$uname_os" = "xDarwin" ]]; then
save_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$CPPFLAGS -x objective-c++"
AC_MSG_CHECKING([for Mac OS X Objective-C support])
AC_TRY_COMPILE([#import <Foundation/Foundation.h>],
[
[NSFileManager defaultManager]
],
[
HAVE_MACOSX_SUPPORT="yes"
OBJCFLAGS="-x objective-c++"
MACOSX_INC="-DHAVE_MACOSX_SUPPORT"],[]
)
AC_MSG_RESULT([$HAVE_MACOSX_SUPPORT])
CPPFLAGS=$save_CPPFLAGS
fi
AC_SUBST(MACOSX_INC)
AC_SUBST(HAVE_MACOSX_SUPPORT)
AC_SUBST(OBJCFLAGS)


HAVE_PGSQL=no
PGSQL_INC=""
PGSQL_LIB=""
AC_ARG_WITH(libpq,AC_HELP_STRING([--with-libpq=DIR],[use Postgress SQL from DIR (default /usr)]),[ac_cv_use_libpq=$withval],[ac_cv_use_libpq=/usr])
if [[ "x$ac_cv_use_libpq" != "xno" ]]; then
AC_MSG_CHECKING([for Postgress SQL in $ac_cv_use_libpq])
local_lib="$ARCHLIB"
libpq="$ac_cv_use_libpq/$local_lib/libpq.so"
if [[ "x$local_lib" != "xlib" -a ! -f "$libpq" ]]; then
    local_lib="lib"
    libpq="$ac_cv_use_libpq/$local_lib/libpq.so"
fi
if [[ "x$ac_cv_use_libpq" != "x/usr" ]]; then
    PGSQL_LIB="-L$ac_cv_use_libpq/$local_lib -lpq"
else
    PGSQL_LIB="-lpq"
fi
for i in include include/pgsql pgsql/include include/postgresql; do
    incpq="$ac_cv_use_libpq/$i"
    test -f "$incpq/libpq-fe.h" && break
done
if [[ ! -f "$incpq/libpq-fe.h" ]]; then
    incpq="$incpq/pgsql"
fi
if [[ -f "$incpq/libpq-fe.h" ]]; then
if [[ -f "$libpq" ]]; then
HAVE_PGSQL=yes
else
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -I$incpq"
SAVE_LIBS="$LIBS"
LIBS="$LIBS -lpq"
AC_TRY_LINK([#include <libpq-fe.h>],[PQconnectStart("");],[HAVE_PGSQL=yes])
LIBS="$SAVE_LIBS"
CFLAGS="$SAVE_CFLAGS"
fi
if [[ "x$HAVE_PGSQL" = "xyes" ]]; then
    PGSQL_INC="-I$incpq"
fi
fi
AC_MSG_RESULT([$HAVE_PGSQL])
fi
AC_SUBST(HAVE_PGSQL)
AC_SUBST(PGSQL_INC)
AC_SUBST(PGSQL_LIB)

HAVE_MYSQL=no
MYSQL_INC=""
MYSQL_LIB=""
MYSQL_VER=""
AC_ARG_WITH(mysql,AC_HELP_STRING([--with-mysql=DIR],[use MySQL client library from DIR]),[ac_cv_use_mysql=$withval],[ac_cv_use_mysql=yes])
if [[ "x$ac_cv_use_mysql" != "xno" ]]; then
if [[ "x$ac_cv_use_mysql" = "xyes" ]]; then
AC_MSG_CHECKING([for MySQL using mysql_config])
MYSQL_INC=`(mysql_config --include) 2>/dev/null`
MYSQL_LIB=`(mysql_config --libs_r) 2>/dev/null`
MYSQL_VER=`(mysql_config --version) 2>/dev/null`
if [[ x`echo "$MYSQL_INC $MYSQL_LIB $MYSQL_VER" | wc -l | sed 's/^ *//g'` != "x1" ]]; then
MYSQL_INC=""
fi
if [[ ! -f `echo "$MYSQL_INC" | sed -n 's,^-I\([^ ]\+\)\( .*\)\?$,\1/mysql.h,p'` ]]; then
MYSQL_INC=""
fi
if [[ "x$MYSQL_INC" != "x" -a "x$MYSQL_LIB" != "x" -a "x$MYSQL_VER" != "x" ]]; then
HAVE_MYSQL=yes
else
MYSQL_INC=""
MYSQL_LIB=""
MYSQL_VER=""
fi
else
AC_MSG_CHECKING([for MySQL in $ac_cv_use_mysql])
local_lib="$ARCHLIB"
if [[ "x$local_lib" != "xlib" -a ! -d "$ac_cv_use_mysql/$local_lib/mysql" ]]; then
    local_lib="lib"
fi
if [[ -f "$ac_cv_use_mysql/include/mysql/mysql.h" -a -d "$ac_cv_use_mysql/$local_lib/mysql" ]]; then
MYSQL_INC="-I$ac_cv_use_mysql/include/mysql"
MYSQL_LIB="-L$ac_cv_use_mysql/$local_lib/mysql -lmysqlclient_r -lpthread -lz -lcrypt -lnsl -lm"
MYSQL_VER=`sed -n 's/^.*MYSQL_SERVER_VERSION[[^"]]*"\([[^"]]*\).*$/\1/p' "$ac_cv_use_mysql/include/mysql/mysql_version.h"`
HAVE_MYSQL=yes
fi
fi
AC_MSG_RESULT([$HAVE_MYSQL $MYSQL_VER])
if test "$HAVE_MYSQL" = "yes"; then
    save_CPPFLAGS=$CPPFLAGS
    CPPFLAGS="$CPPFLAGS $MYSQL_INC"
    AC_CHECK_DECLS([MYSQL_OPT_RECONNECT],[MYSQL_INC="$MYSQL_INC -DMYSQL_OPT_RECONNECT=MYSQL_OPT_RECONNECT"],,[#include<mysql.h>])
    AC_CHECK_DECLS([MYSQL_OPT_READ_TIMEOUT],[MYSQL_INC="$MYSQL_INC -DMYSQL_OPT_READ_TIMEOUT=MYSQL_OPT_READ_TIMEOUT"],,[#include<mysql.h>])
    AC_CHECK_DECLS([MYSQL_OPT_WRITE_TIMEOUT],[MYSQL_INC="$MYSQL_INC -DMYSQL_OPT_WRITE_TIMEOUT=MYSQL_OPT_WRITE_TIMEOUT"],,[#include<mysql.h>])
    AC_CHECK_DECLS([mysql_set_character_set],[MYSQL_INC="$MYSQL_INC -DHAVE_MYSQL_SET_CHARSET"],,[#include<mysql.h>])
    CPPFLAGS=$save_CPPFLAGS
fi
fi
AC_SUBST(HAVE_MYSQL)
AC_SUBST(MYSQL_INC)
AC_SUBST(MYSQL_LIB)
AC_SUBST(MYSQL_VER)

HAVE_SQLITE=no
SQLITE_INC=""
SQLITE_LIB=""
AC_ARG_WITH(sqlite,AC_HELP_STRING([--with-sqlite=DIR],[use SQLite library from DIR]),[ac_cv_use_sqlite=$withval],[ac_cv_use_sqlite=yes])
if [[ "x$ac_cv_use_sqlite" != "xno" ]]; then
if [[ "x$ac_cv_use_sqlite" = "xyes" ]]; then
AC_MSG_CHECKING([for SQLite using pkg-config])
SQLITE_INC=`(pkg-config --cflags sqlite3) 2>/dev/null`
SQLITE_LIB=`(pkg-config --libs sqlite3) 2>/dev/null`
if [[ "x$SQLITE_INC$SQLITE_LIB" = "x" ]]; then
SQLITE_INC=""
SQLITE_LIB=""
else
HAVE_SQLITE=yes
fi
else
AC_MSG_CHECKING([for SQLite in $ac_cv_use_sqlite])
if [[ -f "$ac_cv_use_sqlite/sqlite3.h" ]]; then
    SQLITE_INC="-I$ac_cv_use_sqlite"
    SQLITE_LIB="-lsqlite3"
    HAVE_SQLITE=yes
fi
fi
AC_MSG_RESULT([$HAVE_SQLITE])
fi
AC_SUBST(HAVE_SQLITE)
AC_SUBST(SQLITE_INC)
AC_SUBST(SQLITE_LIB)


HAVE_ZAP=no
ZAP_FLAGS=""
AC_ARG_ENABLE(dahdi,AC_HELP_STRING([--enable-dahdi],[Enable Dahdi driver (default: yes)]),want_dahdi=$enableval,want_dahdi=yes)
if [[ "x$want_dahdi" = "xyes" ]]; then
AC_MSG_CHECKING([for Dahdi generic headers])
AC_TRY_COMPILE([
#include <dahdi/user.h>
],[],
HAVE_ZAP="yes"
)
AC_MSG_RESULT([$HAVE_ZAP])
if [[ "x$HAVE_ZAP" = "xno" ]]; then
AC_ARG_ENABLE(zaptel,AC_HELP_STRING([--enable-zaptel],[Enable Zaptel driver (default: yes)]),want_zaptel=$enableval,want_zaptel=yes)
if [[ "x$want_zaptel" = "xyes" ]]; then
AC_MSG_CHECKING([for Zaptel generic headers])
AC_TRY_COMPILE([
#include <zaptel/zaptel.h>
#include <sys/ioctl.h>
],[ioctl(0,ZT_GETVERSION,(void*)0)],
HAVE_ZAP="yes"
ZAP_FLAGS="-DNEW_ZAPTEL_LOCATION -DHAVE_ZAP"
)
AC_MSG_RESULT([$HAVE_ZAP])
if [[ "x$HAVE_ZAP" = "xno" ]]; then
AC_MSG_CHECKING([for Zaptel linux headers])
AC_TRY_COMPILE([
#include <linux/zaptel.h>
#include <sys/ioctl.h>
],[ioctl(0,ZT_GETVERSION,(void*)0)],
HAVE_ZAP="yes"
ZAP_FLAGS="-DHAVE_ZAP"
)
AC_MSG_RESULT([$HAVE_ZAP])
fi
fi
fi
fi
AC_SUBST(HAVE_ZAP)
AC_SUBST(ZAP_FLAGS)

AC_ARG_ENABLE(wpcard,AC_HELP_STRING([--enable-wpcard],[Enable Wanpipe card driver (default: yes)]),BUILD_WPCARD=$enableval,BUILD_WPCARD=yes)
AC_ARG_ENABLE(tdmcard,AC_HELP_STRING([--enable-tdmcard],[Enable TDMV API card driver (default: yes)]),BUILD_TDMCARD=$enableval,BUILD_TDMCARD=yes)
AC_SUBST(BUILD_WPCARD)
AC_SUBST(BUILD_TDMCARD)

HAVE_WANPIPE=no
WANPIPE_FLAGS=""
AC_ARG_ENABLE(wanpipe,AC_HELP_STRING([--enable-wanpipe],[Enable Wanpipe support (default: yes)]),want_wanpipe=$enableval,want_wanpipe=yes)
if [[ "x$want_wanpipe" = "xyes" ]]; then
AC_MSG_CHECKING([for Wanpipe new generic headers])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -I/usr/include/wanpipe"
AC_TRY_COMPILE([
#define __LINUX__
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <if_wanpipe.h>
#include <wanpipe_defines.h>
#include <wanpipe_cfg.h>
#include <wanpipe.h>
#include <aft_core.h>
],[],
HAVE_WANPIPE="yes"
)
CFLAGS="$SAVE_CFLAGS"
AC_MSG_RESULT([$HAVE_WANPIPE])
if [[ "x$HAVE_WANPIPE" = "xyes" ]]; then
WANPIPE_FLAGS="-DNEW_WANPIPE_API"
else
AC_MSG_CHECKING([for Wanpipe old generic headers])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -I/usr/include/wanpipe"
AC_TRY_COMPILE([
#define __LINUX__
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <if_wanpipe.h>
#include <wanpipe_defines.h>
#include <wanpipe_cfg.h>
#include <wanpipe.h>
#include <sdla_aft_te1.h>
],[],
HAVE_WANPIPE="yes"
)
CFLAGS="$SAVE_CFLAGS"
AC_MSG_RESULT([$HAVE_WANPIPE])
fi
if [[ "x$HAVE_WANPIPE" = "xyes" ]]; then
WANPIPE_FLAGS="-DHAVE_WANPIPE_API -I/usr/include/wanpipe $WANPIPE_FLAGS"
else
AC_MSG_CHECKING([for Wanpipe linux headers])
AC_TRY_COMPILE([
#define __LINUX__
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe_defines.h>
#include <linux/wanpipe_cfg.h>
#include <linux/wanpipe.h>
#include <linux/sdla_aft_te1.h>
],[],
HAVE_WANPIPE="yes"
)
AC_MSG_RESULT([$HAVE_WANPIPE])
fi
fi
AC_SUBST(HAVE_WANPIPE)
HAVE_WANPIPE_TDM="no"
if [[ "x$HAVE_WANPIPE" = "xyes" ]]; then
AC_MSG_CHECKING([for Wanpipe TDM])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -I/usr/include/wanpipe"
AC_TRY_COMPILE([
#define __LINUX__
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <wanpipe.h>
],[wanpipe_api_t tdm;],
HAVE_WANPIPE_TDM="yes"
)
CFLAGS="$SAVE_CFLAGS"
AC_MSG_RESULT([$HAVE_WANPIPE_TDM])
fi
AC_SUBST(HAVE_WANPIPE_TDM)

AC_ARG_WITH(wphwec,AC_HELP_STRING([--with-wphwec=DIR],[use Wanpipe/Octasic headers from DIR]),[ac_cv_use_wphwec=$withval],[ac_cv_use_wphwec=/usr/include/wanpipe])
if [[ "x$HAVE_WANPIPE" = "xyes" ]]; then
if [[ "x$ac_cv_use_wphwec" != "xno" ]]; then
AC_MSG_CHECKING([for Wanpipe/Octasic headers])
if [[ -f "$ac_cv_use_wphwec/wanec_iface.h" -a -f "$ac_cv_use_wphwec/oct6100_api/oct6100api/oct6100_api.h" ]]; then
    WANPIPE_FLAGS="-DHAVE_WANPIPE_HWEC -I$ac_cv_use_wphwec/oct6100_api $WANPIPE_FLAGS"
    if [[ -f "$ac_cv_use_wphwec/wanec_iface_api.h" ]]; then
	WANPIPE_FLAGS="-DHAVE_WANPIPE_HWEC_API $WANPIPE_FLAGS"
    fi
    ac_cv_use_wphwec=yes
else
    ac_cv_use_wphwec=no
fi
AC_MSG_RESULT([$ac_cv_use_wphwec])
fi
fi
AC_SUBST(WANPIPE_FLAGS)

HAVE_SOUNDCARD=no
AC_MSG_CHECKING([for sound card headers])
AC_TRY_COMPILE([
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <fcntl.h>

#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined (__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif
],[],
HAVE_SOUNDCARD="yes"
)
AC_MSG_RESULT([$HAVE_SOUNDCARD])
AC_SUBST(HAVE_SOUNDCARD)

HAVE_ALSA=no
AC_MSG_CHECKING([for ALSA headers])
AC_TRY_COMPILE([
#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined (__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
],[],
HAVE_ALSA="yes"
)
AC_MSG_RESULT([$HAVE_ALSA])
AC_SUBST(HAVE_ALSA)

# check for Mac OS X audio headers
HAVE_COREAUDIO=no
if [[ "x$uname_os" = "xDarwin" ]]; then
AC_MSG_CHECKING([for Mac OS X audio headers])
AC_TRY_COMPILE([
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

AudioDeviceID getDefaultInputDevice()
{
    AudioDeviceID theAnswer = 0;
    UInt32 theSize = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress theAddress = { kAudioHardwarePropertyDefaultInputDevice,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    OSStatus theError = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theSize,
                                                   &theAnswer);
    return theAnswer;
}
],[],
[HAVE_COREAUDIO="yes"],[]
)
AC_MSG_RESULT([$HAVE_COREAUDIO])
fi
AC_SUBST(HAVE_COREAUDIO)

HAVE_GSM=no
GSM_INC=""
GSM_LIB="-lgsm"
AC_ARG_WITH(libgsm,AC_HELP_STRING([--with-libgsm],[use GSM codec if available (default)]),[ac_cv_use_libgsm=$withval],[ac_cv_use_libgsm=yes])
if [[ "x$ac_cv_use_libgsm" = "xstatic" ]]; then
    ac_cv_use_libgsm=yes
    GSM_LIB="-Wl,-Bstatic $GSM_LIB -Wl,-Bdynamic"
fi
if [[ "x$ac_cv_use_libgsm" = "xyes" ]]; then
for i in /usr/include /usr/include/gsm /usr/local/include /usr/local/include/gsm; do
    ac_cv_use_libgsm="$i"
    test -f "$ac_cv_use_libgsm/gsm.h" && break
done
fi
if [[ "x$ac_cv_use_libgsm" != "xno" ]]; then
AC_MSG_CHECKING([for gsm.h in $ac_cv_use_libgsm])
if [[ -f "$ac_cv_use_libgsm/gsm.h" ]]; then
    HAVE_GSM=yes
    GSM_INC="-I$ac_cv_use_libgsm"
fi
AC_MSG_RESULT([$HAVE_GSM])
fi
AC_SUBST(HAVE_GSM)
AC_SUBST(GSM_INC)
AC_SUBST(GSM_LIB)


HAVE_ILBC=no
ILBC_INC=""
AC_ARG_ENABLE(ilbc,AC_HELP_STRING([--enable-ilbc],[Enable iLBC codec (default: yes)]),want_ilbc=$enableval,want_ilbc=yes)
if [[ "x$want_ilbc" = "xyes" ]]; then
AC_MSG_CHECKING([for iLBC reference in libs])
	basedir=`cd "$srcdir" && pwd`
	incilbc="libs/ilbc"
        if [[ -f "$basedir/$incilbc/iLBC_encode.h" ]]; then
		HAVE_ILBC=yes
		basedir=`echo "$basedir" | sed 's/\([[^\]]\)\([[[:space:]\$\"'\'']]\)/\1\\\2/g'`
		ILBC_INC="-I$basedir/$incilbc"
	fi
AC_MSG_RESULT([$HAVE_ILBC])
fi
AC_SUBST(HAVE_ILBC)
AC_SUBST(ILBC_INC)

HAVE_ILBCWR=no
ILBCWR_INC=""
AC_ARG_ENABLE(ilbc-webrtc,AC_HELP_STRING([--enable-ilbc-webrtc],[Enable iLBC webrtc codec (default: yes)]),want_ilbc_webrtc=$enableval,want_ilbc_webrtc=yes)
if [[ "x$want_ilbc_webrtc" = "xyes" ]]; then
AC_MSG_CHECKING([for iLBC miniwebrtc in libs])
	basedir=`cd "$srcdir" && pwd`
	incilbc="libs/miniwebrtc/audio/coding_ilbc"
        if [[ -f "$basedir/$incilbc/ilbc.h" ]]; then
		HAVE_ILBCWR=yes
		basedir=`echo "$basedir" | sed 's/\([[^\]]\)\([[[:space:]\$\"'\'']]\)/\1\\\2/g'`
		ILBCWR_INC="-I$basedir/$incilbc"
	fi
AC_MSG_RESULT([$HAVE_ILBCWR])
fi
AC_SUBST(HAVE_ILBCWR)
AC_SUBST(ILBCWR_INC)

HAVE_ISAC=no
ISAC_INC=""
AC_ARG_ENABLE(isac-float,AC_HELP_STRING([--enable-isac-float],[Enable iSAC float codec (default: yes)]),want_isac=$enableval,want_isac=yes)
if [[ "x$want_isac" = "xyes" ]]; then
AC_MSG_CHECKING([for iSAC float in libs])
	basedir=`cd "$srcdir" && pwd`
	incisac="libs/miniwebrtc/audio/coding_isac/main"
        if [[ -f "$basedir/$incisac/isac.h" ]]; then
		HAVE_ISAC=yes
		basedir=`echo "$basedir" | sed 's/\([[^\]]\)\([[[:space:]\$\"'\'']]\)/\1\\\2/g'`
		ISAC_INC="-I$basedir/$incisac"
	fi
AC_MSG_RESULT([$HAVE_ISAC])
fi

AC_ARG_ENABLE(isac-fixed,AC_HELP_STRING([--enable-isac-fixed],[Enable iSAC fixed codec (default: no)]),want_isac_fixed=$enableval,want_isac_fixed=no)
if [[ "x$want_isac_fixed" = "xyes" ]]; then
	if [[ x$HAVE_ISAC = "xyes" ]]; then
	    AC_ERROR([iSAC Fixed and iSAC Float can not be enabled both at the same time])
	else
AC_MSG_CHECKING([for iSAC fixed in libs])
		basedir=`cd "$srcdir" && pwd`
		incisac="libs/miniwebrtc/audio/coding_isac/fix"
		if [[ -f "$basedir/$incisac/isacfix.h" ]]; then
			HAVE_ISAC=yes
			basedir=`echo "$basedir" | sed 's/\([[^\]]\)\([[[:space:]\$\"'\'']]\)/\1\\\2/g'`
			ISAC_INC="-I$basedir/$incisac -DISAC_FIXED"
		fi
	fi
AC_MSG_RESULT([$HAVE_ISAC])
fi
AC_SUBST(HAVE_ISAC)
AC_SUBST(ISAC_INC)

HAVE_SPEEX=no
SPEEX_INC=""
SPEEX_LIB="-lspeex"
AC_ARG_WITH(libspeex,AC_HELP_STRING([--with-libspeex],[use Speex codec if available (default)]),[ac_cv_use_libspeex=$withval],[ac_cv_use_libspeex=yes])
if [[ "x$ac_cv_use_libspeex" = "xstatic" ]]; then
    ac_cv_use_libspeex=yes
    SPEEX_LIB="-Wl,-Bstatic $SPEEX_LIB -Wl,-Bdynamic"
fi
if [[ "x$ac_cv_use_libspeex" = "xyes" ]]; then
for i in /usr/include /usr/local/include; do
    ac_cv_use_libspeex="$i"
    test -f "$ac_cv_use_libspeex/speex/speex.h" && break
done
fi
if [[ "x$ac_cv_use_libspeex" != "xno" ]]; then
AC_MSG_CHECKING([for speex.h in $ac_cv_use_libspeex/speex])
if [[ -f "$ac_cv_use_libspeex/speex/speex.h" ]]; then
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror -I$ac_cv_use_libspeex"
AC_TRY_COMPILE([
#include <speex/speex.h>
],[
int i = SPEEX_MODEID_UWB;
speex_lib_ctl(SPEEX_LIB_GET_MAJOR_VERSION, &i);
],
HAVE_SPEEX=yes
)
CFLAGS="$SAVE_CFLAGS"
if [[ "x$HAVE_SPEEX" = "xyes" ]]; then
    SPEEX_INC="-I$ac_cv_use_libspeex"
fi
fi
AC_MSG_RESULT([$HAVE_SPEEX])
fi
AC_SUBST(HAVE_SPEEX)
AC_SUBST(SPEEX_INC)
AC_SUBST(SPEEX_LIB)


HAVE_AMRNB=no
AMRNB_INC=""
AMRNB_LIB="-lamrnb"
AC_ARG_WITH(amrnb,AC_HELP_STRING([--with-amrnb=DIR],[use AMR-NB if available (default)]),[ac_cv_use_amrnb=$withval],[ac_cv_use_amrnb=/usr])
if [[ "x$ac_cv_use_amrnb" = "xstatic" ]]; then
    ac_cv_use_amrnb=/usr
    AMRNB_LIB="-Wl,-Bstatic $AMRNB_LIB -Wl,-Bdynamic"
fi
if [[ "x$ac_cv_use_amrnb" != "xno" ]]; then
AC_MSG_CHECKING([for AMR-NB in $ac_cv_use_amrnb])
local_lib="$ARCHLIB"
amrinc="$ac_cv_use_amrnb/include/amrnb"
test -f "$ac_cv_use_amrnb/$local_lib/libamrnb.so" || local_lib="lib"
if [[ -f "$ac_cv_use_amrnb/$local_lib/libamrnb.so" -a -f "$amrinc/interf_rom.h" ]]; then
    HAVE_AMRNB=yes
    AMRNB_LIB="-L$ac_cv_use_amrnb/$local_lib $AMRNB_LIB"
    AMRNB_INC="-I$amrinc"
fi
AC_MSG_RESULT([$HAVE_AMRNB])
fi
AC_SUBST(HAVE_AMRNB)
AC_SUBST(AMRNB_INC)
AC_SUBST(AMRNB_LIB)


HAVE_SPANDSP=no
SPANDSP_INC=""
SPANDSP_LIB="-lspandsp"
AC_ARG_WITH(spandsp,AC_HELP_STRING([--with-spandsp],[use spandsp library if available (default)]),[ac_cv_use_spandsp=$withval],[ac_cv_use_spandsp=yes])
if [[ "x$ac_cv_use_spandsp" = "xstatic" ]]; then
    ac_cv_use_spandsp="yes"
    SPANDSP_LIB="-Wl,-Bstatic $SPANDSP_LIB -ltiff -lz -ljpeg -Wl,-Bdynamic"
fi
if [[ "x$ac_cv_use_spandsp" = "xyes" ]]; then
for i in /usr/include /usr/local/include; do
    ac_cv_use_spandsp="$i"
    test -f "$ac_cv_use_spandsp/spandsp.h" && break
done
fi
if [[ "x$ac_cv_use_spandsp" != "xno" ]]; then
AC_MSG_CHECKING([for usable spandsp in $ac_cv_use_spandsp])
if [[ -f "$ac_cv_use_spandsp/spandsp.h" ]]; then
AC_LANG_SAVE
AC_LANG([C++])
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Werror"
AC_TRY_COMPILE([
#define __STDC_LIMIT_MACROS
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <spandsp.h>
],[
fax_state_t fax_state;
t30_stats_t fax_stats;
t38_terminal_state_t t38_t_state;
t38_gateway_state_t t38_g_state;
fax_stats.pages_tx = fax_stats.pages_rx = 0;
fax_init(&fax_state,TRUE);
t30_set_ecm_capability(fax_get_t30_state(&fax_state),1);
t30_set_supported_compressions(fax_get_t30_state(&fax_state),T30_SUPPORT_T6_COMPRESSION);
t30_set_tx_file(fax_get_t30_state(&fax_state),"abc",-1,-1);
t30_set_tx_ident(fax_get_t30_state(&fax_state),"xyz");
fax_set_transmit_on_idle(&fax_state,1);
t38_terminal_init(&t38_t_state,TRUE,NULL,NULL);
t38_gateway_init(&t38_g_state,NULL,NULL);
t38_terminal_release(&t38_t_state);
],
HAVE_SPANDSP="yes"
)
if [[ "x$HAVE_SPANDSP" = "xyes" ]]; then
SPANDSP_INC="-I$ac_cv_use_spandsp -DSPANDSP_TXRXSTATS"
else
AC_TRY_COMPILE([
#define __STDC_LIMIT_MACROS
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <spandsp.h>
],[
fax_state_t fax_state;
t38_terminal_state_t t38_t_state;
t38_gateway_state_t t38_g_state;
fax_init(&fax_state,TRUE);
t30_set_ecm_capability(fax_get_t30_state(&fax_state),1);
t30_set_supported_compressions(fax_get_t30_state(&fax_state),T30_SUPPORT_T6_COMPRESSION);
t30_set_tx_file(fax_get_t30_state(&fax_state),"abc",-1,-1);
t30_set_tx_ident(fax_get_t30_state(&fax_state),"xyz");
fax_set_transmit_on_idle(&fax_state,1);
t38_terminal_init(&t38_t_state,TRUE,NULL,NULL);
t38_gateway_init(&t38_g_state,NULL,NULL);
t38_terminal_release(&t38_t_state);
],
HAVE_SPANDSP="yes"
)
if [[ "x$HAVE_SPANDSP" = "xyes" ]]; then
SPANDSP_INC="-I$ac_cv_use_spandsp"
else
AC_TRY_COMPILE([
#define __STDC_LIMIT_MACROS
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <spandsp.h>
],[
#define fax_get_t30_state(x) (&(x)->t30_state)
fax_state_t fax_state;
t38_terminal_state_t t38_t_state;
t38_gateway_state_t t38_g_state;
fax_init(&fax_state,TRUE);
t30_set_ecm_capability(fax_get_t30_state(&fax_state),1);
t30_set_supported_compressions(fax_get_t30_state(&fax_state),T30_SUPPORT_T6_COMPRESSION);
t30_set_tx_file(fax_get_t30_state(&fax_state),"abc",-1,-1);
t30_set_tx_ident(fax_get_t30_state(&fax_state),"xyz");
fax_set_transmit_on_idle(&fax_state,1);
t38_terminal_init(&t38_t_state,TRUE,NULL,NULL);
t38_gateway_init(&t38_g_state,NULL,NULL);
t38_terminal_release(&t38_t_state);
],
HAVE_SPANDSP="yes"
)
if [[ "x$HAVE_SPANDSP" = "xyes" ]]; then
SPANDSP_INC="-I$ac_cv_use_spandsp -DSPANDSP_PRE006"
fi
fi
fi
CFLAGS="$SAVE_CFLAGS"
AC_LANG_RESTORE
fi
AC_MSG_RESULT([$HAVE_SPANDSP])
fi
AC_SUBST(HAVE_SPANDSP)
AC_SUBST(SPANDSP_INC)
AC_SUBST(SPANDSP_LIB)


HAVE_PWLIB=no
PWLIB_RTTI=none
PWLIB_INC=""
PWLIB_LIB=""
PWLIB_RUN=""
AC_ARG_WITH(pwlib,AC_HELP_STRING([--with-pwlib=DIR],[use Pwlib from DIR (default /usr)]),[ac_cv_use_pwlib=$withval],[ac_cv_use_pwlib=/usr])

if [[ "x$ac_cv_use_pwlib" != "xno" ]]; then
if [[ "x$ac_cv_use_pwlib" = "x/usr" -a ! -f "$ac_cv_use_pwlib/include/ptlib.h" ]]; then
    ac_cv_use_pwlib="$ac_cv_use_pwlib/local"
fi
AC_MSG_CHECKING([for Pwlib in $ac_cv_use_pwlib])
local_lib="$ARCHLIB"
ptconfig="$ac_cv_use_pwlib/bin/ptlib-config"
test -x "$ptconfig" || ptconfig="$ac_cv_use_pwlib/make/ptlib-config"
test -x "$ptconfig" || ptconfig="$ac_cv_use_pwlib/share/ptlib/make/ptlib-config"
verpw=`$ptconfig --version 2>/dev/null`
prtti=`$ptconfig --ccflags 2>/dev/null | sed -n 's/^.*\(-f[[^ ]]*rtti\).*$/\1/p'`
# try first installed directory
incpw="$ac_cv_use_pwlib/include/ptlib.h"
libpw="$ac_cv_use_pwlib/$local_lib/libpt.so"
if [[ "x$local_lib" != "xlib" -a ! -f "$libpw" ]]; then
    local_lib="lib"
    libpw="$ac_cv_use_pwlib/$local_lib/libpt.so"
fi
if [[ "x$verpw" = "x" ]]; then
    verpw=`cat "$ac_cv_use_pwlib/include/ptbuildopts.h" 2>/dev/null | sed -n 's/^.*PWLIB_VERSION "\([[^"]]*\)".*$/\1/p'`
fi
vpw=`echo "$verpw" | $csed "$vsed" | $csed "$vsed"`
flgpw=""
PWLIB_INC="-I$ac_cv_use_pwlib/include" # Base dir, ok for ptlib
if [[ "$vpw" '<' "02.00.00" ]]; then
    # Pwlib-1.xm no ptlib (2.x) install into ptlib
    PWLIB_INC="$PWLIB_INC/ptlib"
    if [[ "$vpw" '<' "01.06.00" ]]; then
        # Very old pwlib needs more includes.
        PWLIB_INC="$PWLIB_INC/unix/ptlib $PWLIB_INC/unix $PWLIB_INC"
        flgpw="-DPHAS_TEMPLATES -D_REENTRANT -DP_HAS_SEMAPHORES"
    fi
fi
if [[ "x$flgpw" = "x" ]]; then
    flgpw="`$ptconfig --ccflags 2>/dev/null`"
    if [[ "x$flgpw" = "x" ]]; then
	# Last resort, minimal guess
	flgpw="-D_REENTRANT"
    fi
fi
PWLIB_INC="$PWLIB_INC $flgpw"

if [[ -f "$incpw" -a -f "$libpw" ]]; then
    HAVE_PWLIB=installed
    PWLIB_LIB="-L$ac_cv_use_pwlib/$local_lib -lpt"
    case "x$prtti" in
	x-frtti)
	    PWLIB_RTTI=yes
	    ;;
	x-fno-rtti)
	    PWLIB_RTTI=no
	    ;;
    esac
else
# try source directory style
    libpw=`echo "$ac_cv_use_pwlib/lib/"libpt*r.so`
    test -f "$libpw" || libpw=`echo "$ac_cv_use_pwlib/"lib*/libpt.so`
    if [[ -f "$incpw" -a -f "$libpw" ]]; then
	prtti=`"$ptconfig" --ccflags 2>/dev/null | sed -n 's/^.*\(-f[[^ ]]*rtti\).*$/\1/p'`
        HAVE_PWLIB=sources
	if [[ "$vpw" '<' "02.00.00" ]]; then
	    PWLIB_INC="-I$ac_cv_use_pwlib/include $PWLIB_INC"
	fi
        PWLIB_LIB=`echo "$libpw"|sed 's,^\(.*\)/lib,-L\1 -l,; s,\.so$,,'`
        PWLIB_RUN=":"`echo "$libpw"|sed 's,/[[^/]]*$,,'`
	case "x$prtti" in
	    x-frtti)
		PWLIB_RTTI=yes
		;;
	    x-fno-rtti)
		PWLIB_RTTI=no
		;;
	esac
    fi
fi
AC_MSG_RESULT([$HAVE_PWLIB $verpw RTTI: $PWLIB_RTTI])
fi


HAVE_H323=no
H323_INC=""
H323_LIB=""
H323_RUN=""
AC_ARG_WITH(openh323,AC_HELP_STRING([--with-openh323=DIR],[use OpenH323 from DIR (default /usr)]),[ac_cv_use_openh323=$withval],[ac_cv_use_openh323=/usr])

if [[ "x$HAVE_PWLIB" != "xno" -a "x$ac_cv_use_openh323" != "xno" ]]; then
if [[ "x$ac_cv_use_openh323" = "x/usr" -a ! -f "$ac_cv_use_openh323/include/openh323/h323.h" ]]; then
    ac_cv_use_openh323="$ac_cv_use_openh323/local"
fi
AC_MSG_CHECKING([for OpenH323 in $ac_cv_use_openh323])
local_lib="$ARCHLIB"
# try first installed directory
ver323=""
inc323="$ac_cv_use_openh323/include/openh323/h323.h"
lib323="$ac_cv_use_openh323/$local_lib/libopenh323.so"
if [[ "x$local_lib" != "xlib" -a ! -f "$lib323" ]]; then
    local_lib="lib"
    lib323="$ac_cv_use_openh323/$local_lib/libopenh323.so"
fi
if [[ -f "$inc323" -a -f "$lib323" ]]; then
    ver323=`sed -n 's/^.*OPENH323_VERSION.*"\([[^"]]\+\)".*$/\1/p' "$ac_cv_use_openh323/include/openh323/openh323buildopts.h" 2>/dev/null`
    HAVE_H323=installed
    H323_INC="-I$ac_cv_use_openh323/include/openh323"
    H323_LIB="-L$ac_cv_use_openh323/$local_lib -lopenh323"
else
# try source directory style
    inc323="$ac_cv_use_openh323/include/h323.h"
    lib323=`echo "$ac_cv_use_openh323/lib/"libh323*r.so`
    test -f "$lib323" || lib323=`echo "$ac_cv_use_openh323/lib/"libh323*_.so`
    if [[ -f "$inc323" -a -f "$lib323" ]]; then
	ver323=`sed -n 's/^.*OPENH323_VERSION.*"\([[^"]]\+\)".*$/\1/p' "$ac_cv_use_openh323/include/openh323buildopts.h" 2>/dev/null`
        HAVE_H323=sources
        H323_INC="-I$ac_cv_use_openh323/include"
        H323_LIB="-L$ac_cv_use_openh323/lib -l`echo "$lib323"|sed 's,^.*/lib,,; s,\.so$,,'`"
        H323_RUN=":$ac_cv_use_openh323/lib"
    fi
fi
AC_MSG_RESULT([$HAVE_H323 $ver323])
fi

if [[ "x$HAVE_H323" != "xno" ]]; then
    H323_INC="$PWLIB_INC $H323_INC"
    H323_LIB="$H323_LIB $PWLIB_LIB"
    H323_RUN="$PWLIB_RUN$H323_RUN"
fi
AC_SUBST(HAVE_H323)
AC_SUBST(H323_INC)
AC_SUBST(H323_LIB)
AC_SUBST(H323_RUN)


HAVE_OPENSSL=no
OPENSSL_INC=""
OPENSSL_LIB=""
OPENSSL_VER=""
AC_ARG_WITH(openssl,AC_HELP_STRING([--with-openssl],[use OpenSSL for secure sockets (default)]),[ac_cv_use_openssl=$withval],[ac_cv_use_openssl=yes])
if [[ "x$ac_cv_use_openssl" = "xyes" ]]; then
    AC_MSG_CHECKING([for OpenSSL using pkg-config])
    pkg="openssl"
    verssl=`(pkg-config --modversion $pkg) 2>/dev/null`
    if [[ "x$verssl" = "x" ]]; then
	pkg="libssl"
	verssl=`(pkg-config --modversion $pkg) 2>/dev/null`
    fi
    incssl=`(pkg-config --cflags $pkg) 2>/dev/null`
    libssl=`(pkg-config --libs $pkg) 2>/dev/null`
    if [[ "x$libssl" != "x" ]]; then
	HAVE_OPENSSL=yes
	OPENSSL_VER="$verssl"
	OPENSSL_INC="$incssl"
	OPENSSL_LIB="$libssl"
    else
	verssl="no"
    fi
    AC_MSG_RESULT([$verssl])
elif [[ "x$ac_cv_use_openssl" != "xno" ]]; then
    AC_MSG_CHECKING([for OpenSSL in $ac_cv_use_openssl])
    verssl=no
    if [[ -f "$ac_cv_use_openssl/include/openssl/opensslconf.h" ]]; then
	verssl=`sed -n 's/^#define SHLIB_VERSION_NUMBER "\([0-9.]\+\)".*/\1/p' <$ac_cv_use_openssl/include/openssl/opensslv.h 2>/dev/null`
	if [[ "x$verssl" != "x" ]]; then
	    HAVE_OPENSSL=yes
	    OPENSSL_VER="$verssl"
	    OPENSSL_INC="-I$ac_cv_use_openssl/include"
	    OPENSSL_LIB="-lssl -lcrypto"
	else
	    verssl=no
	fi
    fi
    AC_MSG_RESULT([$verssl])
fi
AC_SUBST(HAVE_OPENSSL)
AC_SUBST(OPENSSL_INC)
AC_SUBST(OPENSSL_LIB)
AC_SUBST(OPENSSL_VER)


HAVE_ZLIB=no
ZLIB_INC=""
ZLIB_LIB=""
AC_ARG_WITH(zlib,AC_HELP_STRING([--with-zlib=DIR],[use zlib for data (de)compression from DIR (default /usr)]),[ac_cv_use_zlib=$withval],[ac_cv_use_zlib=/usr])
if [[ "x$ac_cv_use_zlib" != "xno" ]]; then
    AC_MSG_CHECKING([for zlib in $ac_cv_use_zlib])
    if [[ -f "$ac_cv_use_zlib/include/zlib.h" ]]; then
	# Save current system libs and include dirs and restore them after trying to link
	SAVE_LIBS="$LIBS"
	SAVE_CFLAGS="$CFLAGS"
	LIBS="$LIBS -lz"
	CFLAGS="$CFLAGS -I$ac_cv_use_zlib/include"
	AC_TRY_LINK([#include <zlib.h>],[inflateInit2(0,0);],[HAVE_ZLIB=yes],[HAVE_ZLIB=no])
	LIBS="$SAVE_LIBS"
	CFLAGS="$SAVE_CFLAGS"
	if [[ "$HAVE_ZLIB" = "yes" ]]; then
	    ZLIB_INC="-I$ac_cv_use_zlib/include"
	    ZLIB_LIB="-lz"
	fi
    fi
    AC_MSG_RESULT([$HAVE_ZLIB])
fi
AC_SUBST(HAVE_ZLIB)
AC_SUBST(ZLIB_INC)
AC_SUBST(ZLIB_LIB)


HAVE_LIBUSB=no
LIBUSB_INC=""
LIBUSB_LIB=""
AC_ARG_WITH(libusb,AC_HELP_STRING([--with-libusb=DIR],[use libusb DIR (default /usr)]),[ac_cv_use_libusb=$withval],[ac_cv_use_libusb=/usr])
if [[ "x$ac_cv_use_libusb" != "xno" ]]; then
    AC_MSG_CHECKING([for libusb in $ac_cv_use_libusb])
    if [[ -f "$ac_cv_use_libusb/include/libusb-1.0/libusb.h" ]]; then
	# Save current system libs and include dirs and restore them after trying to link
	SAVE_LIBS="$LIBS"
	SAVE_CFLAGS="$CFLAGS"
	LIBS="$LIBS -lusb-1.0"
	CFLAGS="$CFLAGS -I$ac_cv_use_libusb/include/libusb-1.0"
	AC_TRY_LINK([#include <libusb.h>],[libusb_has_capability(0);],[HAVE_LIBUSB=yes],[HAVE_LIBUSB=no])
	if [[ "$HAVE_LIBUSB" = "yes" ]]; then
	    LIBUSB_INC="-I$ac_cv_use_libusb/include/libusb-1.0"
	    LIBUSB_LIB="-lusb-1.0"
	    AC_TRY_LINK([#include <libusb.h>],[libusb_get_version();],[LIBUSB_INC="-DHAVE_LIBUSB_VER $LIBUSB_INC"])
	fi
	LIBS="$SAVE_LIBS"
	CFLAGS="$SAVE_CFLAGS"
    fi
    AC_MSG_RESULT([$HAVE_LIBUSB])
fi
AC_SUBST(HAVE_LIBUSB)
AC_SUBST(LIBUSB_INC)
AC_SUBST(LIBUSB_LIB)


HAVE_QT4=no
QT4_INC=""
QT4_LIB=""
QT4_INC_NET=""
QT4_LIB_NET=""
QT4_MOC=""
QT4_VER=""
QT4_STATIC_MODULES=""
AC_ARG_WITH(libqt4,AC_HELP_STRING([--with-libqt4],[use Qt for graphical clients (default)]),[ac_cv_use_libqt4=$withval],[ac_cv_use_libqt4=yes])
AC_ARG_WITH(qtstatic,AC_HELP_STRING([--with-qtstatic=MODULES],[link specific modules with static Qt]),[ac_cv_use_qtstatic=$withval],[ac_cv_use_qtstatic=no])
qtstatic=""
if [[ "x$ac_cv_use_qtstatic" != "xno" ]]; then
    qtstatic="--static"
fi
QT4_STATIC_MODULES=`echo "$ac_cv_use_qtstatic" | sed 's/,/ /g'`
if [[ "x$ac_cv_use_libqt4" = "xyes" ]]; then
    AC_MSG_CHECKING([for Qt4 >= 4.3.0 using pkg-config])
    pkgd="/usr/lib/qt4/$ARCHLIB/pkgconfig"
    verqt=`(pkg-config --modversion QtCore) 2>/dev/null`
    if [[ -z "$verqt" -a -d "$pkgd" ]]; then
	export PKG_CONFIG_LIBDIR="$pkgd"
	verqt=`(pkg-config --modversion QtCore) 2>/dev/null`
    fi
    incqt=`(pkg-config --cflags QtNetwork QtGui QtXml QtCore) 2>/dev/null | sed 's/QtNetwork/QtUiTools/'`
    libqt=`(pkg-config --libs $qtstatic QtNetwork QtGui QtXml QtCore) 2>/dev/null | sed 's/QtNetwork/QtUiTools/'`
    if [[ "x$incqt" != "x" -a "x$libqt" != "x" ]]; then
	HAVE_QT4=yes
	QT4_INC="$incqt"
	QT4_LIB="$libqt"
	QT4_INC_NET=`(pkg-config --cflags QtNetwork) 2>/dev/null | grep -o '[[^ ]]*QtNetwork[[^ ]]*'`
	QT4_LIB_NET=`(pkg-config --libs QtNetwork) 2>/dev/null | grep -o '[[^ ]]*QtNetwork[[^ ]]*'`
	QT4_MOC=`echo "$incqt" | sed -n 's,^.*-I\([[^ ]]\+\)/include .*$,\1/bin/moc,p'`
	test -z "$QT4_MOC" && QT4_MOC=`(which moc-qt4) 2>/dev/null`
	test -z "$QT4_MOC" && QT4_MOC="moc"
	QT4_VER=`echo "$verqt" | $csed "$vsed" | $csed "$vsed" | sed 's/\(..\)\.\(..\)\.\(..\)/\1\2\3/'`
	ac_cv_use_libqt="no"
	if [[ 1$QT4_VER -lt 1040300 ]]; then
	    HAVE_QT4=no
	    verqt="too old ($verqt)"
	fi
    else
	verqt="no"
    fi
    unset PKG_CONFIG_LIBDIR
    AC_MSG_RESULT([$verqt])

    if [[ "x$HAVE_QT4" = "xno" ]]; then
	AC_MSG_CHECKING([for Qt4 >= 4.3.0 using qmake])
	incqt=`(qmake -query QT_INSTALL_HEADERS) 2>/dev/null`
	libqt=`(qmake -query QT_INSTALL_LIBS) 2>/dev/null`
	if [[ "x$incqt" = "x**Unknown**" -o "x$libqt" = "x**Unknown**" ]]; then
	    incqt=""
	    libqt=""
	fi
	if [[ "x$incqt" != "x" -a "x$libqt" != "x" ]]; then
	    HAVE_QT4=yes
	    QT4_INC="-I$incqt -I$incqt/QtUiTools -I$incqt/QtGui -I$incqt/QtXml -I$incqt/QtCore"
	    case "$uname_os" in
		*Darwin)
		    QT4_INC="-D__USE_WS_X11__ $QT4_INC"
		    ;;
	    esac
	    QT4_LIB="-L$libqt -lQtUiTools -lQtGui -lQtXml -lQtCore"
	    QT4_INC_NET="-I$incqt/QtNetwork"
	    QT4_LIB_NET="-L$libqt -lQtNetwork"
	    case "$uname_os" in
		*Darwin)
		    framework=`(ls "$libqt" | grep QtGui.framework) 2>/dev/null`
		    if [[ "x$framework" != "x" ]]; then
			QT4_INC="-I$incqt  -I$incqt/QtUiTools -I$libqt/QtGui.framework/Headers -I$libqt/QtXml.framework/Headers -I$libqt/QtCore.framework/Headers"
			QT4_LIB="-L$libqt -F$libqt -lQtUiTools -framework QtGui -framework QtXml -framework QtCore"
			QT4_INC_NET="-I$libqt/QtNetwork.framework/Headers"
			QT4_LIB_NET="-framework QtNetwork"
		    fi
		    ;;
	    esac
	    QT4_MOC=`(qmake -query QT_INSTALL_BINS) 2>/dev/null`
	    QT4_MOC="$QT4_MOC/moc"
	    verqt=`(qmake -query QT_VERSION) 2>/dev/null`
	    QT4_VER=`echo "$verqt" | $csed "$vsed" | $csed "$vsed" | sed 's/\(..\)\.\(..\)\.\(..\)/\1\2\3/'`
	    if [[ 1$QT4_VER -lt 1040300 ]]; then
	    	HAVE_QT4=no
	    	verqt="too old ($verqt)"
	   fi
	else
	    verqt="no"
	fi
	AC_MSG_RESULT([$verqt])
    fi
fi

AC_SUBST(HAVE_QT4)
AC_SUBST(QT4_INC)
AC_SUBST(QT4_LIB)
AC_SUBST(QT4_INC_NET)
AC_SUBST(QT4_LIB_NET)
AC_SUBST(QT4_MOC)
AC_SUBST(QT4_VER)
AC_SUBST(QT4_STATIC_MODULES)


HAVE_MALLINFO=no
AC_ARG_ENABLE(mallinfo,AC_HELP_STRING([--enable-mallinfo],[Enable malloc info if available (default: yes)]),[ac_cv_use_mallinfo=$withval],[ac_cv_use_mallinfo=yes])
if [[ "x$ac_cv_use_mallinfo" != "xno" ]]; then
AC_MSG_CHECKING([for mallinfo in malloc.h])
AC_TRY_COMPILE([
#include <malloc.h>
],[
struct mallinfo info = mallinfo();
return info.keepcost;
],
HAVE_MALLINFO=yes
)
AC_MSG_RESULT([$HAVE_MALLINFO])
fi
AC_SUBST(HAVE_MALLINFO)


HAVE_COREDUMPER=no
COREDUMPER_INC=""
COREDUMPER_LIB=""
AC_ARG_WITH(coredumper,AC_HELP_STRING([--with-coredumper],[use Google coredumper if available (default)]),[ac_cv_use_coredumper=$withval],[ac_cv_use_coredumper=yes])
gcdinc="include/google/coredumper.h"
if [[ "x$ac_cv_use_coredumper" = "xyes" ]]; then
for i in /usr /usr/local; do
    ac_cv_use_coredumper="$i"
    test -f "$ac_cv_use_coredumper/$gcdinc" && break
done
fi
if [[ "x$ac_cv_use_coredumper" != "xno" ]]; then
AC_MSG_CHECKING([for coredumper.h in $ac_cv_use_coredumper])
if [[ -f "$ac_cv_use_coredumper/$gcdinc" ]]; then
    HAVE_COREDUMPER=yes
    COREDUMPER_INC="-I$ac_cv_use_coredumper"
    COREDUMPER_LIB="-L$ac_cv_use_coredumper -lcoredumper"
fi
AC_MSG_RESULT([$HAVE_COREDUMPER])
fi
AC_SUBST(HAVE_COREDUMPER)
AC_SUBST(COREDUMPER_INC)
AC_SUBST(COREDUMPER_LIB)


RTTI_OPT=""
AC_ARG_ENABLE(rtti,AC_HELP_STRING([--enable-rtti],[Enable RTTI support (default: like pwlib)]),want_rtti=$enableval,want_rtti=auto)
AC_MSG_CHECKING([whether to enable RTTI support])
USE_RTTI="$want_rtti"
case "x$want_rtti" in
    xyes|xno|xnone)
	;;
    xauto)
	USE_RTTI="$PWLIB_RTTI"
	;;
    *)
	AC_ERROR([Invalid argument passed to --enable-rtti])
	;;
esac
case "$USE_RTTI" in
    yes)
	RTTI_OPT="-frtti"
	;;
    no)
	RTTI_OPT="-fno-rtti"
	;;
esac
AC_MSG_RESULT([$USE_RTTI])
AC_SUBST(USE_RTTI)
AC_SUBST(RTTI_OPT)

if [[ "x$HAVE_H323" != "xno" ]]; then
if [[ "x$USE_RTTI" != "x$PWLIB_RTTI" ]]; then
AC_MSG_WARN([Mismatched RTTI setting between Yate ($USE_RTTI) and pwlib ($PWLIB_RTTI)])
fi
fi


INSTALL_L=""
AC_ARG_ENABLE(lib-exec,AC_HELP_STRING([--enable-lib-exec],[Executable libraries and modules (default: auto)]),want_lib_exec=$enableval,want_lib_exec=auto)
case "x$want_lib_exec" in
    xyes)
	;;
    xno)
	INSTALL_L="-m 0644"
	;;
    xauto)
	AC_MSG_CHECKING([if shared libraries should be executable])
	if stat -L "/usr/$ARCHLIB/libm.so" 2>/dev/null | grep -q '(0644/'; then
	    want_lib_exec=no
	    INSTALL_L="-m 0644"
	else
	    want_lib_exec=yes
	fi
	AC_MSG_RESULT([$want_lib_exec])
	;;
    *)
	AC_ERROR([Invalid argument passed to --enable-lib-exec])
	;;
esac
AC_SUBST(INSTALL_L)


INSTALL_D="install -D"
CFLAGS=`echo "$CFLAGS" | sed 's/\(^\| \+\)-g[[0-9]]*//' | sed 's/[[[:space:]]]\{2,\}/ /g'`
MODULE_CFLAGS="-fno-exceptions -fPIC $HAVE_GCC_FORMAT_CHECK $HAVE_BLOCK_RETURN"
MODULE_CPPFLAGS="$HAVE_NO_OVERLOAD_VIRT_WARN $RTTI_OPT $MODULE_CFLAGS"
MODULE_LDRELAX="-rdynamic -shared"
MODULE_SYMBOLS="-Wl,--retain-symbols-file,/dev/null"
SONAME_OPT="-shared -Wl,-soname="
case "x$uname_os" in
    x*BSD)
	INSTALL_D="install"
	MODULE_LDRELAX="$MODULE_LDRELAX -lc"
	;;
   xDarwin)
	INSTALL_D="install"
	MODULE_LDRELAX="-dynamiclib"
	MODULE_SYMBOLS=""
	SONAME_OPT="-dynamiclib -Wl,-dylib_install_name,"
        ;;
esac
MODULE_LDFLAGS="$MODULE_LDRELAX $GCC_UNRESOLVED_SYMBOLS"
AC_SUBST(INSTALL_D)
AC_SUBST(MODULE_CFLAGS)
AC_SUBST(MODULE_CPPFLAGS)
AC_SUBST(MODULE_LDRELAX)
AC_SUBST(MODULE_LDFLAGS)
AC_SUBST(MODULE_SYMBOLS)
AC_SUBST(SONAME_OPT)


DOXYGEN_BIN=""
AC_ARG_WITH(doxygen,AC_HELP_STRING([--with-doxygen=EXE],[use doxygen to generate API docs (default: PATH)]),[ac_cv_use_doxygen=$withval],[ac_cv_use_doxygen=yes])
if [[ "x$ac_cv_use_doxygen" != "xno" ]]; then
test "x$ac_cv_use_doxygen" = "xyes" && ac_cv_use_doxygen=`(which doxygen) 2>/dev/null`
AC_MSG_CHECKING([for doxygen version])
if [[ "x$ac_cv_use_doxygen" != "x" -a -x "$ac_cv_use_doxygen" ]]; then
DOXYGEN_BIN="$ac_cv_use_doxygen"
ac_cv_use_doxygen=`($DOXYGEN_BIN --version) 2>/dev/null`
else
ac_cv_use_doxygen="no"
fi
AC_MSG_RESULT([$ac_cv_use_doxygen])
fi
AC_SUBST(DOXYGEN_BIN)


KDOC_BIN=""
AC_ARG_WITH(kdoc,AC_HELP_STRING([--with-kdoc=EXE],[use kdoc to generate API docs (default: PATH)]),[ac_cv_use_kdoc=$withval],[ac_cv_use_kdoc=yes])
if [[ "x$ac_cv_use_kdoc" != "xno" ]]; then
test "x$ac_cv_use_kdoc" = "xyes" && ac_cv_use_kdoc=`(which kdoc) 2>/dev/null`
AC_MSG_CHECKING([for kdoc version])
if [[ "x$ac_cv_use_kdoc" != "x" -a -x "$ac_cv_use_kdoc" ]]; then
KDOC_BIN="$ac_cv_use_kdoc"
ac_cv_use_kdoc=`($KDOC_BIN --version) 2>&1 | awk '{ print $2 }'`
else
ac_cv_use_kdoc="no"
fi
AC_MSG_RESULT([$ac_cv_use_kdoc])
fi
AC_SUBST(KDOC_BIN)


m4_sinclude(./YateLocal.ac)

AC_CONFIG_FILES([packing/rpm/yate.spec
                 packing/portage/yate.ebuild
                 yate.pc
		 yateversn.h
		 yateiss.inc
                 Makefile
                 engine/Makefile
                 modules/Makefile
                 modules/test/Makefile
                 clients/Makefile
                 clients/qt4/Makefile
                 libs/ilbc/Makefile
                 libs/ysip/Makefile
                 libs/yrtp/Makefile
                 libs/ysdp/Makefile
		 libs/yiax/Makefile
		 libs/yjabber/Makefile
		 libs/yscript/Makefile
		 libs/ymgcp/Makefile
		 libs/ysig/Makefile
                 libs/ypbx/Makefile
                 libs/ymodem/Makefile
		 libs/yasn/Makefile
	         libs/ysnmp/Makefile
	         libs/miniwebrtc/Makefile
	         libs/yradio/Makefile
                 share/Makefile
                 share/scripts/Makefile
                 share/skins/Makefile
                 share/sounds/Makefile
                 share/help/Makefile
                 share/data/Makefile
                 conf.d/Makefile])
AC_CONFIG_FILES([yate-config],[chmod +x yate-config])
AC_CONFIG_FILES([run],[chmod +x run])
CONFIGURE_FILES=`echo "$ac_config_files config.status config.log" | sed 's,packing/[[^ ]]* *,,g'`
AC_SUBST(CONFIGURE_FILES)
AC_OUTPUT

if [[ "x$uname_os" = "xDarwin" ]]; then
if [[ "x$HAVE_COREAUDIO" != "xyes" ]]; then
AC_MSG_WARN([Please take note that the building of the audio module for Mac OS X has been disabled. The Yate client will not have audio support. If you need audio, you need at least Mac OS X SDK 10.5 or something newer.])
fi
fi
