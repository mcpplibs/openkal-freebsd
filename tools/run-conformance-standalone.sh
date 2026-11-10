#!/usr/bin/env bash
# THE CONFORMANCE SUITE IN THE STANDALONE ARRANGEMENT: built for
# `<arch>-freebsd-musl', openkal-musl over this implementation with the feature
# `standalone', from working trees --- the specification, openkal-musl, and
# this repository --- so that a change spanning them is examined as written.
#
#   run-conformance-standalone.sh <spec> <openkal-musl> <arch> <features> [<openkal-llvm-runtime>]
#
# The suite is C++: its C++ runtime is openkal-llvm-runtime, built over
# openkal-musl for the row, from the given tree or else the index.
#
# The suite's runner is OPENKAL_CONFORMANCE_RUNNER, as for run-conformance.sh.
# The copies made here are in a temporary directory; nothing committed is
# rewritten.
set -euo pipefail
spec="$(cd "${1:?the specification}" && pwd)"
musl="$(cd "${2:?openkal-musl}" && pwd)"
arch="${3:?the architecture}"
features="${4:?the suite features}"
runtime="${5:-}"
[ -z "$runtime" ] || runtime="$(cd "$runtime" && pwd)"
here="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
cp -R "$spec/conformance" "$work/conformance"
cp -R "$musl" "$work/musl"
rm -rf "$work/conformance/target" "$work/musl/target"

# openkal-musl names this implementation, standalone, by its tree, and the
# specification by its tree.
m="$work/musl/mcpp.toml"
sed -i -E "s|^openkal = .*$|openkal = { path = \"$spec\" }|" "$m"
if grep -qE '^openkal-freebsd = ' "$m"; then
    sed -i -E "s|^openkal-freebsd = \\{ version = \"[^\"]*\"(.*)$|openkal-freebsd = { path = \"$here\"\\1|" "$m"
else
    printf '\n[target.'"'"'cfg(os = "freebsd")'"'"'.dependencies]\nopenkal-freebsd = { path = "%s", features = ["standalone"] }\n\n[target.'"'"'cfg(os = "freebsd")'"'"'.build]\nldflags = ["-nostdlib", "-static", "-Wl,--no-dynamic-linker", "-Wl,--gc-sections", "-Lport/lib"]\n' "$here" >> "$m"
fi
grep -q "openkal-freebsd = { path = \"$here\"" "$m" || { echo "openkal-musl does not name this tree" >&2; exit 1; }

# The suite: the specification by its tree, openkal-musl as its C library,
# and the row opted into with the toolchain the platform rows use.
c="$work/conformance/mcpp.toml"
sed -i -E "s|^openkal = .*$|openkal = { path = \"$spec\" }\nopenkal-musl = { path = \"$work/musl\" }|" "$c"
printf '\n[target.%s-freebsd-musl]\ntoolchain = "llvm@23.1.3"\n' "$arch" >> "$c"
if [ -n "$runtime" ]; then
    rt="$work/runtime"
    cp -R "$runtime" "$rt"; rm -rf "$rt/target" "$rt/.git"
    sed -i -E "s|^openkal-musl = \\{ version = \"[^\"]*\"|openkal-musl = { path = \"$work/musl\"|" "$rt/mcpp.toml"
    grep -q "openkal-musl = { path = \"$work/musl\"" "$rt/mcpp.toml" || { echo "openkal-llvm-runtime does not name this openkal-musl" >&2; exit 1; }
    printf '\n[target.'"'"'cfg(os = "freebsd")'"'"'.dependencies]\nopenkal-llvm-runtime = { path = "%s" }\n' "$rt" >> "$c"
else
    printf '\n[target.'"'"'cfg(os = "freebsd")'"'"'.dependencies]\nopenkal-llvm-runtime = "^0.20.1"\n' >> "$c"
fi

cd "$work/conformance"
mcpp build --target "$arch-freebsd-musl" --features "$features"
binary="$(find target -type f -name openkal-conformance | head -1)"
[ -n "$binary" ] || { echo "the suite was not produced" >&2; exit 2; }
file "$binary" || true
export OPENKAL_CONFORMANCE_EMPTY=""
export OPENKAL_CONFORMANCE_PROVIDES="$(awk '/^[[:space:]]*provides-interfaces[[:space:]]*=/ {on=1} on {line=line $0; if ($0 ~ /\]/) on=0} END {gsub(/.*\[/,"",line); gsub(/\].*/,"",line); gsub(/[" \t]/,"",line); gsub(/,/,";",line); print line}' "$here/mcpp.toml")"
${OPENKAL_CONFORMANCE_RUNNER:-} "./$binary"
