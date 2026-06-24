#!/bin/bash
# =============================================================================
#  measure_pca4drc.sh
#
#  Measurement + impulse extraction + PCA template for the panambio system
#  of four loudspeakers (JACK + Echo AudioFire 4 card).
#
#  Derived from an old private ecasound_script.sh, with:
#    - Generation of the sweep and its inverse with sweepgen.py (GlSweep from
#      pyDRC in pure Python) as the first step (Phase 0).
#    - Automatic start of natambio before measuring, with the appropriate
#      configuration according to FULL_NATAMBIO (full/half) and SUBWOOFER
#      (subwoofer/normal); its stdout/stderr are redirected to NATAMBIO_LOG
#      (/tmp) and it is checked that it starts and registers its JACK ports.
#      ecasound sends the sweep to those input ports (OUT_PORTS).
#    - The four channels (front L/R, rear L/R) described in arrays and traversed in
#      a loop, instead of four nearly identical blocks copied by hand.
#    - Impulse extraction with fft_convolve.py (scipy, no pyDRC) instead of
#      lsconv.py / pyDRC.LsConv.
#    - A PCA step with pca4drc.py (when there are 2 or more measurements): for
#      each channel it generates i_<channel>/pca4drc/ with the WAVs of the PCA
#      components and their .raw. With a single measurement (NUM_POS=1) PCA is
#      NOT applied: the measured impulse is used.
#    - A final correction step with drc (Sbragion) per channel, using config.drc
#      and, as input, the principal PCA component PCA_0.raw (>=2 measurements)
#      or the directly measured impulse (1 measurement); it converts the
#      outputs to WAV.
#    - Separately enableable phases (DO_SWEEP / DO_MEASURE / DO_IMPULSES /
#      DO_PCA / DO_DRC) to allow re-processing without measuring again.
#    - Non-interactive mode (AUTO=1) and safe stop on errors (set -euo).
#
#  Usage:
#      ./measure_pca4drc.sh                 # the five phases, interactive
#      CALIBRATE=1 ./measure_pca4drc.sh     # only adjust GAIN_OUT/GAIN_IN gains
#      SELECT_INPUT=1 ./measure_pca4drc.sh  # choose the microphone input (IN_MEAS) via menu
#      AUTO=1 ./measure_pca4drc.sh          # no pauses (read)
#      SUBWOOFER=true ./measure_pca4drc.sh  # start natambio with subwoofer config
#      DO_SWEEP=0 ./measure_pca4drc.sh      # use an already existing sweep/inverse
#      DO_MEASURE=0 ./measure_pca4drc.sh    # skip measurement (re-process)
#      DO_MEASURE=0 DO_IMPULSES=0 ./measure_pca4drc.sh   # only PCA (+ DRC)
#      DO_DRC=0 ./measure_pca4drc.sh        # everything except the DRC correction
#      OUTPUT_LEN=65536 PCA_NORMALIZE=false ./measure_pca4drc.sh
#
#  Requires: ecasound + a running JACK server, the natambio executable, the
#  drc (Sbragion) binary with its config.drc, python3 with numpy/scipy/soundfile,
#  and the scripts sweepgen.py, fft_convolve.py, pca4drc.py, wav2raw.py and
#  raw2wav.py (next to this .sh by default; otherwise, export TOOLS_DIR pointing
#  to them).
# =============================================================================

set -euo pipefail

# --- Location of the python scripts (by default, next to this .sh) ------------
TOOLS_DIR="${TOOLS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
SWEEPGEN="$TOOLS_DIR/sweepgen.py"
FFT_CONVOLVE="$TOOLS_DIR/fft_convolve.py"
PCA4DRC="$TOOLS_DIR/pca4drc.py"
CHECK_CAPTURE="$TOOLS_DIR/check_capture.py"
WAV2RAW="$TOOLS_DIR/wav2raw.py"
RAW2WAV="$TOOLS_DIR/raw2wav.py"

