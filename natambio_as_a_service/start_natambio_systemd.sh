#!/bin/bash
#
# natambio launcher for systemd (natambio.service unit).
#
# Runs natambio in the FOREGROUND (exec, no nohup/&) with the "complete"
# config (1 process). Output goes to the journal.
#
# For split mode (2 processes: natambio_only + eq) see the natambio@.service
# template in the unit's comments.

AUDIOFIRE4_GUID1=0x001486069aba050d
AUDIOFIRE4_GUID2=0x0014860f9628513b
FA66_GUID=0x0040ab0000c36f49
FA101_GUID=0x0040ab0000c22497
MOTU_GUID=0x0001f200000841eb

# VERSION fixes the suffix of the per-card files (io_<TAG>.xml and the legacy
# test_complete_natambio_<TAG>.xml configs). COMMON_VERSION versions the common
# block separately: they are decoupled because the common block evolves (e.g.
# v08 = asymmetric XTC with <xtc_asym>) without forcing a duplication of every
# card's I/O.
VERSION="07"
COMMON_VERSION="08"
TAG=""

# Supported USB ALSA cards: "pattern-in-/proc/asound/cards::config-TAG". The
# first one present fixes the TAG (each card has its own config because they
# differ in the number of outputs): Scarlett 6i6 -> focusrite_scarlett_vNN,
# Behringer UMC204HD -> behringer_umc204_vNN.
USB_CARDS=(
    "Scarlett 6i6::focusrite_scarlett_v$VERSION"
    "UMC204HD::behringer_umc204_v$VERSION"
)
for entry in "${USB_CARDS[@]}"; do
    pat=${entry%%::*}
    if grep -q "$pat" /proc/asound/cards 2>/dev/null; then
        TAG=${entry##*::}
        break
    fi
done

if [ -z "$TAG" ]; then
    # Count how many AudioFire4 units are on the bus: two -> "dual" config,
    # one (either GUID) -> single-card config.
    af4_count=0
    fa66_found=0
    fa101_found=0
    motu_found=0
    for g in /sys/bus/firewire/devices/*/guid; do
        [ -r "$g" ] || continue
        guid=`cat "$g" 2>/dev/null`
        if [ "$guid" = "$AUDIOFIRE4_GUID1" ] || [ "$guid" = "$AUDIOFIRE4_GUID2" ]; then
            af4_count=$((af4_count + 1))
        elif [ "$guid" = "$FA66_GUID" ]; then
            fa66_found=1
        elif [ "$guid" = "$FA101_GUID" ]; then
            fa101_found=1
        elif [ "$guid" = "$MOTU_GUID" ]; then
            motu_found=1
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
    elif [ "$motu_found" -eq 1 ]; then
        TAG=motu_ultralite_v$VERSION
    fi
fi

if [ -z "$TAG" ]; then
    echo "Error: none of Scarlett 6i6 USB / Behringer UMC204HD / AudioFire4 / Edirol FA101 / Edirol FA66 / MOTU UltraLite was found. natambio does not start." >&2
    exit 1
fi

CONFIGDIR=$HOME/natambio_config

# Common model + I/O: the final config of each card is the AGGREGATION of the
# common block (the processing shared by all of them: loudness/xtc/coeffs/NAE/
# convol) plus the card's own I/O. That way, editing a non-I/O parameter in the
# common block applies to ALL cards. The assembly is:
#     natambio_common_v$COMMON_VERSION.xml  +  io_<TAG>.xml  +  </natambio></main>
# (the I/O-vs-rest order is irrelevant to natambio: it parses the whole DOM and
#  groups by tag; the port binding happens at runtime).
COMMON=$CONFIGDIR/natambio_common_v$COMMON_VERSION.xml
IOFILE=$CONFIGDIR/io_$TAG.xml
LEGACY=$CONFIGDIR/test_complete_natambio_$TAG.xml

if [ -f "$IOFILE" ]; then
    if [ ! -f "$COMMON" ]; then
        echo "Error: the common block $COMMON is missing" >&2
        exit 1
    fi
    ASSEMBLED=$CONFIGDIR/natambio_assembled_$TAG.xml
    # AUTO-GENERATED on every startup from COMMON + IOFILE; do not edit by hand.
    {
        cat "$COMMON"
        cat "$IOFILE"
        printf '  </natambio>\n</main>\n'
    } > "$ASSEMBLED" || { echo "Error: could not assemble $ASSEMBLED" >&2; exit 1; }
    echo "starting natambio ($(basename "$COMMON") + io_$TAG -> $(basename "$ASSEMBLED"))..."
    exec /usr/local/bin/natambio "$ASSEMBLED"
elif [ -f "$LEGACY" ]; then
    # There is no io_$TAG.xml for this card yet: the hand-tuned complete config
    # (old model) is used. To move it to the common model, create $IOFILE.
    echo "starting natambio (legacy $(basename "$LEGACY"); no io_$TAG.xml)..."
    exec /usr/local/bin/natambio "$LEGACY"
else
    echo "Error: there is no config for card '$TAG'." >&2
    echo "  Create its specific I/O:            $IOFILE" >&2
    echo "  (or a complete legacy config:       $LEGACY)" >&2
    exit 1
fi
