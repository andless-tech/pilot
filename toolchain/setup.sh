#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
name=arm-rockchip830-linux-uclibcgnueabihf
archive="$here/$name.tar.gz"
test -f "$archive" || { echo 'Missing toolchain archive: run scripts/import_sdk.py SDK --toolchain' >&2; exit 1; }
(cd "$here" && sha256sum -c "$name.tar.gz.sha256")
digest=$(sha256sum "$archive")
digest=${digest%% *}
base="${PILOT_TOOLCHAIN_CACHE:-$HOME/.cache/pilot/toolchains}"
root="$base/$digest"
mkdir -p "$base"
if [ ! -x "$root/$name/bin/$name-gcc" ]; then
    stage=$(mktemp -d "$base/.unpack.XXXXXX")
    trap 'rm -rf -- "$stage"' EXIT HUP INT TERM
    tar -xzf "$archive" -C "$stage"
    mv -T "$stage" "$root"
    trap - EXIT HUP INT TERM
fi
printf 'Toolchain ready. Build with:\nmake TOOLCHAIN_ROOT=%s/%s\n' "$root" "$name"
