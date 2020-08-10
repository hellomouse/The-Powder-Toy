#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

if [ -z "${PLATFORM_SHORT-}" ]; then
	>&2 echo "PLATFORM_SHORT not set" 
	exit 1
fi

if [ -z "${build_sh_init-}" ]; then
	if [ $PLATFORM_SHORT == "win" ]; then
		for i in C:/Program\ Files\ \(x86\)/Microsoft\ Visual\ Studio/**/**/VC/Auxiliary/Build/vcvarsall.bat; do
			vcvarsall_path=$i
		done
		cat << BUILD_INIT_BAT > .github/build_init.bat
@echo off
call "${vcvarsall_path}" x64
bash -c 'build_sh_init=1 ./.github/build.sh'
BUILD_INIT_BAT
		./.github/build_init.bat
	else
		build_sh_init=1 ./.github/build.sh
	fi
	exit 0
fi

other_flags=
bin_postfix=
if [ $PLATFORM_SHORT == "lin" ]; then
	# We use gcc on lin; sadly, gcc + lto + libstdc++ + pthread = undefined reference to
	# pthread_create, thanks to weak symbols in libstdc++.so (or something). See
	# https://gcc.gnu.org/legacy-ml/gcc-help/2017-03/msg00081.html
	other_flags+=$'\t-Db_asneeded=false\t-Dcpp_link_args=-Wl,--no-as-needed'
fi
if [ $PLATFORM_SHORT == "win" ]; then
	other_flags+=$'\t-Db_vscrt=mt'
	bin_postfix=$bin_postfix.exe
fi
meson -Dbuildtype=release -Dbuild_render=true -Dbuild_font=true -Db_pie=false -Db_staticpic=false -Db_lto=true -Dstatic=prebuilt -Dignore_updates=false -Dinstall_check=true $other_flags build
cd build
ninja
if [ $PLATFORM_SHORT == "lin" ]; then
	strip powder$bin_postfix render$bin_postfix font$bin_postfix
fi
if [ $PLATFORM_SHORT == "mac" ]; then
	strip powder$bin_postfix render$bin_postfix font$bin_postfix
fi
7z a ../powder.zip powder$bin_postfix render$bin_postfix font$bin_postfix
cd ..
7z a powder.zip README.md LICENSE
