#!/bin/bash
set -e

PARALLEL=$(grep -c ^processor /proc/cpuinfo)

# bzip2's make script doesn't like emmake
cd bzip2
emcc -O3 -D_FILE_OFFSET_BITS=64 blocksort.c huffman.c crctable.c randtable.c \
    compress.c decompress.c bzlib.c -o bz2.bc
echo Done compiling bzip2

# build specifically fftw3f
cd ../fftw3
emconfigure ./configure CFLAGS=-O3 --enable-float --disable-fortran
emmake make -j$PARALLEL
echo Done compiling fftw3f

# lua also does not like emmake
cd ../lua5.2
cd src
emcc -O3 -DLUA_COMPAT_ALL -DLUA_USE_LINUX lapi.c lcode.c lctype.c ldebug.c \
    ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c \
    lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c \
    lbaselib.c lbitlib.c lcorolib.c ldblib.c liolib.c lmathlib.c loslib.c \
    lstrlib.c ltablib.c loadlib.c linit.c -o ../lua52.bc
cd ..
echo Done compiling lua5.2

# we can use normal stuff to compile here
cd ../zlib
emconfigure ./configure
emmake make -j $PARALLEL
mv libz.so.1.2.11 libz.bc
echo Done compiling zlib
