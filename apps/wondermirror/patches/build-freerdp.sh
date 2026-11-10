#!/bin/sh
# Builds FreeRDP's X11 client with Wonder Mirror's patches and installs it as
# $PREFIX/lib/wondermirror/xfreerdp3, where Wonder Mirror prefers it over the
# system's. Only the client is rebuilt: it keeps using the system's FreeRDP
# libraries, so the source must be the version of the installed package.
#
#   patches/build-freerdp.sh [PREFIX]        (default: ~/.local)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
prefix=${1:-$HOME/.local}
version=$(pkg-config --modversion freerdp3)
work=${WONDERMIRROR_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/wondermirror/freerdp-$version}

mkdir -p "$work"
if [ ! -d "$work/src" ]; then
    git clone --quiet --depth 1 --branch "$version" \
        https://github.com/FreeRDP/FreeRDP.git "$work/src"
fi
git -C "$work/src" checkout --quiet -- .
for patch in "$here"/freerdp-*.patch; do
    git -C "$work/src" apply "$patch"
done

CFLAGS="${CFLAGS:--O2} -Wno-incompatible-pointer-types" cmake -S "$work/src" -B "$work/build" \
    -G Ninja -W no-dev \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_SKIP_RPATH=ON -DWITH_BINARY_VERSIONING=ON \
    -DWITH_CHANNELS=ON -DWITH_CLIENT_CHANNELS=ON -DWITH_X11=ON \
    -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF \
    -DWITH_SERVER=OFF -DWITH_SAMPLE=OFF -DWITH_WAYLAND=OFF -DWITH_MANPAGES=OFF \
    -DWITH_VERBOSE_WINPR_ASSERT=OFF -DBUILD_TESTING=OFF \
    -DWITH_FFMPEG=ON -DWITH_DSP_FFMPEG=ON -DWITH_SWSCALE=ON -DWITH_PULSE=ON \
    -DWITH_ALSA=ON -DWITH_CUPS=ON -DWITH_FUSE=ON -DWITH_ICU=ON -DWITH_JPEG=ON \
    -DWITH_PCSC=ON >"$work/cmake.log" 2>&1
ninja -C "$work/build" xfreerdp >"$work/build.log"

install -Dm755 "$work/build/client/X11/xfreerdp3" "$prefix/lib/wondermirror/xfreerdp3"
echo "installed $prefix/lib/wondermirror/xfreerdp3"