# =============================================================================
# --- Sweep generation parameters (sweepgen.py / GlSweep) ----------------------
# Independent block: defines the excitation log-sweep and its inverse that are
# generated in Phase 0. They must be consistent with the measurement (SWEEP_RATE
# must match the capture sample rate, 48000 Hz).
# =============================================================================
SWEEP_RATE=${SWEEP_RATE:-48000}     # sweep sample rate (Hz)
SWEEP_AMPLITUDE=${SWEEP_AMPLITUDE:-0.5}   # sweep amplitude (peak)
SWEEP_HZSTART=${SWEEP_HZSTART:-20}        # start frequency (Hz)
SWEEP_HZEND=${SWEEP_HZEND:-20000}         # end frequency (Hz)
SWEEP_LENGTH=${SWEEP_LENGTH:-6}     # sweep duration (s)
SWEEP_SILENCE=${SWEEP_SILENCE:-1}   # silence at the start and end (s)
SWEEP_LEADIN=${SWEEP_LEADIN:-0.05}  # fraction of the sweep with a fade-in window
SWEEP_LEADOUT=${SWEEP_LEADOUT:-0.005} # fraction of the sweep with a fade-out window
# =============================================================================

# --- Measurement parameters ---------------------------------------------------
NUM_POS=${NUM_POS:-16}              # number of microphone positions
SWEEP=${SWEEP:-sweep_48k.wav}       # excitation sweep (generated in Phase 0)
INVERSE=${INVERSE:-inverse_48k.wav} # inverse sweep, for deconvolution (Phase 0)
IN_MEAS=${IN_MEAS:-system:capture_1}
GAIN_OUT=${GAIN_OUT:-0.0}           # dB at the output (playback)
GAIN_IN=${GAIN_IN:-10.0}            # dB at the input (capture)
REC_SECONDS=${REC_SECONDS:-10}      # duration of each capture

# --- Capture analysis parameters (check_capture.py) ---------------------------
MIN_LEVEL=${MIN_LEVEL:--40}         # dBFS below which "low level" is warned
MIN_SNR=${MIN_SNR:-20}              # dB below which "low SNR" is warned

# --- PCA parameters -----------------------------------------------------------
OUTPUT_LEN=${OUTPUT_LEN:-131072}    # length of the PCA component WAVs
PCA_NORMALIZE=${PCA_NORMALIZE:-true}

# =============================================================================
# --- DRC parameters (Phase 4: correction with the standard drc by Sbragion) ---
# For each channel drc is run with DRC_CONFIG (config.drc next to this script).
# --BCBaseDir is overridden with the channel's impulse folder (same level as the
# original p_left/, so the relative paths of the config, e.g.
# PSPointsobjetivo '../target/...', keep resolving) and --BCInFile with the
# principal component pca4drc/PCA_0.raw. When finished, the outputs DRC_PS_OUT
# and DRC_MS_OUT (defined in the config as PSOutFile/MSOutFile) are converted to
# WAV with raw2wav.py.
# =============================================================================
DRC_BIN=${DRC_BIN:-drc}                       # standard DRC binary
DRC_CONFIG=${DRC_CONFIG:-"$TOOLS_DIR/config.drc"}
DRC_PS_OUT=${DRC_PS_OUT:-rps.raw}             # = PSOutFile from the config
DRC_MS_OUT=${DRC_MS_OUT:-rms.raw}             # = MSOutFile from the config
# =============================================================================

# --- System configuration -----------------------------------------------------
# true  -> full NatAmbio: 4 loudspeakers (front L/R + rear L/R).
# false -> only 2 loudspeakers (front L/R).
FULL_NATAMBIO=${FULL_NATAMBIO:-true}
# true  -> system with subwoofer; false -> normal system. Selects the XML
# configuration with which natambio is started before measuring.
SUBWOOFER=${SUBWOOFER:-false}

# --- Launching natambio (the JACK client to measure) --------------------------
# Before measuring, natambio is started with the appropriate configuration
# according to FULL_NATAMBIO (full/half) and SUBWOOFER (subwoofer/normal).
# natambio exposes the input ports to which ecasound sends the sweep (see
# OUT_PORTS).
NATAMBIO_BIN=${NATAMBIO_BIN:-natambio}
NATAMBIO_LOG=${NATAMBIO_LOG:-/tmp/natambio_measure.log}  # stdout/stderr of natambio
if [ "$FULL_NATAMBIO" = "true" ]; then NAT_PREFIX=full; else NAT_PREFIX=half; fi
if [ "$SUBWOOFER" = "true" ]; then NAT_VARIANT=subwoofer; else NAT_VARIANT=normal; fi
NATAMBIO_CONFIG=${NATAMBIO_CONFIG:-"$TOOLS_DIR/${NAT_PREFIX}_natambio_measurements_${NAT_VARIANT}.xml"}

