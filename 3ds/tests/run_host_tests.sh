#!/bin/sh
# Builds and runs the 3DS client's host tests (plain C parts only) with the
# host gcc. From the repo root, e.g. in a Debian container:
#   docker run --rm -v "$PWD":/src -w /src debian:bookworm-slim sh 3ds/tests/run_host_tests.sh
# or directly with any host gcc (MinGW/w64devkit works: SAN=none).
# Set CATALOG_JSON to a saved /api/v1/roms?system=3DS response to also parse a
# full real catalog. Sanitizers: SAN=undefined by default; SAN=address,undefined
# adds ASan; SAN=none for neither.
set -e
cd "$(dirname "$0")/.."
if ! command -v gcc >/dev/null 2>&1; then
    apt-get update -qq >/dev/null && apt-get install -y -qq gcc libc6-dev >/dev/null
fi
out=$(mktemp -d)
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -O1 -g -Isource"
SAN=${SAN:-undefined}
if [ "$SAN" != "none" ]; then
    CFLAGS="$CFLAGS -fsanitize=$SAN -fno-sanitize-recover=all"
fi

gcc $CFLAGS -o "$out/test_catalog_data" tests/test_catalog_data.c source/catalog_data.c
"$out/test_catalog_data" "$out" ${CATALOG_JSON:+"$CATALOG_JSON"}

# Modules that include common.h build against tests/host/3ds.h (types only)
HOST_INC="-Itests/host -Iinclude"

gcc $CFLAGS $HOST_INC -o "$out/test_history_parse" tests/test_history_parse.c source/history_parse.c
"$out/test_history_parse"

gcc $CFLAGS -o "$out/test_gui_motion" tests/test_gui_motion.c source/gui_motion.c -lm
"$out/test_gui_motion"

gcc $CFLAGS $HOST_INC -o "$out/test_config_file" tests/test_config_file.c source/config_file.c
"$out/test_config_file"

gcc $CFLAGS -o "$out/test_ui_format" tests/test_ui_format.c source/ui_format.c
"$out/test_ui_format"

gcc $CFLAGS $HOST_INC -o "$out/test_ui_gesture" tests/test_ui_gesture.c source/ui_gesture.c source/gui_motion.c -lm
"$out/test_ui_gesture"

gcc $CFLAGS -o "$out/test_folder_scan" tests/test_folder_scan.c source/folder_scan.c
"$out/test_folder_scan"

rm -rf "$out"
