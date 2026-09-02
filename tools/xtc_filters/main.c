/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav_out.h"
#include "xtc_conf.h"
#include "xtc.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-c config.toml] [-t ITD(microsec)] [-l ILD(dB positive)]\n"
        "          [-a ILD_alpha(0-3)] [-z azimuth(degrees)] [-r SampleRate]\n"
        "          [-f FilterLength(samples)] [-F] [-M ModelDelay]\n"
        "\n"
        "  -c FILE  read parameters from a TOML file (see xtc_sym_*.toml).\n"
        "           Flags given after -c override the file, so a stored\n"
        "           configuration can be reused with one value changed.\n"
        "  -F       run the XTC recursion at the exact, unrounded ITD instead\n"
        "           of rounding it to whole samples. At 48 kHz the default\n"
        "           170 us is 8.16 samples, and rounding it to 8 caps crosstalk\n"
        "           suppression near 10 kHz some 55 dB above where the exact\n"
        "           ITD lands. Appends _frac to the output filenames.\n"
        "  -M N     bulk model delay in samples for -F. Defaults to the value\n"
        "           for the sample rate: %d samples at 48 kHz, scaled from there.\n"
        "\n"
        "Defaults: -t 170 -l 14 -a 2.0 -z 20 -r 48000 -f 4096\n",
        prog, XTC_DEFAULT_MODEL_DELAY);
}

/* save_xtc_wavs — writes the two final XTC filters to disk.
 *
 * With no output.prefix configured the historical filename contract is kept
 * verbatim, since it is what the surrounding scripts and the published
 * documentation expect:
 *   XTC_{AZ:02}_deg_ITD_{ITD_us}_micsec_ILD_{ILD:.1f}_dB_a_{ALPHA:.1f}_{direct|cross}.wav
 * A prefix replaces the whole descriptive part with <prefix>_{direct,cross}.wav,
 * which is the sensible choice once the parameters live in a TOML file that is
 * itself the record of the design.
 */
static int save_xtc_wavs(const xtc_conf_sym *cfg,
                         const double *direct, const double *cross) {
    char base[XTC_CONF_PATHMAX];
    char path[XTC_CONF_PATHMAX * 2];

    if (cfg->prefix[0] != '\0') {
        int n = snprintf(base, sizeof(base), "%s", cfg->prefix);
        if (n < 0 || (size_t)n >= sizeof(base)) return -1;
    } else {
        /* The fractional design gets its own suffix: same parameters, different
         * filters, and overwriting one pair with the other would be silent. */
        int n = snprintf(base, sizeof(base),
                         "XTC_%02d_deg_ITD_%d_micsec_ILD_%.1f_dB_a_%.1f%s",
                         cfg->xtc.azimuth_deg, cfg->xtc.itd_us,
                         cfg->xtc.ild_db, cfg->xtc.ild_alpha,
                         cfg->frac_delay ? "_frac" : "");
        if (n < 0 || (size_t)n >= sizeof(base)) {
            fprintf(stderr, "Generated filename too long\n");
            return -1;
        }
    }

    if (make_wav_path(path, sizeof(path), cfg->directory, base, "_direct.wav") != 0)
        return -1;
    int rc = write_wav(path, direct, cfg->filter_len, cfg->sample_rate);
    if (rc != 0) return rc;

    if (make_wav_path(path, sizeof(path), cfg->directory, base, "_cross.wav") != 0)
        return -1;
    return write_wav(path, cross, cfg->filter_len, cfg->sample_rate);
}

int main(int argc, char **argv) {
    /* Defaults identical to the Python counterpart. */
    xtc_conf_sym cfg = {
        .sample_rate = 48000,
        .filter_len  = 4096,
        .frac_delay  = 0,
        .model_delay = INT_MIN,   /* unset; resolved from the rate below */
        .xtc = { .itd_us = 170, .ild_db = 14.0, .ild_alpha = 2.0, .azimuth_deg = 20 },
        .directory = "filters",
        .prefix    = "",
    };

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* Same flag-then-value parser as the Python script (avoids GNU getopt to
     * preserve the exact semantics of the original script), with -c added.
     * The file is loaded where it appears in the command line, so flags placed
     * after it win and flags placed before it do not. */
    const char *nextarg = "";
    char err[512];

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (strcmp(a, "-c") == 0) nextarg = "CONFIG";
        else if (strcmp(a, "-t") == 0) nextarg = "ITD";
        else if (strcmp(a, "-l") == 0) nextarg = "ILD";
        else if (strcmp(a, "-a") == 0) nextarg = "FACTOR";
        else if (strcmp(a, "-z") == 0) nextarg = "AZIMUTH";
        else if (strcmp(a, "-r") == 0) nextarg = "SRATE";
        else if (strcmp(a, "-f") == 0) nextarg = "FILTERLEN";
        else if (strcmp(a, "-F") == 0) cfg.frac_delay = 1;
        else if (strcmp(a, "-M") == 0) nextarg = "MODELDELAY";
        else if (strcmp(a, "-h") == 0 || strcmp(a, "-u") == 0) {
            usage(argv[0]);
            return 0;
        } else if (a[0] != '-') {
            if      (strcmp(nextarg, "ITD")       == 0) cfg.xtc.itd_us      = atoi(a);
            else if (strcmp(nextarg, "ILD")       == 0) cfg.xtc.ild_db      = atof(a);
            else if (strcmp(nextarg, "AZIMUTH")   == 0) cfg.xtc.azimuth_deg = atoi(a);
            else if (strcmp(nextarg, "FACTOR")    == 0) cfg.xtc.ild_alpha   = atof(a);
            else if (strcmp(nextarg, "SRATE")     == 0) cfg.sample_rate     = atoi(a);
            else if (strcmp(nextarg, "FILTERLEN") == 0) cfg.filter_len      = atoi(a);
            else if (strcmp(nextarg, "MODELDELAY") == 0) cfg.model_delay     = atoi(a);
            else if (strcmp(nextarg, "CONFIG")    == 0) {
                if (xtc_conf_load_sym(a, &cfg, err, sizeof(err)) != 0) {
                    fprintf(stderr, "xtc: %s: %s\n", a, err);
                    return 4;
                }
            }
        }
    }

    /* Resolve the model delay now that the sample rate is final. An explicit
     * -M or a model_delay in the config file wins; otherwise it comes from the
     * rate, as it does in natambio, so a filter generated here carries the
     * same latency as the same design generated inside the processor. */
    if (cfg.model_delay == INT_MIN)
        cfg.model_delay = xtc_model_delay(cfg.sample_rate);

    if (make_output_dir(cfg.directory) != 0) return 2;

    /* The XTC output buffers live in main; process() fills them,
     * save_xtc_wavs persists them, and main frees them. */
    double *direct = calloc((size_t)cfg.filter_len, sizeof(double));
    double *cross  = calloc((size_t)cfg.filter_len, sizeof(double));
    if (!direct || !cross) {
        fprintf(stderr, "Memory allocation failed for XTC buffers\n");
        free(direct); free(cross);
        return 3;
    }

    int rc = process(cfg.xtc.itd_us, cfg.xtc.ild_db, cfg.xtc.ild_alpha,
                     cfg.xtc.azimuth_deg, cfg.sample_rate, cfg.filter_len,
                     cfg.frac_delay, cfg.model_delay,
                     direct, cross);
    if (rc == 0) {
        rc = save_xtc_wavs(&cfg, direct, cross);
    }

    free(direct);
    free(cross);
    return rc;
}
