/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sndfile.h>

#include "xtc.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -t ITD(microsec) -l ILD(dB positive) -a ILD_alpha(0-3) "
        "-z azimuth(degrees) -r SampleRate -f FilterLength(samples)\n", prog);
}

/* Not static: process() in xtc.c also calls it (forward-declared there). */
int write_wav(const char *path, const double *data, int n, int sample_rate) {
    SF_INFO info;
    memset(&info, 0, sizeof(info));
    info.samplerate = sample_rate;
    info.channels   = 1;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;   /* 32-bit IEEE float */

    SNDFILE *f = sf_open(path, SFM_WRITE, &info);
    if (!f) {
        fprintf(stderr, "Error opening %s: %s\n", path, sf_strerror(NULL));
        return -1;
    }
    sf_count_t written = sf_write_double(f, data, (sf_count_t)n);
    sf_close(f);
    if (written != (sf_count_t)n) {
        fprintf(stderr, "Short write to %s: %ld/%d\n", path, (long)written, n);
        return -2;
    }
    return 0;
}

/* save_xtc_wavs — writes the two final XTC filters to disk.
 * Named identically to ambio_filters_scipy.py (contract with new_ambio_filter.sh):
 *   XTC_{AZ:02}_deg_ITD_{ITD_us}_micsec_ILD_{ILD:.1f}_dB_a_{ALPHA:.1f}_{direct|cross}.wav
 */
static int save_xtc_wavs(const double *direct, const double *cross,
                          int itd_us, double ild_db, double ild_alpha,
                          int azimuth_deg, int sample_rate, int filter_len) {
    char path[512];
    snprintf(path, sizeof(path),
             "filters/XTC_%02d_deg_ITD_%d_micsec_ILD_%.1f_dB_a_%.1f_direct.wav",
             azimuth_deg, itd_us, ild_db, ild_alpha);
    int rc1 = write_wav(path, direct, filter_len, sample_rate);
    snprintf(path, sizeof(path),
             "filters/XTC_%02d_deg_ITD_%d_micsec_ILD_%.1f_dB_a_%.1f_cross.wav",
             azimuth_deg, itd_us, ild_db, ild_alpha);
    int rc2 = write_wav(path, cross, filter_len, sample_rate);
    return (rc1 != 0) ? rc1 : rc2;
}

int main(int argc, char **argv) {
    /* Default values identical to the Python script */
    int    itd_us      = 170;
    double ild_db      = 14.0;
    double ild_alpha   = 2.0;
    int    azimuth     = 20;
    int    sample_rate = 48000;
    int    filter_len  = 4096;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* Same flag-then-value parser as the Python script (avoids GNU getopt to
     * preserve the exact semantics of the original script). */
    const char *nextarg = "";
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (strcmp(a, "-t") == 0) nextarg = "ITD";
        else if (strcmp(a, "-l") == 0) nextarg = "ILD";
        else if (strcmp(a, "-a") == 0) nextarg = "FACTOR";
        else if (strcmp(a, "-z") == 0) nextarg = "AZIMUTH";
        else if (strcmp(a, "-r") == 0) nextarg = "SRATE";
        else if (strcmp(a, "-f") == 0) nextarg = "FILTERLEN";
        else if (strcmp(a, "-h") == 0 || strcmp(a, "-u") == 0) {
            usage(argv[0]);
            return 0;
        } else if (a[0] != '-') {
            if      (strcmp(nextarg, "ITD")       == 0) itd_us      = atoi(a);
            else if (strcmp(nextarg, "ILD")       == 0) ild_db      = atof(a);
            else if (strcmp(nextarg, "AZIMUTH")   == 0) azimuth     = atoi(a);
            else if (strcmp(nextarg, "FACTOR")    == 0) ild_alpha   = atof(a);
            else if (strcmp(nextarg, "SRATE")     == 0) sample_rate = atoi(a);
            else if (strcmp(nextarg, "FILTERLEN") == 0) filter_len  = atoi(a);
        }
    }

    if (mkdir("filters", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create filters/: %s\n", strerror(errno));
        return 2;
    }

    /* Los buffers de salida XTC viven en main; process los rellena,
     * save_xtc_wavs los persiste, y main los libera. */
    double *direct = calloc((size_t)filter_len, sizeof(double));
    double *cross  = calloc((size_t)filter_len, sizeof(double));
    if (!direct || !cross) {
        fprintf(stderr, "Memory allocation failed for XTC buffers\n");
        free(direct); free(cross);
        return 3;
    }

    int rc = process(itd_us, ild_db, ild_alpha, azimuth, sample_rate, filter_len,
                     direct, cross);
    if (rc == 0) {
        rc = save_xtc_wavs(direct, cross, itd_us, ild_db, ild_alpha,
                            azimuth, sample_rate, filter_len);
    }

    free(direct);
    free(cross);
    return rc;
}