# --- Enableable phases --------------------------------------------------------
DO_SWEEP=${DO_SWEEP:-1}            # Phase 0: generate sweep and inverse
DO_MEASURE=${DO_MEASURE:-1}
DO_IMPULSES=${DO_IMPULSES:-1}
DO_PCA=${DO_PCA:-1}
DO_DRC=${DO_DRC:-1}               # Phase 4: correction with drc (Sbragion)
AUTO=${AUTO:-0}                     # 1 = no interactive pauses
CALIBRATE=${CALIBRATE:-0}           # 1 = only calibrate gains (GAIN_OUT/GAIN_IN)
SELECT_INPUT=${SELECT_INPUT:-0}     # 1 = choose IN_MEAS (microphone input) via menu before measuring

# In calibration mode only playback/recording is done to adjust levels: the
# impulse/PCA/DRC phases are disabled so as not to require their dependencies
# (drc, etc.).
if [ "$CALIBRATE" = "1" ]; then
    DO_MEASURE=1
    DO_IMPULSES=0
    DO_PCA=0
    DO_DRC=0
fi

# --- Definition of the channels (parallel arrays) ---------------------------------
# The four channels are defined; with FULL_NATAMBIO=false only the first two
# (front L/R) are used by trimming NUM_WAYS below.
#   label               natambio JACK input port             meas dir       impulse dir     sweep        impulse prefix
LABELS=(    "front left"  "front right"  "rear left"   "rear right" )
OUT_PORTS=( "natambio:front_input_left" "natambio:front_input_right" \
            "natambio:rear_input_left"  "natambio:rear_input_right" )
MEAS_DIRS=( "m_front_left"      "m_front_right"      "m_rear_left"  "m_rear_right" )
IMP_DIRS=(  "i_front_left"      "i_front_right"      "i_rear_left"  "i_rear_right" )
SWEEP_PRE=( "left_sweep"  "right_sweep"  "left_sweep"  "right_sweep" )
IMP_PRE=(   "front_left_impulse" "front_right_impulse" "rear_left_impulse" "rear_right_impulse" )

# 4 channels for the full NatAmbio, 2 (only front L/R) otherwise.
if [ "$FULL_NATAMBIO" = "true" ]; then
    NUM_WAYS=4
else
    NUM_WAYS=2
fi

pause() {
    # Wait for the user to press Enter, except in AUTO mode.
    if [ "$AUTO" != "1" ]; then
        read -r -p "$1"
    fi
}

# --- Preflight checks (jackd, ecasound, files, scripts) -----------------------
preflight() {
    local err=0
    if [ "$DO_SWEEP" = "1" ]; then
        [ -f "$SWEEPGEN" ] || { echo "ERROR: cannot find '$SWEEPGEN' (export TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_MEASURE" = "1" ]; then
        command -v ecasound >/dev/null 2>&1 || { echo "ERROR: ecasound is not installed."; err=1; }
        # Running JACK server (ecasound records/plays via JACK).
        if command -v jack_lsp >/dev/null 2>&1; then
            jack_lsp >/dev/null 2>&1 || { echo "ERROR: the JACK server is not responding; start it (jackd / qjackctl)."; err=1; }
        elif ! pgrep -x jackd >/dev/null 2>&1; then
            echo "WARNING: cannot find jack_lsp or the jackd process; make sure JACK is running."
        fi
        # The sweep is generated in Phase 0; it is only required to exist if not generated.
        if [ "$DO_SWEEP" != "1" ]; then
            [ -f "$SWEEP" ] || { echo "ERROR: the excitation sweep '$SWEEP' does not exist."; err=1; }
        fi
        [ -f "$CHECK_CAPTURE" ] || { echo "ERROR: cannot find '$CHECK_CAPTURE' (export TOOLS_DIR)."; err=1; }
        # natambio is started before measuring: check binary and configuration.
        command -v "$NATAMBIO_BIN" >/dev/null 2>&1 || { echo "ERROR: cannot find the natambio executable ('$NATAMBIO_BIN')."; err=1; }
        [ -f "$NATAMBIO_CONFIG" ] || { echo "ERROR: the natambio configuration '$NATAMBIO_CONFIG' does not exist."; err=1; }
    fi
    if [ "$DO_IMPULSES" = "1" ]; then
        # The inverse is also generated in Phase 0; same criterion.
        if [ "$DO_SWEEP" != "1" ]; then
            [ -f "$INVERSE" ] || { echo "ERROR: the inverse sweep '$INVERSE' does not exist."; err=1; }
        fi
        [ -f "$FFT_CONVOLVE" ] || { echo "ERROR: cannot find '$FFT_CONVOLVE' (export TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_PCA" = "1" ]; then
        [ -f "$PCA4DRC" ] || { echo "ERROR: cannot find '$PCA4DRC' (export TOOLS_DIR)."; err=1; }
        [ -f "$WAV2RAW" ] || { echo "ERROR: cannot find '$WAV2RAW' (export TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_DRC" = "1" ]; then
        command -v "$DRC_BIN" >/dev/null 2>&1 || { echo "ERROR: cannot find the DRC binary ('$DRC_BIN')."; err=1; }
        [ -f "$DRC_CONFIG" ] || { echo "ERROR: the DRC configuration '$DRC_CONFIG' does not exist."; err=1; }
        [ -f "$RAW2WAV" ] || { echo "ERROR: cannot find '$RAW2WAV' (export TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_SWEEP" = "1" ] || [ "$DO_MEASURE" = "1" ] || [ "$DO_IMPULSES" = "1" ] || [ "$DO_PCA" = "1" ] || [ "$DO_DRC" = "1" ]; then
        command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 is not installed."; err=1; }
    fi
    [ "$err" = "0" ] || { echo "Aborting due to failed preflight checks."; exit 1; }
    echo "Preflight checks OK."
}

