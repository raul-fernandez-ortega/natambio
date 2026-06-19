#!/bin/bash
# =============================================================================
#  measure_pca4drc.sh
#
#  Plantilla de medición + extracción de impulsos + PCA para el sistema panambio
#  de cuatro altavoces (JACK + tarjeta Echo AudioFire 4).
#
#  Derivado de ~/Measurements/panambio_subwoofer_02/ecasound_script.sh, con:
#    - Generación del sweep e inversa con sweepgen.py (GlSweep de pyDRC en Python
#      puro) como primer paso (Fase 0).
#    - Arranque automático de natambio antes de medir, con la configuración
#      adecuada según FULL_NATAMBIO (full/half) y SUBWOOFER (subwoofer/normal);
#      su stdout/stderr se derivan a NATAMBIO_LOG (/tmp) y se comprueba que
#      arranca y registra sus puertos JACK. ecasound envía el sweep a esos
#      puertos de entrada (OUT_PORTS).
#    - Las cuatro vías (front L/R, rear L/R) descritas en arrays y recorridas en
#      bucle, en vez de cuatro bloques casi idénticos copiados a mano.
#    - Extracción de impulsos con fft_convolve.py (scipy, sin pyDRC) en lugar de
#      lsconv.py / pyDRC.LsConv.
#    - Un paso de PCA con pca4drc.py: por cada vía genera un subdirectorio
#      i_<via>/pca4drc/ con los WAV de las componentes PCA y sus .raw para DRC.
#    - Un paso final de corrección con drc (Sbragion) por vía, usando config.drc
#      y la componente principal PCA_0.raw; convierte las salidas a WAV.
#    - Fases activables por separado (DO_SWEEP / DO_MEASURE / DO_IMPULSES /
#      DO_PCA / DO_DRC) para poder re-procesar sin volver a medir.
#    - Modo no interactivo (AUTO=1) y parada segura ante errores (set -euo).
#
#  Uso:
#      ./measure_pca4drc.sh                 # las cinco fases, interactivo
#      AUTO=1 ./measure_pca4drc.sh          # sin pausas (read)
#      SUBWOOFER=true ./measure_pca4drc.sh  # arrancar natambio con config subwoofer
#      DO_SWEEP=0 ./measure_pca4drc.sh      # usar un sweep/inversa ya existentes
#      DO_MEASURE=0 ./measure_pca4drc.sh    # saltar medición (re-procesar)
#      DO_MEASURE=0 DO_IMPULSES=0 ./measure_pca4drc.sh   # sólo PCA (+ DRC)
#      DO_DRC=0 ./measure_pca4drc.sh        # todo menos la corrección DRC
#      OUTPUT_LEN=65536 PCA_NORMALIZE=false ./measure_pca4drc.sh
#
#  Requiere: ecasound + servidor JACK en marcha, el ejecutable natambio, el
#  binario drc (Sbragion) con su config.drc, python3 con numpy/scipy/soundfile, y
#  los scripts sweepgen.py, fft_convolve.py, pca4drc.py, wav2raw.py y raw2wav.py
#  (junto a este .sh por defecto; si no, exporta TOOLS_DIR apuntando a ellos).
# =============================================================================

set -euo pipefail

# --- Localización de los scripts python (por defecto, junto a este .sh) -------
TOOLS_DIR="${TOOLS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
SWEEPGEN="$TOOLS_DIR/sweepgen.py"
FFT_CONVOLVE="$TOOLS_DIR/fft_convolve.py"
PCA4DRC="$TOOLS_DIR/pca4drc.py"
CHECK_CAPTURE="$TOOLS_DIR/check_capture.py"
WAV2RAW="$TOOLS_DIR/wav2raw.py"
RAW2WAV="$TOOLS_DIR/raw2wav.py"

