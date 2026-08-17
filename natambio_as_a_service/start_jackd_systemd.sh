#!/bin/bash
#
# jackd launcher for systemd (natambio-jackd.service unit).
#
# Key difference from start_jackd_v02.sh: jackd runs in the FOREGROUND
# (exec, no trailing '&' or 'sleep') so systemd supervises the real process
# and can restart it. Output goes to the journal (no file redirection).
#
# Card support:
#   USB      — the supported cards are listed in USB_CARDS, matched by name against
#              /proc/asound/cards. The ALSA card id is read from that same line
#              rather than fixed here, so the script picks the right interface when
#              more than one USB audio device is present.
#   FireWire — each supported card is identified by its GUID. The GUIDs are listed
#              as constants below; the script matches them at runtime against the
#              devices enumerated on the FireWire bus (via FFADO).

SAMPLERATE=48000
BUFFERSIZE=256
SCRIPTDIR=$HOME/control_scripts

# Supported USB (ALSA, class-compliant) cards, in priority order. Each entry is
# "human name::egrep-pattern". The pattern is looked up in the header line of
# /proc/asound/cards and the real ALSA card id (the text in brackets) is taken
# from there, because it is not fixed: the Scarlett 6i6 shows up as [USB] and the
# Behringer UMC204HD as [U192k]. This way it starts with whichever one is plugged
# in, without pinning the id by hand.
USB_CARDS=(
    "Focusrite Scarlett 6i6::Scarlett 6i6"
    "Behringer UMC204HD::UMC204HD"
)

# Prints the ALSA card id (the text in brackets) of the first card whose header
# line in /proc/asound/cards matches the egrep pattern $1.
alsa_card_id_by_desc() {
    awk -v pat="$1" '/^[[:space:]]*[0-9]+[[:space:]]*\[/ && $0 ~ pat {
        s=$0; sub(/^[^[]*\[/,"",s); sub(/[[:space:]]*\].*$/,"",s); print s; exit
    }' /proc/asound/cards 2>/dev/null
}

AUDIOFIRE4_GUID1=0x001486069aba050d
AUDIOFIRE4_GUID2=0x0014860f9628513b
AUDIOFIRE8_GUID=0x00148603d7f40d23
FA66_GUID=0x0040ab0000c36f49
FA101_GUID=0x0040ab0000c22497
MOTU_GUID=0x0001f200000841eb

export LADSPA_PATH=$HOME/.ladspa

# Kills any ffado-dbus-server that was already running (typically one
# activated by D-Bus in a previous session). If it survives a bus reset —
# for example one triggered by a hardware change or a jackd restart — it
# keeps stale handles and starts serving zero values; it also holds the
# bus name org.ffado.Control, preventing the server started by this script
# from claiming it. We kill it to force a clean start BEFORE jackd. We use
# 'pkill -f' because the process name is truncated to 15 characters
# (ffado-dbus-serv) so 'pkill -x ffado-dbus-server' would not match.
kill_stale_ffado_dbus_server() {
    pgrep -f ffado-dbus-server >/dev/null 2>&1 || return 0
    echo "Reiniciando ffado-dbus-server: matando instancia(s) previa(s)..."
    pkill -f ffado-dbus-server 2>/dev/null
    for _ in $(seq 1 15); do
        pgrep -f ffado-dbus-server >/dev/null 2>&1 || return 0
        sleep 0.2
    done
    echo "  no termina; forzando con SIGKILL..."
    pkill -9 -f ffado-dbus-server 2>/dev/null
    sleep 0.3
}