# --- Analysis of a capture: warns of clipping / low level / low SNR -----------
check_capture() {
    # Delegates to check_capture.py. Returns 0 if the capture is valid and !=0 if
    # the levels are not correct (clipping / low level / low SNR) and the
    # measurement must be repeated. It is invoked inside an 'if', so it does not
    # break 'set -e'.
    python3 "$CHECK_CAPTURE" "$1" "$2" \
        --min-level "$MIN_LEVEL" --min-snr "$MIN_SNR"
}

# --- Plays the sweep through a channel and records the microphone response ---------
run_ecasound_capture() {
    # $1 output port (natambio input), $2 output gain (dB),
    # $3 input gain (dB), $4 output WAV file.
    ecasound -t:"$REC_SECONDS" \
        -a:1 -i "$SWEEP" -a:1 -o:jack_auto,"$1" -a:1 -eadb:"$2" \
        -a:2 -i:jack_auto,"$IN_MEAS" -a:2 -f:f32_le,1,48000 \
        -o:"$4" -a:2 -eadb:"$3" -ev
}

# --- Parameter XML for sweepgen.py --------------------------------------------
write_sweep_xml() {
    # Writes to "$1" the XML <generate_sweep> with the parameters of the sweep
    # generation block (SWEEP_*) and the file names SWEEP / INVERSE.
    cat > "$1" <<XML
<?xml version="1.0"?>
<generate_sweep>
  <params>
    <sample_rate>$SWEEP_RATE</sample_rate>
    <amplitude>$SWEEP_AMPLITUDE</amplitude>
    <Hzstart>$SWEEP_HZSTART</Hzstart>
    <Hzend>$SWEEP_HZEND</Hzend>
    <length>$SWEEP_LENGTH</length>
    <silence>$SWEEP_SILENCE</silence>
    <leadin>$SWEEP_LEADIN</leadin>
    <leadout>$SWEEP_LEADOUT</leadout>
  </params>
  <sweep_filename>$SWEEP</sweep_filename>
  <inverse_filename>$INVERSE</inverse_filename>
</generate_sweep>
XML
}

# --- Start / stop of natambio (the JACK client to measure) --------------------
NATAMBIO_PID=""
start_natambio() {
    # Starts natambio in the background with NATAMBIO_CONFIG, redirecting its
    # stdout/stderr to NATAMBIO_LOG so as not to clutter the script output. It
    # waits until it registers its JACK ports; aborts if it dies or does not
    # appear in time.
    echo "Starting natambio with '$(basename "$NATAMBIO_CONFIG")' (log: $NATAMBIO_LOG)..."
    "$NATAMBIO_BIN" -quiet "$NATAMBIO_CONFIG" >"$NATAMBIO_LOG" 2>&1 &
    NATAMBIO_PID=$!
    # Ensures natambio is stopped no matter what happens from here on.
    trap stop_natambio EXIT

    local tries=0
    while [ "$tries" -lt 50 ]; do          # up to ~5 s (50 x 0.1 s)
        if ! kill -0 "$NATAMBIO_PID" 2>/dev/null; then
            echo "ERROR: natambio terminated unexpectedly. Last lines of the log:"
            tail -n 20 "$NATAMBIO_LOG" | sed 's/^/    /'
            NATAMBIO_PID=""
            exit 1
        fi
        if jack_lsp 2>/dev/null | grep -q '^natambio:'; then
            echo "natambio running (PID $NATAMBIO_PID); JACK ports registered."
            return 0
        fi
        sleep 0.1
        tries=$((tries + 1))
    done
    echo "ERROR: natambio did not register its JACK ports in time. Last lines of the log:"
    tail -n 20 "$NATAMBIO_LOG" | sed 's/^/    /'
    stop_natambio
    exit 1
}