# =============================================================================
# --- Parámetros de generación del sweep (sweepgen.py / GlSweep) ---------------
# Bloque independiente: define el log-sweep de excitación y su inversa que se
# generan en la Fase 0. Deben ser coherentes con la medición (SWEEP_RATE debe
# coincidir con la frecuencia de muestreo de captura, 48000 Hz).
# =============================================================================
SWEEP_RATE=${SWEEP_RATE:-48000}     # frecuencia de muestreo del sweep (Hz)
SWEEP_AMPLITUDE=${SWEEP_AMPLITUDE:-0.5}   # amplitud (pico) del sweep
SWEEP_HZSTART=${SWEEP_HZSTART:-20}        # frecuencia inicial (Hz)
SWEEP_HZEND=${SWEEP_HZEND:-20000}         # frecuencia final (Hz)
SWEEP_LENGTH=${SWEEP_LENGTH:-6}     # duración del barrido (s)
SWEEP_SILENCE=${SWEEP_SILENCE:-1}   # silencio al inicio y al final (s)
SWEEP_LEADIN=${SWEEP_LEADIN:-0.05}  # fracción del barrido con ventana de entrada
SWEEP_LEADOUT=${SWEEP_LEADOUT:-0.005} # fracción del barrido con ventana de salida
# =============================================================================

# --- Parámetros de medición ---------------------------------------------------
NUM_POS=${NUM_POS:-16}              # número de posiciones de micrófono
SWEEP=${SWEEP:-sweep_48k.wav}       # sweep de excitación (se genera en la Fase 0)
INVERSE=${INVERSE:-inverse_48k.wav} # sweep inverso, para deconvolución (Fase 0)
IN_MEAS=${IN_MEAS:-system:capture_1}
GAIN_OUT=${GAIN_OUT:-0.0}           # dB a la salida (reproducción)
GAIN_IN=${GAIN_IN:-10.0}            # dB a la entrada (captura)
REC_SECONDS=${REC_SECONDS:-10}      # duración de cada captura

# --- Parámetros de análisis de captura (check_capture.py) ---------------------
MIN_LEVEL=${MIN_LEVEL:--40}         # dBFS por debajo del cual se avisa "nivel bajo"
MIN_SNR=${MIN_SNR:-20}              # dB por debajo del cual se avisa "SNR baja"

# --- Parámetros de PCA --------------------------------------------------------
OUTPUT_LEN=${OUTPUT_LEN:-131072}    # longitud de los WAV de las componentes PCA
PCA_NORMALIZE=${PCA_NORMALIZE:-true}

# =============================================================================
# --- Parámetros de DRC (Fase 4: corrección con drc estándar de Sbragion) ------
# Por cada vía se ejecuta drc con DRC_CONFIG (config.drc junto a este script).
# Se sobrescribe --BCBaseDir con la carpeta de impulsos de la vía (igual nivel
# que el p_left/ original, para que las rutas relativas del config, p.ej.
# PSPointsobjetivo '../target/...', sigan resolviendo) y --BCInFile con la
# componente principal pca4drc/PCA_0.raw. Al terminar, las salidas DRC_PS_OUT y
# DRC_MS_OUT (definidas en el config como PSOutFile/MSOutFile) se convierten a
# WAV con raw2wav.py.
# =============================================================================
DRC_BIN=${DRC_BIN:-drc}                       # binario DRC estándar
DRC_CONFIG=${DRC_CONFIG:-"$TOOLS_DIR/config.drc"}
DRC_PS_OUT=${DRC_PS_OUT:-rps.raw}             # = PSOutFile del config
DRC_MS_OUT=${DRC_MS_OUT:-rms.raw}             # = MSOutFile del config
# =============================================================================

# --- Configuración del sistema ------------------------------------------------
# true  -> NatAmbio completo: 4 altavoces (front L/R + rear L/R).
# false -> sólo 2 altavoces (front L/R).
FULL_NATAMBIO=${FULL_NATAMBIO:-true}
# true  -> sistema con subwoofer; false -> sistema normal. Selecciona el XML de
# configuración con el que se arranca natambio antes de medir.
SUBWOOFER=${SUBWOOFER:-false}

