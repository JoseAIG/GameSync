#!/usr/bin/env bash
# Build the PS5 client. Run from WSL (Ubuntu 24.04 with clang-18/lld-18).
#
# Usage:
#     bash ps5/build.sh            # build dist/PPSA90543 + zip
#     bash ps5/build.sh clean      # drop the staged build (keeps fetched deps)
#
# The PS5 toolchain glue (ELF -> FSELF converter, libc.prx runtime shim,
# pinned ps5-payload-sdk and PacBrew SDL2 sysroot) comes from
# BlackBearReloaded's ps5-native-app-boilerplate (GPL-3.0-or-later), fetched at
# a pinned commit into $PS5_WORK. Our sources are staged into a subfolder of
# that checkout because its build only takes repository-relative paths.
set -euo pipefail

BOILERPLATE_URL="https://github.com/BlackBearReloaded/ps5-native-app-boilerplate"
BOILERPLATE_COMMIT="3ada439fa044f60c8b488678577a740761385711"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# Keep the work tree on the Linux filesystem: /mnt/<drive> is slow and loses
# the executable bits the boilerplate's scripts need.
PS5_WORK="${PS5_WORK:-$HOME/.cache/gamesync-ps5}"
BP="$PS5_WORK/boilerplate"
STAGE="$BP/gamesync"

if [ "${1:-}" = "clean" ]; then
    rm -rf "$STAGE" "$BP/build" "$BP/dist" "$SCRIPT_DIR/dist"
    echo "cleaned"
    exit 0
fi

mkdir -p "$PS5_WORK"
if [ ! -d "$BP/.git" ]; then
    git clone -q "$BOILERPLATE_URL" "$BP"
fi
if [ "$(git -C "$BP" rev-parse HEAD)" != "$BOILERPLATE_COMMIT" ]; then
    git -C "$BP" fetch -q origin
    git -C "$BP" checkout -q --force "$BOILERPLATE_COMMIT"
fi

# Two link-line fixes for the pinned commit:
#  - PacBrew's SDL2 asks for -lSceVideoOut and friends, but the line never
#    names the SDK's stub directory;
#  - --as-needed must come before those -l flags too, or SDL's keyboard and
#    IME modules stay NEEDED even though ps5_shims.c defines what SDL calls
#    (importing libSceKeyboard/libSceImeDialog stops a title from launching).
python3 - "$BP/tools/build.sh" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
new = '-e _start -o "$build/llvm-pie.elf" -L"$sdk_root/target/lib" --as-needed "${link_inputs[@]}"'
olds = ['-e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}"',
        '-e _start -o "$build/llvm-pie.elf" -L"$sdk_root/target/lib" "${link_inputs[@]}"']
if new not in s:
    for old in olds:
        if old in s:
            s = s.replace(old, new)
            break
    else:
        sys.exit("boilerplate tools/build.sh changed: link-line patch no longer applies")
    open(p, "w").write(s)
PY

VERSION="$(tr -d ' \r\n' < "$REPO_DIR/VERSION")"
IFS=. read -r V_MAJOR V_MINOR V_MICRO <<< "$VERSION"
CONTENT_VERSION="$(printf '%02d.%03d.%03d' "$V_MAJOR" "$V_MINOR" "$V_MICRO")"

# Stage: the shared C client modules, the PS5 sources, a generated version
# header, and sce_sys = boilerplate placeholders overlaid with ours.
rm -rf "$STAGE"
mkdir -p "$STAGE/src" "$STAGE/sce_sys" "$STAGE/assets"
cp "$SCRIPT_DIR"/src/*.c "$SCRIPT_DIR"/src/*.h "$SCRIPT_DIR"/src/*.cpp "$SCRIPT_DIR"/src/*.hpp "$STAGE/src/"
cat > "$STAGE/src/version.h" <<EOF
#ifndef GAMESYNC_PS5_VERSION_H
#define GAMESYNC_PS5_VERSION_H
#define APP_VERSION "$VERSION"
#endif
EOF
cp "$BP"/sce_sys/* "$STAGE/sce_sys/"
cp -r "$SCRIPT_DIR"/sce_sys/. "$STAGE/sce_sys/"
sed "s/@CONTENT_VERSION@/$CONTENT_VERSION/" "$SCRIPT_DIR/sce_sys/param.json" > "$STAGE/sce_sys/param.json"
if [ -d "$SCRIPT_DIR/assets" ]; then
    cp -r "$SCRIPT_DIR"/assets/. "$STAGE/assets/"
fi
# notices travel with the title (libiconv is LGPL, the runtime shim GPL)
cp "$REPO_DIR/LICENSE" "$STAGE/assets/LICENSE.txt"
cp "$REPO_DIR/THIRD_PARTY_NOTICES.md" "$STAGE/assets/THIRD_PARTY_NOTICES.md"
cp "$REPO_DIR/licenses/LGPL-2.1.txt" "$STAGE/assets/LGPL-2.1.txt"

# dlmalloc 2.8.6 (Doug Lea, public domain/CC0) for ps5_heap.c, the copy
# SwanStationPS5 ships, pinned by commit and checked by hash.
DLMALLOC_URL="https://raw.githubusercontent.com/darkxex/SwanStationPS5/e6617a232c671cb53ced802885ff8b540c94c23f/third_party/dlmalloc/malloc.c"
DLMALLOC_SHA256="278cf587a7a067e37edd1b2a77d979d3a219e1e958f285746722c19faac4a5ac"
DLMALLOC="$PS5_WORK/dlmalloc-2.8.6.c"
if [ ! -f "$DLMALLOC" ] || ! echo "$DLMALLOC_SHA256  $DLMALLOC" | sha256sum -c --quiet - 2>/dev/null; then
    curl -sSfL "$DLMALLOC_URL" -o "$DLMALLOC.download"
    echo "$DLMALLOC_SHA256  $DLMALLOC.download" | sha256sum -c --quiet -
    mv "$DLMALLOC.download" "$DLMALLOC"
fi
mkdir -p "$STAGE/third_party/dlmalloc"
cp "$DLMALLOC" "$STAGE/third_party/dlmalloc/malloc.c"

cd "$BP"
# The allocator family is wrapped onto ps5_heap.c: a native title's own C
# heap can't hand out the few MB a PS2 card image needs. APP_LAPY_HELPER=1
# packages the upstream Lapy one-shot helper (/app0/lapy.elf) that
# elevation.cpp sends to the ELF loader when no HEN grants /data.
USE_CCACHE="${USE_CCACHE:-0}" make \
    APP_LAPY_HELPER=1 \
    APP_SOURCE_DIR=gamesync/src \
    APP_PARAM=gamesync/sce_sys/param.json \
    APP_SCE_SYS=gamesync/sce_sys \
    APP_ASSETS=gamesync/assets \
    APP_WRAP_SYMBOLS="malloc free calloc realloc reallocf memalign posix_memalign aligned_alloc malloc_usable_size" \
    PACBREW_PACKAGES="sdl2 zlib"

TITLE_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$STAGE/sce_sys/param.json")"
mkdir -p "$SCRIPT_DIR/dist"
rm -rf "$SCRIPT_DIR/dist/$TITLE_ID"
cp -r "$BP/dist/$TITLE_ID" "$SCRIPT_DIR/dist/"
cp "$BP/dist/$TITLE_ID.zip" "$SCRIPT_DIR/dist/ps5sync.zip"
echo ""
echo "Built $SCRIPT_DIR/dist/ps5sync.zip ($TITLE_ID, v$VERSION)"
echo "Extract to /data/homebrew/ on the PS5 and chmod -R 777 the folder."