stop_natambio() {
    # Stops natambio with SIGINT (its normal channel of stopping). If it does not
    # finish within a couple of seconds, it escalates to SIGTERM and, as a last
    # resort, SIGKILL, so as not to leave the script hanging on exit.
    if [ -n "$NATAMBIO_PID" ] && kill -0 "$NATAMBIO_PID" 2>/dev/null; then
        echo "Stopping natambio (PID $NATAMBIO_PID)..."
        local sig
        for sig in INT TERM KILL; do
            kill "-$sig" "$NATAMBIO_PID" 2>/dev/null || true
            local tries=0
            while [ "$tries" -lt 20 ]; do      # up to ~2 s per signal
                kill -0 "$NATAMBIO_PID" 2>/dev/null || break 2
                sleep 0.1
                tries=$((tries + 1))
            done
        done
        wait "$NATAMBIO_PID" 2>/dev/null || true
    fi
    NATAMBIO_PID=""
}

# --- natambio configuration report + user confirmation ------------------------
report_natambio_routing() {
    # After starting natambio, shows the mode (full/half), the variant
    # (subwoofer) and to which card outputs (system:) the natambio outputs are
    # actually connected, querying the live JACK connections. Asks the user for
    # confirmation before measuring.
    echo
    echo "================= Measurement configuration ================="
    echo "  natambio mode  : $NAT_PREFIX   (FULL_NATAMBIO=$FULL_NATAMBIO)"
    echo "  subwoofer mode : $SUBWOOFER"
    echo
    echo "  Ways to measure (ecasound -> natambio input):"
    local w
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        printf "    %-12s -> %s\n" "${LABELS[$w]}" "${OUT_PORTS[$w]}"
    done
    echo
    echo "  natambio outputs connected to the card (system:):"
    if command -v jack_lsp >/dev/null 2>&1; then
        local nat_out=()
        mapfile -t nat_out < <(jack_lsp -o 2>/dev/null | grep '^natambio:')
        if [ "${#nat_out[@]}" -eq 0 ]; then
            echo "    (no natambio output ports were found)"
        fi
        local port conn found
        for port in "${nat_out[@]}"; do
            found=0
            # jack_lsp -c <port> lists the port and, indented, its connections.
            while IFS= read -r conn; do
                [ -z "$conn" ] && continue
                printf "    %-28s -> %s\n" "$port" "$conn"
                found=1
            done < <(jack_lsp -c "$port" 2>/dev/null | grep -E '^[[:space:]]' | sed 's/^[[:space:]]*//')
            [ "$found" -eq 0 ] && printf "    %-28s -> (no connection)\n" "$port"
        done
    else
        echo "    (jack_lsp not available; cannot query the routing)"
    fi
    echo "============================================================="
    echo "Please confirm that the assignment is correct."
    pause "Press Enter to confirm and start the measurement..."
}

