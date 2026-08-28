/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "xtc_conf.h"
#include "toml_min.h"

#include <stdio.h>
#include <string.h>

/* Copies a string value into a fixed buffer, refusing to truncate silently. */
static int copy_str(char *dst, size_t dstlen, const char *src, const char *what,
                    char *err, size_t errlen)
{
    size_t n = strlen(src);
    if (n >= dstlen) {
        snprintf(err, errlen, "%s is too long (%zu characters, maximum %zu)",
                 what, n, dstlen - 1);
        return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

/* Reads the four keys of one side. `required` makes every key mandatory;
 * otherwise absent keys leave *side untouched. Returns 0 on success. */
static int load_side(const toml_min *t, const char *table, xtc_conf_side *side,
                     int required, char *err, size_t errlen)
{
    struct { const char *key; int is_int; void *dst; } fields[] = {
        { "itd_us",      1, &side->itd_us      },
        { "ild_db",      0, &side->ild_db      },
        { "ild_alpha",   0, &side->ild_alpha   },
        { "azimuth_deg", 1, &side->azimuth_deg },
    };

    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        int rc = fields[i].is_int
               ? toml_min_int(t, table, fields[i].key, (int *)fields[i].dst, err, errlen)
               : toml_min_double(t, table, fields[i].key, (double *)fields[i].dst, err, errlen);
        if (rc < 0) return -1;
        if (rc == 0 && required) {
            snprintf(err, errlen, "[%s] is missing the key '%s'", table, fields[i].key);
            return -1;
        }
    }
    return 0;
}

/* Reads [output]; both keys optional. */
static int load_output(const toml_min *t, char *directory, char *prefix,
                       char *err, size_t errlen)
{
    const char *s = NULL;

    int rc = toml_min_string(t, "output", "directory", &s, err, errlen);
    if (rc < 0) return -1;
    if (rc > 0) {
        if (*s == '\0') {
            snprintf(err, errlen, "output.directory must not be empty");
            return -1;
        }
        if (copy_str(directory, XTC_CONF_PATHMAX, s, "output.directory", err, errlen) != 0)
            return -1;
    }

    rc = toml_min_string(t, "output", "prefix", &s, err, errlen);
    if (rc < 0) return -1;
    if (rc > 0) {
        /* A prefix containing a path separator would silently scatter the
         * filters outside output.directory. */
        if (strchr(s, '/') != NULL) {
            snprintf(err, errlen, "output.prefix must not contain '/'; "
                                  "use output.directory for the path");
            return -1;
        }
        if (copy_str(prefix, XTC_CONF_PATHMAX, s, "output.prefix", err, errlen) != 0)
            return -1;
    }
    return 0;
}

/* Reads the two top-level design switches. Both optional: an absent key leaves
 * whatever the caller pre-loaded. Shared by the two loaders because the pair
 * describes the design, not the layout. */
static int load_design(const toml_min *t, int *frac_delay, int *model_delay,
                       char *err, size_t errlen)
{
    if (toml_min_bool(t, "", "frac_delay", frac_delay, err, errlen) < 0) return -1;
    if (toml_min_int(t, "", "model_delay", model_delay, err, errlen) < 0) return -1;
    if (*model_delay < 0) {
        snprintf(err, errlen, "model_delay must not be negative (got %d)", *model_delay);
        return -1;
    }
    return 0;
}

int xtc_conf_load_sym(const char *path, xtc_conf_sym *cfg, char *err, size_t errlen)
{
    static const char *const allowed[] = {
        "sample_rate", "filter_len", "frac_delay", "model_delay",
        "xtc.itd_us", "xtc.ild_db", "xtc.ild_alpha", "xtc.azimuth_deg",
        "output.directory", "output.prefix",
        NULL
    };

    toml_min *t = toml_min_load(path, err, errlen);
    if (!t) return -1;

    if (toml_min_check_keys(t, allowed, err, errlen) != 0) goto fail;

    if (toml_min_int(t, "", "sample_rate", &cfg->sample_rate, err, errlen) < 0) goto fail;
    if (toml_min_int(t, "", "filter_len",  &cfg->filter_len,  err, errlen) < 0) goto fail;

    if (load_design(t, &cfg->frac_delay, &cfg->model_delay, err, errlen) != 0) goto fail;

    if (load_side(t, "xtc", &cfg->xtc, 0, err, errlen) != 0) goto fail;
    if (load_output(t, cfg->directory, cfg->prefix, err, errlen) != 0) goto fail;

    toml_min_free(t);
    return 0;

fail:
    toml_min_free(t);
    return -1;
}

int xtc_conf_load_asym(const char *path, xtc_conf_asym *cfg, char *err, size_t errlen)
{
    static const char *const allowed[] = {
        "sample_rate", "filter_len", "frac_delay", "model_delay",
        "left.itd_us",  "left.ild_db",  "left.ild_alpha",  "left.azimuth_deg",
        "right.itd_us", "right.ild_db", "right.ild_alpha", "right.azimuth_deg",
        "output.directory", "output.prefix",
        NULL
    };

    toml_min *t = toml_min_load(path, err, errlen);
    if (!t) return -1;

    if (toml_min_check_keys(t, allowed, err, errlen) != 0) goto fail;

    if (!toml_min_has_table(t, "left")) {
        snprintf(err, errlen, "no [left] table: an asymmetric design needs both sides");
        goto fail;
    }
    if (!toml_min_has_table(t, "right")) {
        snprintf(err, errlen, "no [right] table: an asymmetric design needs both sides");
        goto fail;
    }

    if (toml_min_int(t, "", "sample_rate", &cfg->sample_rate, err, errlen) < 0) goto fail;
    if (toml_min_int(t, "", "filter_len",  &cfg->filter_len,  err, errlen) < 0) goto fail;

    if (load_design(t, &cfg->frac_delay, &cfg->model_delay, err, errlen) != 0) goto fail;

    if (load_side(t, "left",  &cfg->left,  1, err, errlen) != 0) goto fail;
    if (load_side(t, "right", &cfg->right, 1, err, errlen) != 0) goto fail;
    if (load_output(t, cfg->directory, cfg->prefix, err, errlen) != 0) goto fail;

    toml_min_free(t);
    return 0;

fail:
    toml_min_free(t);
    return -1;
}
