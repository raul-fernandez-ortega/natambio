#! /usr/bin/env python3
#
# Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
#
# Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.

"""
xtc_conf.py — TOML configuration shared by the two Python XTC generators.

Reads the SAME files as the C tools in tools/xtc_filters, through the standard
library's `tomllib` (Python 3.11+) or `tomli` on older interpreters. The C side
parses a strict subset of TOML by hand; since that subset is a subset, a file
written for either tool is read identically by both.

Schema (see tools/xtc_filters/xtc_conf.h for the C counterpart):

    sample_rate = 48000        # top level, common to the whole design
    filter_len  = 4096

    [xtc]                      # or [left] / [right] in the asymmetric tool
    itd_us      = 170
    ild_db      = 14.0
    ild_alpha   = 2.0
    azimuth_deg = 20

    [output]
    directory = "filters"
    prefix    = "my_room"      # optional

The balance b of the asymmetric model is deliberately absent: it is not baked
into the coefficients but applied downstream as a routing gain. See the balance
section of docs/xtc/xtc_no_simetrico_es.md.
"""

import sys

try:                     # Python 3.11+
    import tomllib
except ModuleNotFoundError:          # pragma: no cover - depends on interpreter
    try:
        import tomli as tomllib
    except ModuleNotFoundError:
        sys.stderr.write(
            "xtc: reading TOML needs Python 3.11+ or the 'tomli' package "
            "(pip install tomli)\n")
        raise

SIDE_KEYS = ("itd_us", "ild_db", "ild_alpha", "azimuth_deg")
SIDE_TYPES = {"itd_us": int, "ild_db": float, "ild_alpha": float, "azimuth_deg": int}

TOP_TYPES = {"sample_rate": int, "filter_len": int}
OUTPUT_KEYS = ("directory", "prefix")


class ConfError(Exception):
    """Configuration problem, already phrased for the user."""


def _load(path):
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except OSError as e:
        raise ConfError("cannot open %s: %s" % (path, e.strerror)) from e
    except tomllib.TOMLDecodeError as e:
        raise ConfError("%s: %s" % (path, e)) from e


def _check_keys(doc, allowed_tables):
    """Rejects unknown keys, the way the C reader does.

    Without this a typo such as `ild_alfa` silently leaves ild_alpha at its
    default, and the parameter appears to do nothing.
    """
    for key, value in doc.items():
        if isinstance(value, dict):
            if key not in allowed_tables:
                raise ConfError("unknown table [%s]" % key)
            known = OUTPUT_KEYS if key == "output" else SIDE_KEYS
            for subkey in value:
                if subkey not in known:
                    raise ConfError("unknown key '%s.%s'" % (key, subkey))
        elif key not in TOP_TYPES:
            raise ConfError("unknown key '%s'" % key)


def _number(value, want, name):
    """Converts with the same strictness as the C side.

    An int is accepted where a float is wanted (TOML has no 14 vs 14.0
    distinction worth enforcing here), but a float where an int is wanted is an
    error: `filter_len = 4096.0` would otherwise truncate silently. Booleans are
    rejected outright — in Python `True` is an int, and `filter_len = true`
    would sail through as 1.
    """
    if isinstance(value, bool):
        raise ConfError("%s must be a number, not a boolean" % name)
    if want is int:
        if not isinstance(value, int):
            raise ConfError("%s must be an integer, got %r" % (name, value))
        return value
    if not isinstance(value, (int, float)):
        raise ConfError("%s must be a number, got %r" % (name, value))
    return float(value)


def _load_side(doc, table, side, required):
    """Overlays doc[table] onto the dict `side`."""
    block = doc.get(table)
    if block is None:
        if required:
            raise ConfError("no [%s] table: an asymmetric design needs both sides"
                            % table)
        return side
    for key in SIDE_KEYS:
        if key in block:
            side[key] = _number(block[key], SIDE_TYPES[key], "%s.%s" % (table, key))
        elif required:
            raise ConfError("[%s] is missing the key '%s'" % (table, key))
    return side


def _load_output(doc, cfg):
    block = doc.get("output")
    if block is None:
        return
    for key in OUTPUT_KEYS:
        if key not in block:
            continue
        value = block[key]
        if not isinstance(value, str):
            raise ConfError("output.%s must be a quoted string" % key)
        if key == "directory" and not value:
            raise ConfError("output.directory must not be empty")
        if key == "prefix" and "/" in value:
            raise ConfError("output.prefix must not contain '/'; "
                            "use output.directory for the path")
        cfg[key] = value


def _load_top(doc, cfg):
    for key, want in TOP_TYPES.items():
        if key in doc:
            cfg[key] = _number(doc[key], want, key)


def load_sym(path, cfg):
    """Overlays `path` onto cfg (a dict), leaving absent keys untouched.

    Every key is optional here, so a stored configuration can be combined with
    command-line flags exactly as in the C tool. Returns cfg.
    """
    doc = _load(path)
    _check_keys(doc, allowed_tables=("xtc", "output"))
    _load_top(doc, cfg)
    _load_side(doc, "xtc", cfg["xtc"], required=False)
    _load_output(doc, cfg)
    return cfg


def load_asym(path, cfg):
    """Reads `path` into cfg (a dict). Returns cfg.

    Both [left] and [right] must be present with all four keys: there is no
    meaningful default for one side of an asymmetric layout, and quietly
    supplying one would produce a plausible filter for a geometry the user never
    described.
    """
    doc = _load(path)
    _check_keys(doc, allowed_tables=("left", "right", "output"))
    _load_top(doc, cfg)
    _load_side(doc, "left", cfg["left"], required=True)
    _load_side(doc, "right", cfg["right"], required=True)
    _load_output(doc, cfg)
    return cfg