# --- Interactive selection of the destination JACK port per channel ---------------
# NOTE: not used for now (natambio fixes OUT_PORTS with its own labels), but it
# is kept in case ports need to be reassigned by hand later on.
select_jack_ports() {
    # Queries the available JACK input ports (sweep destinations: the card
    # outputs system:playback_* and the inputs of other applications, e.g.
    # natambio:*_input_*) and allows assigning one to each channel via a numeric
    # menu, overwriting OUT_PORTS[w]. Only in interactive mode (without AUTO) and
    # if jack_lsp is available.
    if [ "$AUTO" = "1" ]; then
        return 0
    fi
    if ! command -v jack_lsp >/dev/null 2>&1; then
        echo "WARNING: jack_lsp not available; the default ports are used."
        return 0
    fi

    # JACK input ports (destinations). The ecasound ones are excluded in case it
    # is already running, so as not to autoconnect.
    local ports=()
    mapfile -t ports < <(jack_lsp -i 2>/dev/null | grep -v -i '^ecasound:')
    if [ "${#ports[@]}" -eq 0 ]; then
        echo "WARNING: there are no JACK input ports available; the default ones are used."
        return 0
    fi

    echo
    echo "Available JACK input ports (sweep destination):"
    local idx
    for idx in "${!ports[@]}"; do
        printf "  %2d) %s\n" "$((idx + 1))" "${ports[$idx]}"
    done
    echo "   0) keep the current value"
    echo "Assign the ecasound output port for each measurement channel:"

    local w sel
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        while true; do
            read -r -p "  Way '${LABELS[$w]}' [current: ${OUT_PORTS[$w]}] -> port no. (0=keep): " sel
            if [ -z "$sel" ] || [ "$sel" = "0" ]; then
                echo "    kept: ${OUT_PORTS[$w]}"
                break
            fi
            if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#ports[@]}" ]; then
                OUT_PORTS[$w]="${ports[$((sel - 1))]}"
                echo "    ${LABELS[$w]} -> ${OUT_PORTS[$w]}"
                break
            fi
            echo "    Invalid input. Enter a number between 0 and ${#ports[@]}."
        done
    done
    echo
}

# --- Interactive selection of the microphone input (IN_MEAS) ------------------
select_input_port() {
    # Lists the source JACK ports (capture ports, e.g. system:capture_*: that is
    # where the microphone audio comes from) and allows assigning one as IN_MEAS
    # via a numeric menu. Only acts if SELECT_INPUT=1, in interactive mode
    # (without AUTO) and with jack_lsp available; otherwise, the current IN_MEAS
    # is kept (by default system:capture_1).
    if [ "$SELECT_INPUT" != "1" ] || [ "$AUTO" = "1" ]; then
        return 0
    fi
    if ! command -v jack_lsp >/dev/null 2>&1; then
        echo "WARNING: jack_lsp not available; IN_MEAS=$IN_MEAS is used."
        return 0
    fi

    # Source JACK ports (captures). The ecasound ones are excluded in case it is
    # already running, so as not to autoconnect to its own output.
    local ports=()
    mapfile -t ports < <(jack_lsp -o 2>/dev/null | grep -v -i '^ecasound:')
    if [ "${#ports[@]}" -eq 0 ]; then
        echo "WARNING: there are no JACK capture ports available; IN_MEAS=$IN_MEAS is used."
        return 0
    fi

    echo
    echo "Available JACK capture ports (microphone source):"
    local idx
    for idx in "${!ports[@]}"; do
        printf "  %2d) %s\n" "$((idx + 1))" "${ports[$idx]}"
    done
    echo "   0) keep the current value"

    local sel
    while true; do
        read -r -p "Microphone input [current: $IN_MEAS] -> port no. (0=keep): " sel
        if [ -z "$sel" ] || [ "$sel" = "0" ]; then
            echo "    kept: IN_MEAS=$IN_MEAS"
            break
        fi
        if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#ports[@]}" ]; then
            IN_MEAS="${ports[$((sel - 1))]}"
            echo "    IN_MEAS=$IN_MEAS"
            break
        fi
        echo "    Invalid input. Enter a number between 0 and ${#ports[@]}."
    done
    echo
}

