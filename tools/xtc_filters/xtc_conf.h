/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef XTC_CONF_H
#define XTC_CONF_H

#include <stddef.h>

/* xtc_conf — TOML schema shared by the two XTC generators.
 *
 * The symmetric and asymmetric layouts use the SAME key names for a side, so
 * that [xtc], [left] and [right] are interchangeable blocks:
 *
 *     sample_rate = 48000        # top level, common to the whole design
 *     filter_len  = 4096
 *     frac_delay  = true         # optional; exact ITD instead of rounded
 *     model_delay = 64           # optional; bulk delay for frac_delay
 *
 *     [xtc]                      # or [left] / [right] in the asymmetric tool
 *     itd_us      = 170
 *     ild_db      = 14.0
 *     ild_alpha   = 2.0
 *     azimuth_deg = 20
 *
 *     [output]
 *     directory = "filters"
 *     prefix    = "my_room"      # optional
 *
 * frac_delay and model_delay sit at the top level rather than inside a side
 * block because they describe the whole design, not one speaker's path -- which
 * is also what keeps [xtc], [left] and [right] interchangeable.
 *
 * The balance b of the asymmetric model is deliberately NOT a key here. It is
 * not baked into the coefficients: it is applied downstream as a routing gain,
 * exactly as the <xtc_asym> block does in natambio. See the balance section of
 * docs/xtc/xtc_no_simetrico_es.md.
 *
 * The Python counterparts read these very same files through the standard
 * `tomllib`, so a file that works with one tool works with the other.
 */

#define XTC_CONF_PATHMAX 512

/* One speaker's acoustic path: exactly one G of the model. Mirrors
 * xtc_asym_side in lib/xtc_asym.h, kept separate so this header does not drag
 * the DSP library into tools that only parse configuration. */
typedef struct {
    int    itd_us;
    double ild_db;
    double ild_alpha;
    int    azimuth_deg;
} xtc_conf_side;

typedef struct {
    int           sample_rate;
    int           filter_len;
    /* Run the XTC recursion at the exact, unrounded ITD (get_xtc_frac /
     * get_xtc_asym_frac) instead of rounding it to whole samples, and the bulk
     * model delay that path needs. Design-wide, hence top level. Defaults are
     * the caller's to set: the tools use 0 and XTC_DEFAULT_MODEL_DELAY. */
    int           frac_delay;
    int           model_delay;
    xtc_conf_side xtc;
    char          directory[XTC_CONF_PATHMAX];
    char          prefix[XTC_CONF_PATHMAX];   /* empty => legacy filename contract */
} xtc_conf_sym;

typedef struct {
    int           sample_rate;
    int           filter_len;
    /* Run the XTC recursion at the exact, unrounded ITD (get_xtc_frac /
     * get_xtc_asym_frac) instead of rounding it to whole samples, and the bulk
     * model delay that path needs. Design-wide, hence top level. Defaults are
     * the caller's to set: the tools use 0 and XTC_DEFAULT_MODEL_DELAY. */
    int           frac_delay;
    int           model_delay;
    xtc_conf_side left, right;
    char          directory[XTC_CONF_PATHMAX];
    char          prefix[XTC_CONF_PATHMAX];
} xtc_conf_asym;

/* xtc_conf_load_sym — overlays `path` onto *cfg.
 *
 * Every key is optional: whatever the caller pre-loaded (defaults, or values
 * already taken from the command line) survives if the file does not mention
 * it. Unknown keys are an error, not a shrug.
 *
 * Returns 0 on success, -1 on error with a message in `err`. */
int xtc_conf_load_sym(const char *path, xtc_conf_sym *cfg, char *err, size_t errlen);

/* xtc_conf_load_asym — reads `path` into *cfg.
 *
 * Unlike the symmetric case, [left] and [right] must both be present and each
 * must define all four of its keys. There is no meaningful default for one side
 * of an asymmetric layout, and silently filling in a missing ild_db would
 * produce a plausible-looking filter for a geometry the user never described.
 *
 * Returns 0 on success, -1 on error with a message in `err`. */
int xtc_conf_load_asym(const char *path, xtc_conf_asym *cfg, char *err, size_t errlen);

#endif
