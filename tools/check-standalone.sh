#!/usr/bin/env bash
# The standalone arrangement compiles for every architecture this
# implementation states, from its own sources and openkal's headers alone: no
# system header, no C library, `-ffreestanding'. The objects carry the brand.
#
#   check-standalone.sh <clang++> <openkal-include-dir>
set -euo pipefail
cxx="${1:?clang++}"; inc="${2:?the include directory of openkal}"
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
for arch in x86_64 aarch64; do
    for f in "$here"/src/*.cpp; do
        "$cxx" --no-default-config --target="$arch-unknown-freebsd14.0" -std=c++23 -ffreestanding -nostdinc++ \
               -fno-exceptions -fno-rtti -DOKF_STANDALONE -I "$inc" -c "$f" -o "$out/$arch-$(basename "$f" .cpp).o"
    done
    echo "  ok  standalone, $arch: every source compiles without a system header"
done
