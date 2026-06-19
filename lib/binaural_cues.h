#ifndef BINAURAL_CUES_H
#define BINAURAL_CUES_H

double deg2rad(double deg);

/* ILD broadband — log-empirical fit to public HRTF datasets
 * (HUTUBS, RIEC, CIPIC, BiLi, ARI):
 *     ILD(f) = alpha · 10 · log10(max(f, 200 Hz)/1000 + 1) · sin(theta)
 *   theta : azimuth in radians
 *   freq  : frequency [Hz]; clamped internally to a minimum of 200 Hz
 *   alpha : empirical scale factor (typical values 1.8–2.0)
 * Returns ILD in dB (positive = right ear louder).
 */
double ild_log_empirical(double theta, double freq, double alpha);

#endif
