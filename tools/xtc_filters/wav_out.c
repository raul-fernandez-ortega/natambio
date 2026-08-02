/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "wav_out.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sndfile.h>

int write_wav(const char *path, const double *data, int n, int sample_rate)
{
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

int make_output_dir(const char *dir)
{
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create %s/: %s\n", dir, strerror(errno));
        return -1;
    }
    return 0;
}

int make_wav_path(char *out, size_t outlen,
                  const char *dir, const char *base, const char *suffix)
{
    int n = snprintf(out, outlen, "%s/%s%s", dir, base, suffix);
    if (n < 0 || (size_t)n >= outlen) {
        fprintf(stderr, "Output path too long: %s/%s%s\n", dir, base, suffix);
        return -1;
    }
    return 0;
}
