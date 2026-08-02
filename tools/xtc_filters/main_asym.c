/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

/*
 * natambio-xtc-filters-asym -- offline generator for the asymmetric XTC filter
 * set, the standalone counterpart of the <xtc_asym> block of natambio.
 *
 * Reuses lib/xtc_asym.c (-> dsp.c, binaural_cues.c) unchanged, so the WAV files
 * written here are the same coefficients natambio computes in process.
 *
 * Three filters come out, not four: the two direct filters of the asymmetric
 * model are bit-for-bit identical, because they depend only on the round-trip
 * operator P, which is symmetric under exchange of channels. What differs
 * between channels is the cross filter. See docs/xtc/xtc_no_simetrico_es.md.
 *
 * The balance b is NOT applied here. process_asym() produces M^-1, and the
 * level trim between channels belongs downstream as a routing gain -- in
 * natambio, the <gain> of the two <convol> blocks feeding the same output. A
 * balance that is left unadjusted caps crosstalk cancellation at about
 * 20*log10|1-b| dB, so it is not optional; it is simply not part of the
 * coefficients. The balance section of the technical note gives the listening
 * procedure.
 *
 * Parameters come from a TOML file. There is no flag interface here: eight
 * numbers on a command line, half of them differing from the other half by a
 * single letter, is exactly the shape of mistake that produces a plausible
 * filter for the wrong geometry.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav_out.h"
#include "xtc_conf.h"
#include "xtc_asym.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -c config.toml\n"
        "\n"
        "Generates the three asymmetric XTC filters (one direct, shared by both\n"
        "channels, and one cross filter per channel) from a TOML description of\n"
        "the two sides. See xtc_asym_*.toml for annotated examples.\n"
        "\n"
        "The balance between channels is not baked into these coefficients; it is\n"
        "applied as a routing gain. See the balance section of\n"
        "docs/xtc/xtc_no_simetrico_es.md.\n", prog);
}

/* save_asym_wavs — writes the three filters as <prefix>_{direct,cross_left,
 * cross_right}.wav.
 *
 * Unlike the symmetric tool there is no descriptive default filename encoding
 * the parameters: with eight of them the name would be unreadable, and the TOML
 * file is already the record of the design. Hence a plain default prefix, and a
 * strong suggestion in the examples to set one per system. */
static int save_asym_wavs(const xtc_conf_asym *cfg, const double *direct,
                          const double *cross_l, const double *cross_r) {
    char path[XTC_CONF_PATHMAX * 2];
    const char *prefix = (cfg->prefix[0] != '\0') ? cfg->prefix : "XTC_asym";

    if (make_wav_path(path, sizeof(path), cfg->directory, prefix, "_direct.wav") != 0)
        return -1;
    int rc = write_wav(path, direct, cfg->filter_len, cfg->sample_rate);
    if (rc != 0) return rc;

    if (make_wav_path(path, sizeof(path), cfg->directory, prefix, "_cross_left.wav") != 0)
        return -1;
    rc = write_wav(path, cross_l, cfg->filter_len, cfg->sample_rate);
    if (rc != 0) return rc;

    if (make_wav_path(path, sizeof(path), cfg->directory, prefix, "_cross_right.wav") != 0)
        return -1;
    return write_wav(path, cross_r, cfg->filter_len, cfg->sample_rate);
}

int main(int argc, char **argv) {
    xtc_conf_asym cfg = {
        .sample_rate = 48000,
        .filter_len  = 4096,
        .directory   = "filters",
        .prefix      = "",
    };

    const char *config_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "xtc_asym: -c needs a file argument\n");
                return 1;
            }
            config_path = argv[++i];
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "-u") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "xtc_asym: unexpected argument '%s'\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    if (!config_path) {
        usage(argv[0]);
        return 1;
    }

    char err[512];
    if (xtc_conf_load_asym(config_path, &cfg, err, sizeof(err)) != 0) {
        fprintf(stderr, "xtc_asym: %s: %s\n", config_path, err);
        return 4;
    }

    if (make_output_dir(cfg.directory) != 0) return 2;

    /* xtc_conf_side and xtc_asym_side hold the same four numbers; the split
     * keeps the configuration parser independent of the DSP library. */
    const xtc_asym_side left = {
        .itd_us      = cfg.left.itd_us,
        .ild_db      = cfg.left.ild_db,
        .ild_alpha   = cfg.left.ild_alpha,
        .azimuth_deg = cfg.left.azimuth_deg,
    };
    const xtc_asym_side right = {
        .itd_us      = cfg.right.itd_us,
        .ild_db      = cfg.right.ild_db,
        .ild_alpha   = cfg.right.ild_alpha,
        .azimuth_deg = cfg.right.azimuth_deg,
    };

    double *direct  = calloc((size_t)cfg.filter_len, sizeof(double));
    double *cross_l = calloc((size_t)cfg.filter_len, sizeof(double));
    double *cross_r = calloc((size_t)cfg.filter_len, sizeof(double));
    if (!direct || !cross_l || !cross_r) {
        fprintf(stderr, "Memory allocation failed for XTC buffers\n");
        free(direct); free(cross_l); free(cross_r);
        return 3;
    }

    int rc = process_asym(&left, &right, cfg.sample_rate, cfg.filter_len,
                          direct, cross_l, cross_r);
    if (rc == 0) {
        rc = save_asym_wavs(&cfg, direct, cross_l, cross_r);
    }

    free(direct);
    free(cross_l);
    free(cross_r);
    return rc;
}
