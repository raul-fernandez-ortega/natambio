#!/bin/bash
#
# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
#
# dsp_regress -- did lib/ change what it produces?
#
# Builds lib/ twice: once from a git ref, once from the working tree, links the
# same probe against each, and compares the filters byte for byte. What it is
# guarding is the thing a DSP library makes easy to break by accident -- a
# change meant for one caller quietly moving the output of another. When
# firwin2() was split into firwin2_ex() this is what said, rather than hoped,
# that <loudness> and <xtc> still produced the same coefficients.
#
#     ./regress.sh              compare the working tree against HEAD
#     ./regress.sh HEAD~1       ... against the commit before the last
#     ./regress.sh main         ... against another branch
#
# Exit status is 0 when every case matches, 1 when any differs, 2 when
# something could not be built -- which is not a pass and is reported as its
# own thing. A reference that predates a function the probe calls will fail to
# build, and that is the correct answer: there is nothing to compare.
#
# Bit for bit and not "close enough": these are design-time filters, computed
# once at start-up, so there is no reason for any of them to move by a ulp, and
# a tolerance would only be a place for a real change to hide. A difference
# here is not necessarily a bug -- <low_and_high_filter> was moved to a
# Butterworth template on purpose, and this script duly reported it -- but it
# is always something to have decided rather than discovered.

set -u

REF="${1:-HEAD}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

CFLAGS="-O3 -Wall -g"
LIBS="-lfftw3 -lm"

fail_build() { echo "dsp_regress: $*" >&2; exit 2; }

command -v git >/dev/null || fail_build "git not found"
git -C "$repo" rev-parse --verify --quiet "$REF^{commit}" >/dev/null \
  || fail_build "'$REF' is not a commit in $repo"

echo "dsp_regress: reference $REF ($(git -C "$repo" rev-parse --short "$REF")) against the working tree"
echo

# --- the reference lib, out of git and built on its own ----------------------
mkdir -p "$work/ref"
git -C "$repo" archive "$REF" lib | tar -x -C "$work/ref" \
  || fail_build "could not extract lib/ from $REF"
( cd "$work/ref/lib" && for c in *.c; do
    gcc $CFLAGS -c "$c" -o "${c%.c}.o" || exit 1
  done && ar rcs libref.a ./*.o ) >"$work/ref.log" 2>&1 \
  || { sed 's/^/    /' "$work/ref.log" >&2; fail_build "the reference lib/ would not build"; }

# --- the working tree's, as it stands ----------------------------------------
mkdir -p "$work/new"
cp "$repo"/lib/*.c "$repo"/lib/*.h "$work/new/" || fail_build "could not copy lib/"
( cd "$work/new" && for c in *.c; do
    gcc $CFLAGS -c "$c" -o "${c%.c}.o" || exit 1
  done && ar rcs libnew.a ./*.o ) >"$work/new.log" 2>&1 \
  || { sed 's/^/    /' "$work/new.log" >&2; fail_build "the working tree's lib/ would not build"; }

# --- the probe, once against each --------------------------------------------
gcc $CFLAGS -I "$work/ref/lib" -o "$work/probe_ref" "$here/dsp_regress.c" \
    "$work/ref/lib/libref.a" $LIBS 2>"$work/pref.log" \
  || { sed 's/^/    /' "$work/pref.log" >&2
       fail_build "the probe will not build against $REF -- it calls something that was not there yet"; }
gcc $CFLAGS -I "$work/new" -o "$work/probe_new" "$here/dsp_regress.c" \
    "$work/new/libnew.a" $LIBS 2>"$work/pnew.log" \
  || { sed 's/^/    /' "$work/pnew.log" >&2; fail_build "the probe will not build against the working tree"; }

# --- the cases ---------------------------------------------------------------
# One per line: a label, then the probe's arguments after <out.bin>. Add a case
# by adding a line; keep the existing ones, since their whole value is that they
# have been the same for a long time.
run_case() {
  local label="$1"; shift
  "$work/probe_ref" "$1" "$work/a.bin" "${@:2}" >"$work/c.log" 2>&1
  local ra=$?
  "$work/probe_new" "$1" "$work/b.bin" "${@:2}" >>"$work/c.log" 2>&1
  local rb=$?
  if [ $ra -ne 0 ] || [ $rb -ne 0 ]; then
    printf '  %-46s ERROR (ref %d, new %d)\n' "$label" "$ra" "$rb"
    sed 's/^/      /' "$work/c.log"
    bad=$((bad + 1)); return
  fi
  if cmp -s "$work/a.bin" "$work/b.bin"; then
    printf '  %-46s igual  (%s bytes)\n' "$label" "$(stat -c%s "$work/a.bin")"
  else
    local n
    n=$(cmp -l "$work/a.bin" "$work/b.bin" 2>/dev/null | wc -l)
    printf '  %-46s DIFIERE en %s bytes\n' "$label" "$n"
    bad=$((bad + 1))
  fi
}

bad=0

echo "firwin2, on a model of the probe's own:"
run_case "flat then 24 dB/oct from 3 kHz, 1024 taps"  firwin2  1024 48000 3000 24
run_case "flat then 12 dB/oct from 120 Hz, 8192 taps" firwin2  8192 48000 120  12
run_case "-3 dB/octave tilt, 4096 taps"               tilt     4096 48000
run_case "the same, folded to minimum phase"          minphase 1024 48000 3000 24
run_case "44.1 kHz, so the grid is a different one"   firwin2  2048 44100 1000 18

echo
echo "<loudness>: the curve, firwin2 and minimum_phase, end to end:"
run_case "iso226-2003  70-80 phon, 4096 taps"   loudness  4096 48000 iso226-2003     70 80
run_case "iso226-2023  60-83 phon, 8192 taps"   loudness  8192 48000 iso226-2023     60 83
run_case "fletcher-munson 75-80 phon, 2048"     loudness  2048 48000 fletcher-munson 75 80
run_case "a-weighting 65-80 phon, 16384 taps"   loudness 16384 48000 a-weighting     65 80
run_case "iso226-2003 at 44.1 kHz, 4096 taps"   loudness  4096 44100 iso226-2003     70 80

echo
echo "<xtc> and <xtc_asym>: the whole ILD -> minimum phase -> recursion:"
run_case "itd 260 us, ild 3.0, az 30, 8192 taps" xtc 8192 48000 260 3.0 1.0 30
run_case "itd 300 us, ild 4.5, az 40, 4096 taps" xtc 4096 48000 300 4.5 1.2 40
run_case "asymmetric, 8192 taps"                 xtcasym 8192 48000 260 3.0 1.0 30 300 3.5 1.0 34

echo
echo "fft_convolve_truncate:"
run_case "8192 x 4096"  convolve 8192 4096
run_case "1000 x 333, neither a power of two" convolve 1000 333

echo
if [ "$bad" -eq 0 ]; then
  echo "dsp_regress: todo igual. lib/ produce exactamente lo mismo que en $REF."
  echo "             (lib/ y solo lib/: los generadores que viven en naconf.cpp"
  echo "              -- <low_and_high_filter>, <fir_filter> -- no se prueban aqui.)"
  exit 0
fi
echo "dsp_regress: $bad caso(s) distintos. Si el cambio es intencionado, dilo en el commit"
echo "             y en la documentacion del bloque afectado; si no, aqui esta."
exit 1
