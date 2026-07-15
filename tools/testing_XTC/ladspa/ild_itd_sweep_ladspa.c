/* ild_itd_sweep_ladspa.c
 *
 * LADSPA port of test_xtc_ild_itd_sweep.py  (Algorithm 2 - "ITD/ILD parametric").
 *
 * Two audio inputs are downmixed to mono (mono = (L+R)/2) and fed to both
 * channels, exactly like the Python script. On the *active* channel of each
 * step the signal is DELAYED and ATTENUATED as a function of an angle z (deg);
 * the other channel is pass-through:
 *
 *     active channel = attenuate( delay( mono ) )
 *     other  channel = mono
 *
 * Per-step laws (z in degrees):
 *     delay (us) = 5.674*z + 184.131*sin(z)        -> integer samples   (ITD)
 *     atten (dB) = -0.10 + 0.407*z - 0.0025*z^2     -> attenuation        (ILD)
 *
 * Unlike the Python version (which reads a looping WAV in RAM at position-D),
 * a real streaming delay line (ring buffer of the mono signal) provides the
 * delayed samples here.
 *
 * The angle z runs 0->90 deg on L, then 90->0 deg on R, in +5 deg steps, with
 * an "invert" flag flipping the active channel polarity on the falling legs.
 * The sequence runs forever (infinite loop). Between steps a sample-by-sample
 * crossfade keeps the delay change, the level change and the L<->R handover
 * click-free.
 *
 * Control ports:
 *     Step time (s)        - seconds per step  (variable; default 2.0)
 *     Transition time (s)  - crossfade duration (default 0.1)
 *
 * On every step change the plugin prints a line to stderr (angle, channel,
 * polarity, delay in samples/us and attenuation in dB) so you can follow the
 * sweep when hosting it under ecasound.
 *
 * Build:  see Makefile   ->  ild_itd_sweep_ladspa.so
 * Label:  natambio_ild_itd_sweep
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ladspa.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Port layout ---------------------------------------------------------*/
#define P_IN_L   0
#define P_IN_R   1
#define P_OUT_L  2
#define P_OUT_R  3
#define P_STEP   4   /* control: seconds per step   */
#define P_TRANS  5   /* control: crossfade seconds  */
#define N_PORTS  6

/* ---- Step sequence -------------------------------------------------------*/
/* z: 0..90 deg in +5 deg steps -> 19 values, over four phases. */
#define N_UP    19
#define N_STEPS (4 * N_UP)   /* L up, L down(inv), R up, R down(inv) = 76 */

typedef struct {
    unsigned long sr;

    /* Port pointers (connected by the host). */
    LADSPA_Data *in_l, *in_r, *out_l, *out_r, *p_step, *p_trans;

    /* Precomputed step table. */
    int   z_deg[N_STEPS];     /* angle (for the message)        */
    int   inv[N_STEPS];       /* polarity flag (for the message) */
    int   delay[N_STEPS];     /* delay in samples                */
    float atten[N_STEPS];     /* signed linear factor (- if inv) */
    char  chan[N_STEPS];      /* 'L' or 'R' (active side)        */

    /* Mono delay line (ring buffer). */
    float *ring;
    int    ring_size;         /* power of two   */
    int    ring_mask;
    int    write_idx;

    /* Running state. */
    int   cur_k;
    long  step_sample;
    int   need_trigger;
    long  loop;

    /* Crossfade between the previous ("old") and current ("cur") params. */
    int   curD, oldD;
    float curA, oldA;
    char  curCh, oldCh;
    long  ramp;               /* remaining crossfade samples */
    long  ov_frames;          /* crossfade length in force   */
} IldItd;

/* ---- Per-step physical laws ---------------------------------------------*/
static double delay_us(int z)
{
    return 5.674 * z + 184.131 * sin(z * M_PI / 180.0);
}
static double atten_db(int z)
{
    return -0.10 + 0.407 * z - 0.0025 * (double) z * (double) z;
}

/* ---- Build the step table (delay depends on the sample rate) -------------*/
static void build_steps(IldItd *p)
{
    /* Phases: (channel, direction, invert). */
    struct { char ch; int down; int inv; } phase[4] = {
        {'L', 0, 0},   /* z 0..90, normal   */
        {'L', 1, 1},   /* z 90..0, inverted */
        {'R', 0, 0},   /* z 0..90, normal   */
        {'R', 1, 1},   /* z 90..0, inverted */
    };
    int ph, j, k = 0, max_delay = 0;

    for (ph = 0; ph < 4; ph++) {
        for (j = 0; j < N_UP; j++) {
            int z = phase[ph].down ? (90 - 5 * j) : (5 * j);
            int d = (int) (delay_us(z) * 1e-6 * (double) p->sr + 0.5);
            float a = (float) pow(10.0, -atten_db(z) / 20.0);
            if (phase[ph].inv) a = -a;
            p->z_deg[k] = z;
            p->inv[k]   = phase[ph].inv;
            p->delay[k] = d;
            p->atten[k] = a;
            p->chan[k]  = phase[ph].ch;
            if (d > max_delay) max_delay = d;
            k++;
        }
    }

    /* Ring buffer large enough for the biggest delay, power of two. */
    p->ring_size = 1;
    while (p->ring_size < max_delay + 2)
        p->ring_size <<= 1;
    if (p->ring_size < 64) p->ring_size = 64;
    p->ring_mask = p->ring_size - 1;
}

