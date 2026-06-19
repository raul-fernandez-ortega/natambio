#!/bin/sh
# -----------------------------------------------------------------------------
# nat_ambio_surround -- regenerate the autotools build infrastructure
#
# Run this once after a fresh checkout (or whenever configure.ac / *.am
# change). Released tarballs ship with configure pre-generated and do not
# need this step.
# -----------------------------------------------------------------------------

set -e

srcdir="$(cd "$(dirname "$0")" && pwd)"
cd "$srcdir"

# Ensure aux directories exist.
mkdir -p build-aux m4

if ! command -v autoreconf >/dev/null 2>&1; then
    echo "autogen.sh: autoreconf not found. Install GNU autotools" >&2
    echo "            (packages 'autoconf', 'automake', 'pkg-config')." >&2
    exit 1
fi

echo "autogen.sh: running autoreconf --install --force --warnings=all"
autoreconf --install --force --warnings=all

echo "autogen.sh: done."
echo "Now run:  ./configure && make"