# --- Gain calibration (CALIBRATE=1): a common GAIN_OUT/GAIN_IN -----------------
calibrate_gains() {
    # Plays the sweep through each channel at the current gains, records to a
    # temporary WAV and analyzes levels with check_capture. The user keeps trying
    # until finding a common GAIN_OUT/GAIN_IN valid for all channels. It saves nothing
    # for DRC: it only serves to adjust levels before measuring.
    echo "### Gain calibration (common to all channels)"
    start_natambio
    select_input_port
    report_natambio_routing
    local cal_dir gout gin ans ngout ngin okall w
    cal_dir="$(mktemp -d)"
    gout="$GAIN_OUT"; gin="$GAIN_IN"
    while true; do
        echo
        echo "Trying GAIN_OUT=$gout dB / GAIN_IN=$gin dB over the $NUM_WAYS channels..."
        okall=1
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            echo "--- ${LABELS[$w]} ---"
            run_ecasound_capture "${OUT_PORTS[$w]}" "$gout" "$gin" "$cal_dir/cal.wav"
            if check_capture "$cal_dir/cal.wav" "${LABELS[$w]}"; then
                echo "    -> levels OK"
            else
                echo "    -> levels NOT valid (clipping / low level / low SNR)"
                okall=0
            fi
        done
        echo
        if [ "$okall" = "1" ]; then
            echo "All channels with correct levels at GAIN_OUT=$gout / GAIN_IN=$gin."
        else
            echo "Some channel does not pass: adjust the gains (and/or the microphone preamp)."
        fi
        [ "$AUTO" = "1" ] && break
        read -r -p "Enter to accept, or new 'GAIN_OUT GAIN_IN' (e.g. -3 12): " ans
        [ -z "$ans" ] && break
        read -r ngout ngin <<< "$ans"
        if [[ "$ngout" =~ ^-?[0-9]+(\.[0-9]+)?$ ]] && [[ "$ngin" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
            gout="$ngout"; gin="$ngin"
        else
            echo "Invalid format. Write TWO numbers: GAIN_OUT GAIN_IN (e.g. -3 12)."
        fi
    done
    rm -rf "$cal_dir"
    stop_natambio
    trap - EXIT
    echo
    echo "================= Recommended gains ================="
    echo "  GAIN_OUT=$gout    GAIN_IN=$gin"
    echo "  Use them in the measurement, for example:"
    echo "      GAIN_OUT=$gout GAIN_IN=$gin ./measure_pca4drc.sh"
    echo "========================================================="
}

preflight

# --- Phase 0: generation of the sweep and its inverse -------------------------
if [ "$DO_SWEEP" = "1" ]; then
    echo "### Phase 0: generation of the sweep and inverse (sweepgen.py)"
    # sweepgen.py is fed an XML <generate_sweep>; it is built on the fly with
    # write_sweep_xml and the output names are overridden with -s / -i so that
    # they match SWEEP / INVERSE.
    SWEEP_XML="$(mktemp)"
    trap 'rm -f "$SWEEP_XML"' EXIT
    write_sweep_xml "$SWEEP_XML"
    python3 "$SWEEPGEN" "$SWEEP_XML" -s "$SWEEP" -i "$INVERSE"
    rm -f "$SWEEP_XML"
    trap - EXIT
fi

# --- Calibration mode: adjusts common GAIN_OUT/GAIN_IN and finishes -----------
if [ "$CALIBRATE" = "1" ]; then
    calibrate_gains
    exit 0
fi

# --- Phase 1: measurement -----------------------------------------------------
if [ "$DO_MEASURE" = "1" ]; then
    echo "### Phase 1: sweep measurement ($NUM_POS positions x $NUM_WAYS channels)"
    # Starts natambio with the corresponding configuration; exposes the input
    # ports (OUT_PORTS) to which ecasound will send the sweep.
    start_natambio
    # If SELECT_INPUT=1, allows choosing the microphone input (IN_MEAS) via menu.
    select_input_port
    # Report of mode/subwoofer and actual routing of natambio outputs to the
    # card, with user confirmation before measuring.
    report_natambio_routing

    for w in $(seq 0 $((NUM_WAYS - 1))); do
        mkdir -p "${MEAS_DIRS[$w]}"
    done

    for i in $(seq 1 "$NUM_POS"); do
        printf -v n "%02d" "$i"
        echo "--- Microphone position $n/$NUM_POS ---"
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            echo "Measurement ${LABELS[$w]} (position $n)"
            # Repeat the measurement until the levels are correct. If the levels
            # are not valid, it warns and waits for the user to readjust their
            # microphone preamp gain before measuring again.
            while true; do
                pause "Press Enter to measure ${LABELS[$w]}..."
                run_ecasound_capture "${OUT_PORTS[$w]}" "$GAIN_OUT" "$GAIN_IN" \
                    "${MEAS_DIRS[$w]}/${SWEEP_PRE[$w]}_$i.wav"
                if check_capture "${MEAS_DIRS[$w]}/${SWEEP_PRE[$w]}_$i.wav" "${LABELS[$w]}"; then
                    break
                fi
                echo "*** The recording of '${LABELS[$w]}' (position $n) is NOT correct due to levels: it must be repeated. ***"
                echo "    Readjust the gain on the microphone preamp before continuing."
                if [ "$AUTO" = "1" ]; then
                    echo "    (AUTO=1: cannot repeat without intervention; continuing with this capture.)"
                    break
                fi
                pause "Press Enter to repeat the measurement of ${LABELS[$w]}..."
            done
        done
        echo "Move the microphone to a different place."
    done
    # Measurement finished: natambio is no longer needed.
    stop_natambio
    trap - EXIT
fi

# --- Phase 2: impulse extraction (FFT deconvolution) --------------------------
if [ "$DO_IMPULSES" = "1" ]; then
    echo "### Phase 2: impulse extraction (fft_convolve.py)"
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        mkdir -p "${IMP_DIRS[$w]}"
    done

    for i in $(seq 1 "$NUM_POS"); do
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            python3 "$FFT_CONVOLVE" \
                "${MEAS_DIRS[$w]}/${SWEEP_PRE[$w]}_$i.wav" \
                "$INVERSE" \
                "${IMP_DIRS[$w]}/${IMP_PRE[$w]}_$i.wav"
        done
    done
fi

# --- Phase 3: reference impulse for DRC ---------------------------------------
# With 2 or more measurements it is obtained by PCA (principal component, PCA_0).
# With a single measurement (NUM_POS=1) there is no PCA possible or necessary:
# the measured impulse itself is used as DRC input (only converted to .raw).
if [ "$DO_PCA" = "1" ]; then
    if [ "$NUM_POS" -le 1 ]; then
        echo "### Phase 3: 1 measurement -> no PCA; the measured impulse is used directly"
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            imp="${IMP_DIRS[$w]}/${IMP_PRE[$w]}_1.wav"
            if [ -f "$imp" ]; then
                echo "Conversion to .raw (DRC format) of $imp"
                python3 "$WAV2RAW" "$imp"
            else
                echo "WARNING: '$imp' does not exist; nothing to prepare for ${LABELS[$w]}."
            fi
        done
    else
        echo "### Phase 3: PCA of the impulses (pca4drc.py) + conversion to .raw"
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            echo "PCA of ${LABELS[$w]} -> ${IMP_DIRS[$w]}/pca4drc/"
            python3 "$PCA4DRC" "${IMP_DIRS[$w]}" "$OUTPUT_LEN" --normalize "$PCA_NORMALIZE"
            # Converts the WAV components to .raw (float 32-bit LE) for DRC.
            pca_dir="${IMP_DIRS[$w]}/pca4drc"
            if compgen -G "$pca_dir/*.wav" >/dev/null; then
                echo "Conversion to .raw (DRC format) in $pca_dir/"
                python3 "$WAV2RAW" "$pca_dir"/*.wav
            fi
        done
    fi
fi

# --- Phase 4: correction with DRC (standard drc) ------------------------------
if [ "$DO_DRC" = "1" ]; then
    echo "### Phase 4: correction with DRC ($DRC_BIN + $(basename "$DRC_CONFIG"))"
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        base="${IMP_DIRS[$w]}"
        # DRC input: with a single measurement, the directly measured impulse (no
        # PCA); with 2 or more, the principal PCA component (PCA_0.raw).
        if [ "$NUM_POS" -le 1 ]; then
            in_rel="${IMP_PRE[$w]}_1.raw"
        else
            in_rel="pca4drc/PCA_0.raw"
        fi
        if [ ! -f "$base/$in_rel" ]; then
            echo "WARNING: '$base/$in_rel' does not exist; DRC skipped for ${LABELS[$w]}."
            continue
        fi
        echo "DRC of ${LABELS[$w]}: --BCBaseDir=$base/ --BCInFile=$in_rel"
        # BaseDir = the channel's impulse folder (same level as the original
        # p_left/). Since --BCBaseDir goes on the command line, it also affects
        # --BCInFile (-> $base/$in_rel) and the config's outputs/relative paths
        # (rps.raw/rms.raw -> $base/, ../target/... -> measurement root).
        if "$DRC_BIN" --BCBaseDir="$base/" --BCInFile="$in_rel" "$DRC_CONFIG"; then
            # Converts the DRC outputs rps.raw / rms.raw to WAV.
            for out in "$DRC_PS_OUT" "$DRC_MS_OUT"; do
                if [ -f "$base/$out" ]; then
                    python3 "$RAW2WAV" "$base/$out" --rate "$SWEEP_RATE"
                else
                    echo "WARNING: DRC did not generate '$base/$out'."
                fi
            done
        else
            echo "ERROR: DRC failed for ${LABELS[$w]} (continuing with the other channels)."
        fi
    done
fi

echo "Done."
