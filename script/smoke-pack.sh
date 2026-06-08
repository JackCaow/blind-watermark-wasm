#!/bin/bash
# Pack the package, install the tarball into a clean throwaway project, and
# round-trip a watermark against the INSTALLED build. This verifies the published
# artifact actually works (entry resolves the glue, the glue finds its .wasm) —
# catching packaging regressions before `npm publish` ever runs.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

# `npm pack` prints the tarball name as the last stdout line; take only that so any
# prepack build chatter on stdout can't end up in the filename (pipefail keeps a
# pack failure fatal).
TARBALL="$(npm pack --silent | tail -n1)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cp test.png script/smoke-test.mjs "$WORK/"
mv "$TARBALL" "$WORK/package.tgz"

cd "$WORK"
npm init -y >/dev/null 2>&1
npm install ./package.tgz >/dev/null 2>&1
node smoke-test.mjs
