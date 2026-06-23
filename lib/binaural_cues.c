/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "binaural_cues.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double deg2rad(double deg) {
    return deg * M_PI / 180.0;
}

double ild_log_empirical(double theta, double freq, double alpha) {
    double f = freq < 200.0 ? 200.0 : freq;
    double A_f = alpha * 10.0 * log10(f / 1000.0 + 1.0);
    return A_f * sin(theta);
}