# --- Lanzamiento de natambio (cliente JACK a medir) ---------------------------
# Antes de medir se arranca natambio con la configuración adecuada según
# FULL_NATAMBIO (full/half) y SUBWOOFER (subwoofer/normal). natambio expone los
# puertos de entrada a los que ecasound envía el sweep (ver OUT_PORTS).
NATAMBIO_BIN=${NATAMBIO_BIN:-natambio}
NATAMBIO_LOG=${NATAMBIO_LOG:-/tmp/natambio_measure.log}  # stdout/stderr de natambio
if [ "$FULL_NATAMBIO" = "true" ]; then NAT_PREFIX=full; else NAT_PREFIX=half; fi
if [ "$SUBWOOFER" = "true" ]; then NAT_VARIANT=subwoofer; else NAT_VARIANT=normal; fi
NATAMBIO_CONFIG=${NATAMBIO_CONFIG:-"$TOOLS_DIR/${NAT_PREFIX}_natambio_measurements_${NAT_VARIANT}.xml"}

# --- Fases activables ---------------------------------------------------------
DO_SWEEP=${DO_SWEEP:-1}            # Fase 0: generar sweep e inversa
DO_MEASURE=${DO_MEASURE:-1}
DO_IMPULSES=${DO_IMPULSES:-1}
DO_PCA=${DO_PCA:-1}
DO_DRC=${DO_DRC:-1}               # Fase 4: corrección con drc (Sbragion)
AUTO=${AUTO:-0}                     # 1 = sin pausas interactivas

# --- Definición de las vías (arrays paralelos) --------------------------------
# Se definen las cuatro vías; con FULL_NATAMBIO=false sólo se usan las dos
# primeras (front L/R) recortando NUM_WAYS más abajo.
#   etiqueta            puerto de entrada JACK de natambio   dir. medidas   dir. impulsos   sweep        prefijo impulso
LABELS=(    "front left"  "front right"  "rear left"   "rear right" )
OUT_PORTS=( "natambio:front_input_left" "natambio:front_input_right" \
            "natambio:rear_input_left"  "natambio:rear_input_right" )
MEAS_DIRS=( "m_front_left"      "m_front_right"      "m_rear_left"  "m_rear_right" )
IMP_DIRS=(  "i_front_left"      "i_front_right"      "i_rear_left"  "i_rear_right" )
SWEEP_PRE=( "left_sweep"  "right_sweep"  "left_sweep"  "right_sweep" )
IMP_PRE=(   "front_left_impulse" "front_right_impulse" "rear_left_impulse" "rear_right_impulse" )

# 4 vías para el NatAmbio completo, 2 (sólo front L/R) en caso contrario.
if [ "$FULL_NATAMBIO" = "true" ]; then
    NUM_WAYS=4
else
    NUM_WAYS=2
fi

pause() {
    # Espera a que el usuario pulse Enter, salvo en modo AUTO.
    if [ "$AUTO" != "1" ]; then
        read -r -p "$1"
    fi
}

