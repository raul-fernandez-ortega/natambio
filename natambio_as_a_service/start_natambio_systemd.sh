#!/bin/bash
#
# natambio launcher for systemd (natambio.service unit).
#
# Runs natambio in the FOREGROUND (exec, no nohup/&) with the "complete"
# config (1 process). Output goes to the journal.
#
# For split mode (2 processes: natambio_only + eq) see the natambio@.service
# template in the unit's comments.

USB_CARD_ID=USB
USB_EXPECTED="Scarlett 6i6 USB"
AUDIOFIRE4_GUID1=0x001486069aba050d
AUDIOFIRE4_GUID2=0x0014860f9628513b
FA66_GUID=0x0040ab0000c36f49
FA101_GUID=0x0040ab0000c22497

VERSION="07"
TAG=""

if grep -q "\[$USB_CARD_ID *\].*$USB_EXPECTED" /proc/asound/cards 2>/dev/null; then
    TAG=usb_v03
else
    # Count how many AudioFire4 units are on the bus: two → "dual" config,
    # one (either GUID) → single-card config.
    af4_count=0
    fa66_found=0
    fa101_found=0
    for g in /sys/bus/firewire/devices/*/guid; do
        [ -r "$g" ] || continue
        guid=`cat "$g" 2>/dev/null`
        if [ "$guid" = "$AUDIOFIRE4_GUID1" ] || [ "$guid" = "$AUDIOFIRE4_GUID2" ]; then
            af4_count=$((af4_count + 1))
        elif [ "$guid" = "$FA66_GUID" ]; then
            fa66_found=1
        elif [ "$guid" = "$FA101_GUID" ]; then
            fa101_found=1
        fi
    done
    if [ "$af4_count" -ge 2 ]; then
        TAG=dual_audiofire4_v$VERSION
    elif [ "$af4_count" -eq 1 ]; then
        TAG=audiofire4_v$VERSION
    elif [ "$fa101_found" -eq 1 ]; then
        TAG=edirol_fa101_v$VERSION
    elif [ "$fa66_found" -eq 1 ]; then
        TAG=edirol_fa66_v$VERSION
    fi
fi

if [ -z "$TAG" ]; then
    echo "Error: no se ha encontrado Scarlett 6i6 USB, AudioFire4, Edirol FA101 ni Edirol FA66. natambio no arranca." >&2
    exit 1
fi

CONFIGDIR=$HOME/nat_ambio_surround_config
COMPLETENATAMBIOCONFIG=test_complete_natambio_$TAG.xml

echo "starting natambio ($COMPLETENATAMBIOCONFIG)..."
exec /usr/local/bin/natambio $CONFIGDIR/$COMPLETENATAMBIOCONFIG