/* ---- Apply a new step: shift cur->old, set target, announce it -----------*/
static void trigger_step(IldItd *p, long ov_frames)
{
    int k = p->cur_k;

    p->oldD  = p->curD;  p->oldA = p->curA;  p->oldCh = p->curCh;
    p->curD  = p->delay[k];
    p->curA  = p->atten[k];
    p->curCh = p->chan[k];
    p->ramp  = ov_frames;
    p->ov_frames = ov_frames;

    fprintf(stderr,
            "[ild_itd_sweep] >> z=%3d deg  %c %s  delay=%4d smp (%7.1f us)  "
            "atten=%+5.2f dB   (loop %ld)\n",
            p->z_deg[k], p->chan[k], p->inv[k] ? "INV" : "---",
            p->delay[k], delay_us(p->z_deg[k]), atten_db(p->z_deg[k]),
            p->loop + 1);
}

/* ---- LADSPA hooks --------------------------------------------------------*/
static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long sr)
{
    IldItd *p = (IldItd *) calloc(1, sizeof(IldItd));
    (void) d;
    if (!p) return NULL;
    p->sr = sr;
    build_steps(p);
    p->ring = (float *) calloc((size_t) p->ring_size, sizeof(float));
    if (!p->ring) { free(p); return NULL; }
    return (LADSPA_Handle) p;
}

static void connect_port(LADSPA_Handle h, unsigned long port, LADSPA_Data *data)
{
    IldItd *p = (IldItd *) h;
    switch (port) {
        case P_IN_L:   p->in_l    = data; break;
        case P_IN_R:   p->in_r    = data; break;
        case P_OUT_L:  p->out_l   = data; break;
        case P_OUT_R:  p->out_r   = data; break;
        case P_STEP:   p->p_step  = data; break;
        case P_TRANS:  p->p_trans = data; break;
        default: break;
    }
}

static void activate(LADSPA_Handle h)
{
    IldItd *p = (IldItd *) h;
    p->cur_k        = 0;
    p->step_sample  = 0;
    p->need_trigger = 1;   /* announce step 0 on the first sample */
    p->loop         = 0;
    p->curD = p->oldD = 0;
    p->curA = p->oldA = 1.0f;
    p->curCh = p->oldCh = p->chan[0];
    p->ramp = 0;
    p->write_idx = 0;
    if (p->ring)
        memset(p->ring, 0, (size_t) p->ring_size * sizeof(float));
    fprintf(stderr,
            "[ild_itd_sweep] active @ %lu Hz | %d steps, infinite loop "
            "(ITD/ILD, ring=%d smp)\n", p->sr, N_STEPS, p->ring_size);
}