# Priority 1: a supported USB ALSA card (Focusrite Scarlett 6i6 / Behringer
# UMC204HD). Starts with the first one present, using its real ALSA id.
for entry in "${USB_CARDS[@]}"; do
    name=${entry%%::*}
    pat=${entry##*::}
    cid=$(alsa_card_id_by_desc "$pat")
    if [ -n "$cid" ]; then
        echo "Booting with $name (ALSA hw:$cid)"
        exec /usr/bin/jackd -R -P70 -dalsa -dhw:$cid -r$SAMPLERATE -p$BUFFERSIZE -n3
    fi
done

echo "Ninguna tarjeta USB soportada. Probando firewire..."

# Detect ALL supported cards present on the bus.
#   AF4_GUIDS : list of AudioFire4 units (1 or 2; chained and synchronised via SPDIF)
#   FW_DEVICE/FW_GUID : first AF8 / FA66 / FA101 / MOTU found, if applicable
#                       (single device)
AF4_GUIDS=()
FW_DEVICE=""
FW_GUID=""
for g in /sys/bus/firewire/devices/*/guid; do
    [ -r "$g" ] || continue
    guid=`cat "$g" 2>/dev/null`
    case "$guid" in
        $AUDIOFIRE4_GUID1|$AUDIOFIRE4_GUID2) AF4_GUIDS+=("${guid#0x}") ;;
        $AUDIOFIRE8_GUID)                    FW_DEVICE=af8;   FW_GUID=${guid#0x} ;;
        $FA66_GUID)                          FW_DEVICE=fa66;  FW_GUID=${guid#0x} ;;
        $FA101_GUID)                         FW_DEVICE=fa101; FW_GUID=${guid#0x} ;;
        $MOTU_GUID)                          FW_DEVICE=motu;  FW_GUID=${guid#0x} ;;
    esac
done

# --- AudioFire4 (one or two, slaved to external SPDIF) -----------------------
if [ ${#AF4_GUIDS[@]} -gt 0 ]; then
    echo "AudioFire4 detectadas: ${#AF4_GUIDS[@]} -> ${AF4_GUIDS[*]}"
    kill_stale_ffado_dbus_server
    echo "Firewire bus reset..."
    ffado-test BusReset
    sleep 1
    echo "Starting ffado-dbus-server..."
    ffado-dbus-server &
    FFADO_PID=$!
    sleep 1
    # Configure each card (Audiofire4_sync.sh sets clock=SPDIF int32:1).
    for guid in "${AF4_GUIDS[@]}"; do
        echo "Configurando AudioFire4 GUID $guid (clock=SPDIF)"
        bash $SCRIPTDIR/Audiofire4_sync.sh $guid
        sleep 0.2
    done
    kill -9 "$FFADO_PID" 2>/dev/null
    sleep 1
    # No -d flag: the FFADO backend aggregates ALL synchronised AF4 units on the bus.
    exec /usr/bin/jackd -R -P70 -dfirewire -r$SAMPLERATE -p$BUFFERSIZE -n3
fi

# --- Fall-through: AF8 / FA66 / FA101 (single device) ------------------------
# FireWire device present but unknown GUID -> assume Edirol FA66
if [ -z "$FW_DEVICE" ]; then
    for g in /sys/bus/firewire/devices/*/guid; do
        [ -r "$g" ] || continue
        FW_DEVICE=fa66
        break
    done
fi

if [ -z "$FW_DEVICE" ]; then
    echo "Error: no hay tarjeta soportada (Scarlett 6i6 USB / Behringer UMC204HD / AudioFire4 / AudioFire8 / Edirol FA66 / Edirol FA101 / MOTU UltraLite). jackd no arranca." >&2
    exit 1
fi

case "$FW_DEVICE" in
    af8)
        kill_stale_ffado_dbus_server
        echo "Firewire bus reset..."
        ffado-test BusReset
        sleep 1
        echo "Starting ffado-dbus-server..."
        ffado-dbus-server &
        FFADO_PID=$!
        sleep 1
        echo "AudioFire8 (GUID $FW_GUID)"
        bash $SCRIPTDIR/Audiofire8_sync.sh $FW_GUID
        kill -9 "$FFADO_PID" 2>/dev/null
        sleep 1
        ;;
    fa66)
        echo "Edirol FA66 (GUID $FW_GUID)"
        ;;
    motu)
        # MOTU UltraLite (FFADO): configured with dbus commands BEFORE jackd, the
        # same way as the AudioFire units. We kill any leftover ffado-dbus-server,
        # do a bus reset, bring up a temporary ffado-dbus-server, run the sync
        # script with the GUID and kill it again so that jackd takes over a clean
        # FireWire backend.
        kill_stale_ffado_dbus_server
        echo "Firewire bus reset..."
        ffado-test BusReset
        sleep 1
        echo "Starting ffado-dbus-server..."
        ffado-dbus-server &
        FFADO_PID=$!
        sleep 1
        echo "MOTU UltraLite (GUID $FW_GUID)"
        bash $SCRIPTDIR/MOTU_Ultralite_sync.sh $FW_GUID
        kill -9 "$FFADO_PID" 2>/dev/null
        sleep 1
        ;;
    fa101)
        # Edirol FA-101 (BeBoB): clock is "Device Controlled" — the source
        # (internal / external SPDIF) is chosen via HARDWARE on the device; FFADO
        # does not change it. No clock sync script to run; SPDIF is set on the
        # physical selector on the unit.
        #
        # A FireWire bus reset before jackd is beneficial: if the card lost its
        # SPDIF lock or was left in a bad state, the reset re-enumerates it and
        # brings it back (equivalent to the manual 'ffado-test BusReset' that
        # reactivates the card). We first kill any resident ffado-dbus-server:
        # if it survives the reset it keeps stale handles. After the reset we
        # give the card time to re-lock its clock before jackd claims it.
        kill_stale_ffado_dbus_server
        echo "Edirol FA101 (GUID $FW_GUID) -- clock por hardware; firewire bus reset..."
        ffado-test BusReset
        sleep 2
        ;;
esac

exec /usr/bin/jackd -R -P70 -dfirewire -r$SAMPLERATE -p$BUFFERSIZE -n3
