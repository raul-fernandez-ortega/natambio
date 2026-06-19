#!/bin/sh
# Generate /usr/bin wrappers for the NatAmbio Python tools.
# The actual scripts live in /usr/share/natambio/<toolkit>/ ; each wrapper is a
# thin launcher so the toolkits keep their original on-disk layout (the data
# dirs target/ config/ mic/ stay next to the scripts) without patching code.
#
# Usage: gen-wrappers.sh <bindir>
set -eu

BINDIR="$1"
PCA="/usr/share/natambio/python_pca4drc"
NAE="/usr/share/natambio/python_nae_natambio"

install -d "$BINDIR"

# wrap <command-name> <absolute-script-path> [extra env line]
wrap() {
    cmd="$1"; script="$2"; env_line="${3:-}"
    {
        echo '#!/bin/sh'
        [ -n "$env_line" ] && echo "$env_line"
        echo "exec python3 \"$script\" \"\$@\""
    } > "$BINDIR/$cmd"
    chmod 0755 "$BINDIR/$cmd"
}

wrap natambio-pca4drc       "$PCA/pca4drc.py"
wrap natambio-sweepgen      "$PCA/sweepgen.py"
wrap natambio-fft-convolve  "$PCA/fft_convolve.py"
wrap natambio-check-capture "$PCA/check_capture.py"
wrap natambio-wav2raw       "$PCA/wav2raw.py"
wrap natambio-raw2wav       "$PCA/raw2wav.py"

wrap natambio-nae           "$NAE/nae_natambio.py"
