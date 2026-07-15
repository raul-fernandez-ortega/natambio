/* xtc_sweep_ladspa.c
 *
 * LADSPA port of test_xtc_sweep.py  (Algorithm 1 - "Inversion & sum").
 *
 * Two audio inputs are downmixed to mono (mono = (L+R)/2) and fed to both
 * channels, exactly like the Python script. On the *active* channel of each
 * step an inverted, attenuated copy of that same channel is summed in:
 *
 *     out_left  = mono - gL * mono   (= mono * (1 - gL))
 *     out_right = mono - gR * mono   (= mono * (1 - gR))
 *
 * gL/gR are LINEAR factors 10**(dB/20); the inactive channel keeps gain 0.
 *
 * The sweep walks the STEP sequence forever (infinite loop): it rises on L
 * (-40 -> +6 dB), jumps to R at +6 dB and falls on R (+6 -> -40 dB), then
 * starts over. Between steps there is a sample-by-sample crossfade so gain
 * changes and the L<->R handover are click-free.
 *
 * Control ports:
 *     Step time (s)        - seconds per step  (variable; default 2.0)
 *     Transition time (s)  - crossfade duration (default 0.1)
 *
 * On every step change the plugin prints a line to stderr (gain, channel and
 * whether the channel is being phase-inverted by over-cancellation) so you can
 * follow the sweep when hosting it under ecasound.
 *
 * Build:  see Makefile   ->  xtc_sweep_ladspa.so
 * Label:  natambio_xtc_sweep
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ladspa.h>

/* ---- Port layout ---------------------------------------------------------*/
#define P_IN_L   0
#define P_IN_R   1
#define P_OUT_L  2
#define P_OUT_R  3
#define P_STEP   4   /* control: seconds per step   */
#define P_TRANS  5   /* control: crossfade seconds  */
#define N_PORTS  6

/* ---- Step sequence -------------------------------------------------------*/
/* Levels: -40..0 dB in 5 dB steps (9), then 0.5..6.0 dB in 0.5 dB steps (12). */
#define N_LEVELS 21
#define N_STEPS  (2 * N_LEVELS)   /* 21 on L (rising) + 21 on R (falling) = 42 */

typedef struct {
    unsigned long sr;

    /* Port pointers (connected by the host). */
    LADSPA_Data *in_l, *in_r, *out_l, *out_r, *p_step, *p_trans;

    /* Precomputed step table. */
    float gain_db[N_STEPS];   /* level in dB              */
    char  chan[N_STEPS];      /* 'L' or 'R' (active side) */

    /* Running state. */
    int   cur_k;          /* current step index                 */
    long  step_sample;    /* samples elapsed inside current step */
    int   need_trigger;   /* pending step-change handling        */
    long  loop;           /* completed sweep passes              */

    /* Crossfade of the linear gains. */
    float curL, curR;     /* gain currently applied  */
    float tL, tR;         /* crossfade target        */
    float dL, dR;         /* per-sample increment    */
    long  ramp;           /* remaining crossfade samples */
} XtcSweep;

/* ---- Build the level / step tables --------------------------------------*/
static void build_steps(XtcSweep *p)
{
    float levels[N_LEVELS];
    int i, n = 0;

    for (i = -40; i <= 0; i += 5)          /* -40, -35, ... 0  (9 values) */
        levels[n++] = (float) i;
    for (i = 1; i <= 12; i++)              /* 0.5, 1.0, ... 6.0 (12 values) */
        levels[n++] = 0.5f * (float) i;

    /* Rising on L, then falling on R. */
    for (i = 0; i < N_LEVELS; i++) {
        p->gain_db[i] = levels[i];
        p->chan[i]    = 'L';
    }
    for (i = 0; i < N_LEVELS; i++) {
        p->gain_db[N_LEVELS + i] = levels[N_LEVELS - 1 - i];
        p->chan[N_LEVELS + i]    = 'R';
    }
}

/* ---- Apply a new step: set crossfade targets and announce it -------------*/
static void trigger_step(XtcSweep *p, long ov_frames)
{
    int   k      = p->cur_k;
    float g      = p->gain_db[k];
    char  ch     = p->chan[k];
    float factor = powf(10.0f, g / 20.0f);

    p->tL = (ch == 'L') ? factor : 0.0f;
    p->tR = (ch == 'R') ? factor : 0.0f;
    p->dL = (p->tL - p->curL) / (float) ov_frames;
    p->dR = (p->tR - p->curR) / (float) ov_frames;
    p->ramp = ov_frames;

    /* g > 0 dB  =>  (1 - g) < 0  =>  the channel over-cancels and flips phase. */
    fprintf(stderr,
            "[xtc_sweep] >> %+5.1f dB  %c%s   (loop %ld)\n",
            g, ch,
            (g > 0.0f) ? "  [over-cancel / phase INVERTED]" : "",
            p->loop + 1);
}

/* ---- LADSPA hooks --------------------------------------------------------*/
static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long sr)
{
    XtcSweep *p = (XtcSweep *) calloc(1, sizeof(XtcSweep));
    (void) d;
    if (!p) return NULL;
    p->sr = sr;
    build_steps(p);
    return (LADSPA_Handle) p;
}

static void connect_port(LADSPA_Handle h, unsigned long port, LADSPA_Data *data)
{
    XtcSweep *p = (XtcSweep *) h;
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
    XtcSweep *p = (XtcSweep *) h;
    p->cur_k       = 0;
    p->step_sample = 0;
    p->need_trigger = 1;   /* announce step 0 on the first sample */
    p->loop        = 0;
    p->curL = p->curR = 0.0f;
    p->tL = p->tR = 0.0f;
    p->dL = p->dR = 0.0f;
    p->ramp = 0;
    fprintf(stderr,
            "[xtc_sweep] active @ %lu Hz | %d steps, infinite loop "
            "(inversion & sum)\n", p->sr, N_STEPS);
}

static void run(LADSPA_Handle h, unsigned long n)
{
    XtcSweep *p = (XtcSweep *) h;
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
        float mono;

        if (p->need_trigger) {
            trigger_step(p, ov_frames);
            p->need_trigger = 0;
        }

        /* Advance the gain crossfade (linear ramp -> plateau). */
        if (p->ramp > 0) {
            p->curL += p->dL;
            p->curR += p->dR;
            if (--p->ramp == 0) {   /* snap to the exact target */
                p->curL = p->tL;
                p->curR = p->tR;
            }
        }

        mono = 0.5f * ((float) inL[i] + (float) inR[i]);
        outL[i] = mono - p->curL * mono;
        outR[i] = mono - p->curR * mono;

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
    free(h);
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

    s_desc.UniqueID        = 0x4E410001;  /* 'NA' 01 - unofficial local ID */
    s_desc.Label           = "natambio_xtc_sweep";
    s_desc.Properties      = 0; /* in-place safe: inputs are read before outputs are written */
    s_desc.Name            = "NatAmbio XTC Sweep (inversion & sum)";
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
