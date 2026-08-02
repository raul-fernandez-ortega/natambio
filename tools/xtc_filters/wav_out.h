/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef WAV_OUT_H
#define WAV_OUT_H

#include <stddef.h>

/* write_wav — writes `n` doubles as a mono 32-bit IEEE float WAV.
 *
 * Not static, and deliberately so: DEBUG builds of lib/xtc.c call it to dump
 * the intermediate ILD filters, forward-declaring it rather than including this
 * header (xtc.c must stay free of tool-local includes -- see the note at the
 * top of lib/xtc_asym.h about third-party ports mirroring that file).
 *
 * Returns 0 on success, non-zero on error, reporting to stderr. */
int write_wav(const char *path, const double *data, int n, int sample_rate);

/* make_output_dir — creates `dir` if it does not exist. Returns 0 on success. */
int make_output_dir(const char *dir);

/* make_wav_path — builds "<dir>/<base><suffix>" into `out`.
 *
 * Returns 0 on success, -1 if the result would not fit, reporting to stderr.
 * Truncating here would silently write the filter to a different file than the
 * one named, so the length is checked rather than assumed. */
int make_wav_path(char *out, size_t outlen,
                  const char *dir, const char *base, const char *suffix);

#endif
