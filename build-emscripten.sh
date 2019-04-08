#!/bin/bash
set -e
shopt -s globstar

# how many things to run in parallel
PARALLEL=$(grep -c ^processor /proc/cpuinfo)
# memory allocation
MEMORY="250MB"

python3 generator.py

export EMCC_CORES=$PARALLEL
# -g4 for debug, -O3 for release
OPT="-O3"
mkdir -p emscripten-build
# for uber-debug: -s ASSERTIONS=2 -s SAFE_HEAP=1
EMCC_FLAGS="-s WASM=1 -s FETCH=1 -s USE_SDL=2 -s TOTAL_MEMORY=$MEMORY \
    -s USE_PTHREADS=1 -s DEMANGLE_SUPPORT=1 $OPT"
# removing gravfft because fftw3f seems very slow (-DGRAVFFT)
CFLAGS="$EMCC_FLAGS -ftree-vectorize \
    -funsafe-math-optimizations -ffast-math -fomit-frame-pointer \
    -Wno-invalid-offsetof \
    -D_64BIT -D_REENTRANT -DLUA_R_INCL -DLIN -DX86 -DLUACONSOLE -DGRAVFFT \
    -D LUA_COMPAT_ALL \
    -Isrc -Idata -Igenerated \
    -Ideps/lua5.2/src -Ideps/bzip2 -Ideps/fftw3/api -Ideps/zlib"

# to only rebuild certain files, pass their paths as arguments
if [[ $# = 0 ]]; then
    # build everything
    C_SOURCES=(src/lua/socket/*.c src/lua/LuaCompat.c)
    CPP_SOURCES=(src/*.cpp src/*/*.cpp src/*/*/*.cpp generated/*.cpp data/*.cpp)
    # assume a complete rebuild
    rm -rf emscripten-build
else
    # build only certain files
    C_SOURCES=()
    CPP_SOURCES=()
    for file in "$@"; do
        case "$file" in
        *.c)
            C_SOURCES+=( $file )
            ;;
        *.cpp)
            CPP_SOURCES+=( $file )
            ;;
        none)
            # just rebuild the bundle
            ;;
        *)
            echo Invalid file extension: $file
            exit 1
            ;;
        esac
    done
    # remove powder.bc
    rm -f emscripten-build/powder.bc
fi

for file in "${C_SOURCES[@]} ${CPP_SOURCES[@]}"; do
    mkdir -p $(dirname emscripten-build/$file)
done
if [[ "${#C_SOURCES[@]}" -ne 0 ]]; then
    printf "%s\n" ${C_SOURCES[@]} | xargs -n 1 -I '{}' -t -P $PARALLEL emcc \
        $CFLAGS \
        -o 'emscripten-build/{}.bc' \
        '{}'
fi
if [[ "${#CPP_SOURCES[@]}" -ne 0 ]]; then
    printf "%s\n" ${CPP_SOURCES[@]} | xargs -n 1 -I '{}' -t -P $PARALLEL emcc \
        -std=c++11 $CFLAGS \
        -o 'emscripten-build/{}.bc' \
        '{}'
fi
set -x
emcc $EMCC_FLAGS \
    -o emscripten-build/powder.bc emscripten-build/**/*.bc
emcc --memory-init-file 1 $EMCC_FLAGS \
    emscripten-build/powder.bc deps/bzip2/bz2.bc deps/fftw3/.libs/libfftw3f.a \
    deps/lua5.2/lua52.bc deps/zlib/libz.bc -o emscripten-build/tpt.js