# --- Comprobaciones previas (jackd, ecasound, ficheros, scripts) --------------
preflight() {
    local err=0
    if [ "$DO_SWEEP" = "1" ]; then
        [ -f "$SWEEPGEN" ] || { echo "ERROR: no encuentro '$SWEEPGEN' (exporta TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_MEASURE" = "1" ]; then
        command -v ecasound >/dev/null 2>&1 || { echo "ERROR: ecasound no está instalado."; err=1; }
        # Servidor JACK en marcha (ecasound graba/reproduce vía JACK).
        if command -v jack_lsp >/dev/null 2>&1; then
            jack_lsp >/dev/null 2>&1 || { echo "ERROR: el servidor JACK no responde; arráncalo (jackd / qjackctl)."; err=1; }
        elif ! pgrep -x jackd >/dev/null 2>&1; then
            echo "AVISO: no encuentro jack_lsp ni el proceso jackd; asegúrate de que JACK está en marcha."
        fi
        # El sweep se genera en la Fase 0; sólo se exige que exista si no se genera.
        if [ "$DO_SWEEP" != "1" ]; then
            [ -f "$SWEEP" ] || { echo "ERROR: no existe el sweep de excitación '$SWEEP'."; err=1; }
        fi
        [ -f "$CHECK_CAPTURE" ] || { echo "ERROR: no encuentro '$CHECK_CAPTURE' (exporta TOOLS_DIR)."; err=1; }
        # natambio se arranca antes de medir: comprueba binario y configuración.
        command -v "$NATAMBIO_BIN" >/dev/null 2>&1 || { echo "ERROR: no encuentro el ejecutable natambio ('$NATAMBIO_BIN')."; err=1; }
        [ -f "$NATAMBIO_CONFIG" ] || { echo "ERROR: no existe la configuración de natambio '$NATAMBIO_CONFIG'."; err=1; }
    fi
    if [ "$DO_IMPULSES" = "1" ]; then
        # La inversa también se genera en la Fase 0; igual criterio.
        if [ "$DO_SWEEP" != "1" ]; then
            [ -f "$INVERSE" ] || { echo "ERROR: no existe el sweep inverso '$INVERSE'."; err=1; }
        fi
        [ -f "$FFT_CONVOLVE" ] || { echo "ERROR: no encuentro '$FFT_CONVOLVE' (exporta TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_PCA" = "1" ]; then
        [ -f "$PCA4DRC" ] || { echo "ERROR: no encuentro '$PCA4DRC' (exporta TOOLS_DIR)."; err=1; }
        [ -f "$WAV2RAW" ] || { echo "ERROR: no encuentro '$WAV2RAW' (exporta TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_DRC" = "1" ]; then
        command -v "$DRC_BIN" >/dev/null 2>&1 || { echo "ERROR: no encuentro el binario DRC ('$DRC_BIN')."; err=1; }
        [ -f "$DRC_CONFIG" ] || { echo "ERROR: no existe la configuración DRC '$DRC_CONFIG'."; err=1; }
        [ -f "$RAW2WAV" ] || { echo "ERROR: no encuentro '$RAW2WAV' (exporta TOOLS_DIR)."; err=1; }
    fi
    if [ "$DO_SWEEP" = "1" ] || [ "$DO_MEASURE" = "1" ] || [ "$DO_IMPULSES" = "1" ] || [ "$DO_PCA" = "1" ] || [ "$DO_DRC" = "1" ]; then
        command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 no está instalado."; err=1; }
    fi
    [ "$err" = "0" ] || { echo "Abortando por comprobaciones previas fallidas."; exit 1; }
    echo "Comprobaciones previas OK."
}

# --- Análisis de una captura: avisa de clipping / nivel bajo / SNR baja -------
check_capture() {
    # Delega en check_capture.py. Devuelve 0 si la captura es válida y !=0 si los
    # niveles no son correctos (clipping / nivel bajo / SNR baja) y hay que
    # repetir la medida. Se invoca dentro de un 'if', así que no rompe 'set -e'.
    python3 "$CHECK_CAPTURE" "$1" "$2" \
        --min-level "$MIN_LEVEL" --min-snr "$MIN_SNR"
}

