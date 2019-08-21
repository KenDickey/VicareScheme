# configure.sh --
#

##set -xe

prefix=/usr/local
libdir=${prefix}/lib64
LIBFFI_VERSION=3.2.1
LIBFFI_INCLUDEDIR=${libdir}/libffi-${LIBFFI_VERSION}/include

./configure \
    --enable-maintainer-mode				\
    --config-cache					\
    --cache-file=config.cache				\
    --prefix="${prefix}"				\
    --libdir="${libdir}"				\
    --enable-binfmt					\
    --enable-time-tests					\
    --enable-file-magic					\
    --with-pthread					\
    TARGET_ARCH="-march=armv8-a" \
    CFLAGS=" -D__ARM_ARCH_ISA_A64 -DARM64 -D__arm__ -D__arm64__ -D__aarch64__ -O3 -pedantic" \
    CPPFLAGS="-I${LIBFFI_INCLUDEDIR}"			\
    VFLAGS='-O3'					\
    "$@"

### end of file