static void run(LADSPA_Handle h, unsigned long n)
{
    IldItd *p = (IldItd *) h;
    const LADSPA_Data *inL = p->in_l, *inR = p->in_r;
    LADSPA_Data *outL = p->out_l, *outR = p->out_r;
    unsigned long i;

    /* Read control ports; fall back to defaults if unset / non-positive. */
    float secs  = (p->p_step  && *p->p_step  > 0.0f) ? *p->p_step  : 2.0f;
    float trans = (p->p_trans && *p->p_trans > 0.0f) ? *p->p_trans : 0.1f;

    long step_frames = (long) (secs * (float) p->sr + 0.5f);
    long ov_frames   = (long) (trans * (float) p->sr + 0.5f);
    if (step_frames < 1) step_frames = 1;
    if (ov_frames   < 1) ov_frames   = 1;
    if (ov_frames >= step_frames) ov_frames = step_frames - 1;
    if (ov_frames   < 1) ov_frames   = 1;

    for (i = 0; i < n; i++) {
        float mono, delN, delO, t;
        float newL, newR, oldL, oldR;

        if (p->need_trigger) {
            trigger_step(p, ov_frames);
            p->need_trigger = 0;
        }

        /* Feed the delay line, then read the two tap positions. */
        mono = 0.5f * ((float) inL[i] + (float) inR[i]);
        p->ring[p->write_idx] = mono;
        delN = p->ring[(p->write_idx - p->curD) & p->ring_mask];
        delO = p->ring[(p->write_idx - p->oldD) & p->ring_mask];
        p->write_idx = (p->write_idx + 1) & p->ring_mask;

        /* Crossfade weight t: 0 -> 1 over ov_frames, then stays at 1. */
        if (p->ramp > 0) {
            t = (float) (p->ov_frames - p->ramp + 1) / (float) p->ov_frames;
            p->ramp--;
        } else {
            t = 1.0f;
        }

        /* Active channel = attenuate(delay(mono)); other = mono. */
        newL = (p->curCh == 'L') ? p->curA * delN : mono;
        newR = (p->curCh == 'R') ? p->curA * delN : mono;

        if (t < 1.0f) {
            oldL = (p->oldCh == 'L') ? p->oldA * delO : mono;
            oldR = (p->oldCh == 'R') ? p->oldA * delO : mono;
            outL[i] = (1.0f - t) * oldL + t * newL;
            outR[i] = (1.0f - t) * oldR + t * newR;
        } else {
            outL[i] = newL;
            outR[i] = newR;
        }

        /* End of step? move on; wrap the sweep forever. */
        if (++p->step_sample >= step_frames) {
            p->step_sample = 0;
            if (++p->cur_k >= N_STEPS) {
                p->cur_k = 0;
                p->loop++;
            }
            p->need_trigger = 1;
        }
    }
}

static void cleanup(LADSPA_Handle h)
{
    IldItd *p = (IldItd *) h;
    if (p) free(p->ring);
    free(p);
}

/* ---- Descriptor ----------------------------------------------------------*/
static LADSPA_PortDescriptor  s_port_desc[N_PORTS];
static LADSPA_PortRangeHint   s_port_hint[N_PORTS];
static const char            *s_port_name[N_PORTS];
static LADSPA_Descriptor      s_desc;

static void __attribute__((constructor)) init_descriptor(void)
{
    s_port_desc[P_IN_L]  = LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO;
    s_port_desc[P_IN_R]  = LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO;
    s_port_desc[P_OUT_L] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    s_port_desc[P_OUT_R] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    s_port_desc[P_STEP]  = LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;
    s_port_desc[P_TRANS] = LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;

    s_port_name[P_IN_L]  = "Input L";
    s_port_name[P_IN_R]  = "Input R";
    s_port_name[P_OUT_L] = "Output L";
    s_port_name[P_OUT_R] = "Output R";
    s_port_name[P_STEP]  = "Step time (s)";
    s_port_name[P_TRANS] = "Transition time (s)";

    /* Step time: [0, 8] s, default 2.0 (= 0*0.75 + 8*0.25). */
    s_port_hint[P_STEP].HintDescriptor =
        LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
        LADSPA_HINT_DEFAULT_LOW;
    s_port_hint[P_STEP].LowerBound = 0.0f;
    s_port_hint[P_STEP].UpperBound = 8.0f;

    /* Transition time: [0, 0.4] s, default 0.1 (= 0*0.75 + 0.4*0.25). */
    s_port_hint[P_TRANS].HintDescriptor =
        LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
        LADSPA_HINT_DEFAULT_LOW;
    s_port_hint[P_TRANS].LowerBound = 0.0f;
    s_port_hint[P_TRANS].UpperBound = 0.4f;

    s_port_hint[P_IN_L].HintDescriptor  = 0;
    s_port_hint[P_IN_R].HintDescriptor  = 0;
    s_port_hint[P_OUT_L].HintDescriptor = 0;
    s_port_hint[P_OUT_R].HintDescriptor = 0;

    s_desc.UniqueID        = 0x4E410002;  /* 'NA' 02 - unofficial local ID */
    s_desc.Label           = "natambio_ild_itd_sweep";
    s_desc.Properties      = 0; /* in-place safe: mono history lives in a separate ring buffer */
    s_desc.Name            = "NatAmbio ITD/ILD Sweep (parametric)";
    s_desc.Maker           = "NatAmbio";
    s_desc.Copyright       = "None";
    s_desc.PortCount       = N_PORTS;
    s_desc.PortDescriptors = s_port_desc;
    s_desc.PortNames       = s_port_name;
    s_desc.PortRangeHints  = s_port_hint;
    s_desc.ImplementationData = NULL;
    s_desc.instantiate     = instantiate;
    s_desc.connect_port    = connect_port;
    s_desc.activate        = activate;
    s_desc.run             = run;
    s_desc.run_adding      = NULL;
    s_desc.set_run_adding_gain = NULL;
    s_desc.deactivate      = NULL;
    s_desc.cleanup         = cleanup;
}

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return (index == 0) ? &s_desc : NULL;
}