# --- XML de parámetros para sweepgen.py ---------------------------------------
write_sweep_xml() {
    # Escribe en "$1" el XML <generate_sweep> con los parámetros del bloque de
    # generación del sweep (SWEEP_*) y los nombres de fichero SWEEP / INVERSE.
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

# --- Arranque / parada de natambio (cliente JACK a medir) ---------------------
NATAMBIO_PID=""
start_natambio() {
    # Arranca natambio en segundo plano con NATAMBIO_CONFIG, derivando su
    # stdout/stderr a NATAMBIO_LOG para no ensuciar la salida del script. Espera
    # a que registre sus puertos JACK; aborta si muere o no aparece a tiempo.
    echo "Arrancando natambio con '$(basename "$NATAMBIO_CONFIG")' (log: $NATAMBIO_LOG)..."
    "$NATAMBIO_BIN" -quiet "$NATAMBIO_CONFIG" >"$NATAMBIO_LOG" 2>&1 &
    NATAMBIO_PID=$!
    # Asegura la parada de natambio pase lo que pase a partir de aquí.
    trap stop_natambio EXIT

    local tries=0
    while [ "$tries" -lt 50 ]; do          # hasta ~5 s (50 x 0.1 s)
        if ! kill -0 "$NATAMBIO_PID" 2>/dev/null; then
            echo "ERROR: natambio terminó inesperadamente. Últimas líneas del log:"
            tail -n 20 "$NATAMBIO_LOG" | sed 's/^/    /'
            NATAMBIO_PID=""
            exit 1
        fi
        if jack_lsp 2>/dev/null | grep -q '^natambio:'; then
            echo "natambio en marcha (PID $NATAMBIO_PID); puertos JACK registrados."
            return 0
        fi
        sleep 0.1
        tries=$((tries + 1))
    done
    echo "ERROR: natambio no registró sus puertos JACK a tiempo. Últimas líneas del log:"
    tail -n 20 "$NATAMBIO_LOG" | sed 's/^/    /'
    stop_natambio
    exit 1
}

stop_natambio() {
    # Detiene natambio con SIGINT (su forma normal de parada). Si no termina en
    # un par de segundos, escala a SIGTERM y, en último extremo, SIGKILL, para no
    # dejar el script colgado en la salida.
    if [ -n "$NATAMBIO_PID" ] && kill -0 "$NATAMBIO_PID" 2>/dev/null; then
        echo "Deteniendo natambio (PID $NATAMBIO_PID)..."
        local sig
        for sig in INT TERM KILL; do
            kill "-$sig" "$NATAMBIO_PID" 2>/dev/null || true
            local tries=0
            while [ "$tries" -lt 20 ]; do      # hasta ~2 s por señal
                kill -0 "$NATAMBIO_PID" 2>/dev/null || break 2
                sleep 0.1
                tries=$((tries + 1))
            done
        done
        wait "$NATAMBIO_PID" 2>/dev/null || true
    fi
    NATAMBIO_PID=""
}

# --- Informe de configuración de natambio + confirmación del usuario ----------
report_natambio_routing() {
    # Tras arrancar natambio, muestra el modo (full/half), la variante
    # (subwoofer) y a qué salidas de la tarjeta (system:) están conectadas
    # realmente las salidas de natambio, consultando las conexiones JACK vivas.
    # Pide confirmación al usuario antes de medir.
    echo
    echo "================= Configuración de medición ================="
    echo "  Modo natambio  : $NAT_PREFIX   (FULL_NATAMBIO=$FULL_NATAMBIO)"
    echo "  Modo subwoofer : $SUBWOOFER"
    echo
    echo "  Vías a medir (ecasound -> entrada de natambio):"
    local w
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        printf "    %-12s -> %s\n" "${LABELS[$w]}" "${OUT_PORTS[$w]}"
    done
    echo
    echo "  Salidas de natambio conectadas a la tarjeta (system:):"
    if command -v jack_lsp >/dev/null 2>&1; then
        local nat_out=()
        mapfile -t nat_out < <(jack_lsp -o 2>/dev/null | grep '^natambio:')
        if [ "${#nat_out[@]}" -eq 0 ]; then
            echo "    (no se encontraron puertos de salida de natambio)"
        fi
        local port conn found
        for port in "${nat_out[@]}"; do
            found=0
            # jack_lsp -c <puerto> lista el puerto y, indentadas, sus conexiones.
            while IFS= read -r conn; do
                [ -z "$conn" ] && continue
                printf "    %-28s -> %s\n" "$port" "$conn"
                found=1
            done < <(jack_lsp -c "$port" 2>/dev/null | grep -E '^[[:space:]]' | sed 's/^[[:space:]]*//')
            [ "$found" -eq 0 ] && printf "    %-28s -> (sin conexión)\n" "$port"
        done
    else
        echo "    (jack_lsp no disponible; no se puede consultar el enrutado)"
    fi
    echo "============================================================="
    echo "Por favor, confirme que la asignación es correcta."
    pause "Pulsa Enter para confirmar y comenzar la medición..."
}

# --- Selección interactiva del puerto JACK de destino por vía -----------------
# NOTA: de momento no se usa (natambio fija OUT_PORTS con sus propias etiquetas),
# pero se mantiene por si más adelante hace falta reasignar puertos a mano.
select_jack_ports() {
    # Consulta los puertos de entrada JACK disponibles (destinos del sweep: las
    # salidas de la tarjeta system:playback_* y las entradas de otras
    # aplicaciones, p.ej. natambio:*_input_*) y permite asignar uno a cada vía
    # mediante un menú numérico, sobrescribiendo OUT_PORTS[w]. Sólo en modo
    # interactivo (sin AUTO) y si jack_lsp está disponible.
    if [ "$AUTO" = "1" ]; then
        return 0
    fi
    if ! command -v jack_lsp >/dev/null 2>&1; then
        echo "AVISO: jack_lsp no disponible; se usan los puertos por defecto."
        return 0
    fi

    # Puertos de entrada JACK (destinos). Se excluyen los de ecasound por si ya
    # estuviera corriendo, para no autoconectar.
    local ports=()
    mapfile -t ports < <(jack_lsp -i 2>/dev/null | grep -v -i '^ecasound:')
    if [ "${#ports[@]}" -eq 0 ]; then
        echo "AVISO: no hay puertos de entrada JACK disponibles; se usan los por defecto."
        return 0
    fi

    echo
    echo "Puertos JACK de entrada disponibles (destino del sweep):"
    local idx
    for idx in "${!ports[@]}"; do
        printf "  %2d) %s\n" "$((idx + 1))" "${ports[$idx]}"
    done
    echo "   0) mantener el valor actual"
    echo "Asigna el puerto de salida de ecasound para cada vía de medida:"

    local w sel
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        while true; do
            read -r -p "  Vía '${LABELS[$w]}' [actual: ${OUT_PORTS[$w]}] -> nº de puerto (0=mantener): " sel
            if [ -z "$sel" ] || [ "$sel" = "0" ]; then
                echo "    se mantiene: ${OUT_PORTS[$w]}"
                break
            fi
            if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#ports[@]}" ]; then
                OUT_PORTS[$w]="${ports[$((sel - 1))]}"
                echo "    ${LABELS[$w]} -> ${OUT_PORTS[$w]}"
                break
            fi
            echo "    Entrada no válida. Introduce un número entre 0 y ${#ports[@]}."
        done
    done
    echo
}

preflight

# --- Fase 0: generación del sweep y su inversa --------------------------------
if [ "$DO_SWEEP" = "1" ]; then
    echo "### Fase 0: generación del sweep e inversa (sweepgen.py)"
    # sweepgen.py se alimenta de un XML <generate_sweep>; se construye al vuelo
    # con write_sweep_xml y se sobrescriben los nombres de salida con -s / -i
    # para que coincidan con SWEEP / INVERSE.
    SWEEP_XML="$(mktemp)"
    trap 'rm -f "$SWEEP_XML"' EXIT
    write_sweep_xml "$SWEEP_XML"
    python3 "$SWEEPGEN" "$SWEEP_XML" -s "$SWEEP" -i "$INVERSE"
    rm -f "$SWEEP_XML"
    trap - EXIT
fi

# --- Fase 1: medición ---------------------------------------------------------
if [ "$DO_MEASURE" = "1" ]; then
    echo "### Fase 1: medición de sweeps ($NUM_POS posiciones x $NUM_WAYS vías)"
    # Arranca natambio con la configuración correspondiente; expone los puertos
    # de entrada (OUT_PORTS) a los que ecasound enviará el sweep.
    start_natambio
    # Informe de modo/subwoofer y enrutado real de salidas de natambio a la
    # tarjeta, con confirmación del usuario antes de medir.
    report_natambio_routing

    for w in $(seq 0 $((NUM_WAYS - 1))); do
        mkdir -p "${MEAS_DIRS[$w]}"
    done

    for i in $(seq 1 "$NUM_POS"); do
        printf -v n "%02d" "$i"
        echo "--- Posición de micrófono $n/$NUM_POS ---"
        for w in $(seq 0 $((NUM_WAYS - 1))); do
            echo "Medida ${LABELS[$w]} (posición $n)"
            # Repite la medida hasta que los niveles sean correctos. Si los
            # niveles no son válidos, avisa y espera a que el usuario reajuste la
            # ganancia de su previo de micrófono antes de volver a medir.
            while true; do
                pause "Pulsa Enter para medir ${LABELS[$w]}..."
                ecasound -t:"$REC_SECONDS" \
                    -a:1 -i "$SWEEP" -a:1 -o:jack_auto,"${OUT_PORTS[$w]}" -a:1 -eadb:"$GAIN_OUT" \
                    -a:2 -i:jack_auto,"$IN_MEAS" -a:2 -f:f32_le,1,48000 \
                    -o:"${MEAS_DIRS[$w]}/${SWEEP_PRE[$w]}_$i.wav" -a:2 -eadb:"$GAIN_IN" -ev
                if check_capture "${MEAS_DIRS[$w]}/${SWEEP_PRE[$w]}_$i.wav" "${LABELS[$w]}"; then
                    break
                fi
                echo "*** La grabación de '${LABELS[$w]}' (posición $n) NO es correcta por niveles: hay que repetirla. ***"
                echo "    Reajusta la ganancia en el previo de micrófono antes de continuar."
                if [ "$AUTO" = "1" ]; then
                    echo "    (AUTO=1: no se puede repetir sin intervención; se continúa con esta captura.)"
                    break
                fi
                pause "Pulsa Enter para repetir la medida de ${LABELS[$w]}..."
            done
        done
        echo "Mueve el micrófono de lugar."
    done
    # Medición terminada: ya no hace falta natambio.
    stop_natambio
    trap - EXIT
fi

# --- Fase 2: extracción de impulsos (deconvolución por FFT) -------------------
if [ "$DO_IMPULSES" = "1" ]; then
    echo "### Fase 2: extracción de impulsos (fft_convolve.py)"
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

# --- Fase 3: PCA por vía + conversión a .raw para DRC -------------------------
if [ "$DO_PCA" = "1" ]; then
    echo "### Fase 3: PCA de los impulsos (pca4drc.py) + conversión a .raw"
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        echo "PCA de ${LABELS[$w]} -> ${IMP_DIRS[$w]}/pca4drc/"
        python3 "$PCA4DRC" "${IMP_DIRS[$w]}" "$OUTPUT_LEN" --normalize "$PCA_NORMALIZE"
        # Convierte las componentes WAV a .raw (float 32-bit LE) para DRC. Si la
        # vía tuvo menos de 2 impulsos, pca4drc.py no crea el directorio: se omite.
        pca_dir="${IMP_DIRS[$w]}/pca4drc"
        if compgen -G "$pca_dir/*.wav" >/dev/null; then
            echo "Conversión a .raw (formato DRC) en $pca_dir/"
            python3 "$WAV2RAW" "$pca_dir"/*.wav
        fi
    done
fi

# --- Fase 4: corrección con DRC (drc estándar) -------------------------------
if [ "$DO_DRC" = "1" ]; then
    echo "### Fase 4: corrección con DRC ($DRC_BIN + $(basename "$DRC_CONFIG"))"
    for w in $(seq 0 $((NUM_WAYS - 1))); do
        base="${IMP_DIRS[$w]}"
        pca0="$base/pca4drc/PCA_0.raw"
        if [ ! -f "$pca0" ]; then
            echo "AVISO: no existe '$pca0'; se omite DRC para ${LABELS[$w]}."
            continue
        fi
        echo "DRC de ${LABELS[$w]}: --BCBaseDir=$base/ --BCInFile=pca4drc/PCA_0.raw"
        # BaseDir = carpeta de impulsos de la vía (mismo nivel que el p_left/
        # original). Al ir --BCBaseDir en la línea de comandos, también afecta a
        # --BCInFile (-> $base/pca4drc/PCA_0.raw) y a las salidas/rutas relativas
        # del config (rps.raw/rms.raw -> $base/, ../target/... -> raíz de medida).
        if "$DRC_BIN" --BCBaseDir="$base/" --BCInFile="pca4drc/PCA_0.raw" "$DRC_CONFIG"; then
            # Convierte las salidas rps.raw / rms.raw de DRC a WAV.
            for out in "$DRC_PS_OUT" "$DRC_MS_OUT"; do
                if [ -f "$base/$out" ]; then
                    python3 "$RAW2WAV" "$base/$out" --rate "$SWEEP_RATE"
                else
                    echo "AVISO: DRC no generó '$base/$out'."
                fi
            done
        else
            echo "ERROR: DRC falló para ${LABELS[$w]} (se continúa con las demás vías)."
        fi
    done
fi

echo "Hecho."
