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
 * The sweep walks the STEP sequence forever (infinite loop): on L it steps
 * the named channel down 0, -3, -6 ... -21 dB and then MUTES it (the widest
 * panning there is), carries on through the over-cancel region where that
 * channel comes back inverted, turns at the far end, and mirrors the whole
 * thing on R. Between steps there is a sample-by-sample crossfade so gain
 * changes and the L<->R handover are click-free.
 *
 * Control ports:
 *     Step time (s)        - seconds per step  (variable; default 2.0)
 *     Transition time (s)  - crossfade duration (default 0.1)
 *     In-phase only        - 0 (default) = test in phase AND phase-inverted,
 *                            sweeping the whole level range including the
 *                            over-cancel region above 0 dB where (1 - g) goes
 *                            negative and the channel flips polarity;
 *                            1 = in-phase only, the sweep stops at 0 dB (full
 *                            cancellation) and never goes anti-correlated.
 *                            Changing it restarts the sweep at the first step.
 *
 * On every step change the plugin prints a line to stderr so you can follow
 * the sweep when hosting it under ecasound. It gives what is LEFT of the named
 * channel relative to the untouched one, and where the image should go:
 *
 *   >> step  4/18 | L =  -9.0 dB vs R, in phase | image -> R
 *   >> step  9/18 | L MUTED (-inf dB vs R) | image -> R hard
 *   >> step 21/42 | L =  -0.0 dB vs R, INVERTED | image: de-localised
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
#define P_PHASE  6   /* control: 0 = both phases, 1 = in phase only */
#define N_PORTS  7

/* ---- Step sequence -------------------------------------------------------*/
/* A step is stored as the SIGNED FACTOR LEFT on the named channel, which is
 * what you hear: 1.0 = untouched, 0.5 = -6 dB, 0.0 = muted, negative = the
 * channel comes back phase-inverted. The gain of the inverted copy that the
 * DSP sums in is g = 1 - rem.
 *
 * In-phase leg: attenuation 0 -> 21 dB in 3 dB steps (8), then mute (1).
 * Even rungs in ATTENUATION keep the image moving; a ladder even in g
 * crowds most of its steps within a dB of the centre.
 * Over-cancel leg: g = +0.5 ... +6.0 dB in 0.5 dB steps (12). */
#define N_IN_ATT    8             /* 0, 3, 6 ... 21 dB of attenuation */
#define N_LEVELS_IN (N_IN_ATT + 1)          /* ... plus mute = 9      */
#define N_LEVELS    (N_LEVELS_IN + 12)      /* ... plus over-cancel   */
#define N_STEPS  (2 * N_LEVELS)   /* 21 on L (down) + 21 on R (up) = 42 */

typedef struct {
    unsigned long sr;

    /* Port pointers (connected by the host). */
    LADSPA_Data *in_l, *in_r, *out_l, *out_r, *p_step, *p_trans, *p_phase;

    /* Precomputed step table (only the first n_steps entries are in use). */
    float rem[N_STEPS];       /* signed factor left on the named channel */
    char  chan[N_STEPS];      /* 'L' or 'R' (named / cancelled side)     */
    int   n_steps;            /* steps in force for the current phase mode */
    int   inphase;            /* 1 = in-phase only, 0 = both, -1 = not built */

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
/* inphase_only = 1 ends each side at mute, so nothing left on the named
 * channel is ever negative and the output never becomes anti-correlated. */
static void build_steps(XtcSweep *p, int inphase_only)
{
    float levels[N_LEVELS];
    int i, n = 0;
    int n_levels = inphase_only ? N_LEVELS_IN : N_LEVELS;

    for (i = 0; i < N_IN_ATT; i++)         /* 0, -3, -6 ... -21 dB (8 values) */
        levels[n++] = powf(10.0f, -(3.0f * (float) i) / 20.0f);
    levels[n++] = 0.0f;                    /* mute: infinite attenuation */
    for (i = 1; i <= 12; i++)              /* over-cancel: g = +0.5 ... +6 dB */
        levels[n++] = 1.0f - powf(10.0f, (0.5f * (float) i) / 20.0f);

    /* Ladder down on L, then back up on R. */
    for (i = 0; i < n_levels; i++) {
        p->rem[i]  = levels[i];
        p->chan[i] = 'L';
    }
    for (i = 0; i < n_levels; i++) {
        p->rem[n_levels + i]  = levels[n_levels - 1 - i];
        p->chan[n_levels + i] = 'R';
    }

    p->n_steps = 2 * n_levels;
    p->inphase = inphase_only;
}

/* ---- Switch phase mode: rebuild the table and restart the sweep ----------*/
static void set_phase_mode(XtcSweep *p, int inphase_only)
{
    build_steps(p, inphase_only);
    p->cur_k        = 0;
    p->step_sample  = 0;
    p->need_trigger = 1;
    fprintf(stderr,
            "[xtc_sweep] phase mode: %s | %d steps\n",
            inphase_only ? "IN PHASE ONLY (0 ... -21 dB, then mute)"
                         : "both (in phase to mute, then inverted)",
            p->n_steps);
}

/* ---- Apply a new step: set crossfade targets and announce it -------------*/
static void trigger_step(XtcSweep *p, long ov_frames)
{
    int   k      = p->cur_k;
    float rem    = p->rem[k];       /* signed factor left on the named channel */
    char  ch     = p->chan[k];
    char  other  = (ch == 'L') ? 'R' : 'L';
    float factor = 1.0f - rem;      /* gain of the inverted copy to sum in */

    p->tL = (ch == 'L') ? factor : 0.0f;
    p->tR = (ch == 'R') ? factor : 0.0f;
    p->dL = (p->tL - p->curL) / (float) ov_frames;
    p->dR = (p->tR - p->curR) / (float) ov_frames;
    p->ramp = ov_frames;

    /* The NAMED CHANNEL IS THE ONE BEING CANCELLED, so the image moves to the
     * other side: centre at the top of the ladder, hard pan at mute, and past
     * mute what is left of it comes back inverted and the image de-localises
     * instead of panning further. */
    if (rem == 0.0f) {
        fprintf(stderr,
                "[xtc_sweep] >> step %2d/%d | %c MUTED (-inf dB vs %c)"
                " | image -> %c hard   (loop %ld)\n",
                k + 1, p->n_steps, ch, other, other, p->loop + 1);
    } else {
        float eff = 20.0f * log10f(fabsf(rem));   /* named ch level vs the other */
        char  img[40];
        if (rem > 0.0f) {
            if (eff > -1.0f) snprintf(img, sizeof img, "image: centre");
            else             snprintf(img, sizeof img, "image -> %c", other);
        } else {
            if (eff < -12.0f) snprintf(img, sizeof img, "image -> %c (anti-corr.)",
                                       other);
            else              snprintf(img, sizeof img, "image: de-localised");
        }
        fprintf(stderr,
                "[xtc_sweep] >> step %2d/%d | %c = %+5.1f dB vs %c, %s "
                "| %s   (loop %ld)\n",
                k + 1, p->n_steps, ch, eff, other,
                (rem > 0.0f) ? "in phase" : "INVERTED", img, p->loop + 1);
    }
}

/* ---- LADSPA hooks --------------------------------------------------------*/
static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long sr)
{
    XtcSweep *p = (XtcSweep *) calloc(1, sizeof(XtcSweep));
    (void) d;
    if (!p) return NULL;
    p->sr = sr;
    build_steps(p, 0);          /* both phases until run() reads the port */
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
        case P_PHASE:  p->p_phase = data; break;
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
    p->inphase = -1;       /* force a table (re)build on the first run() */
    fprintf(stderr,
            "[xtc_sweep] active @ %lu Hz | infinite loop "
            "(inversion & sum)\n", p->sr);
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
    int   inphase = (p->p_phase && *p->p_phase > 0.0f) ? 1 : 0;

    /* Phase mode changed (or first block): rebuild the table, restart sweep. */
    if (inphase != p->inphase)
        set_phase_mode(p, inphase);

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
            if (++p->cur_k >= p->n_steps) {
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
    s_port_desc[P_PHASE] = LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL;

    s_port_name[P_IN_L]  = "Input L";
    s_port_name[P_IN_R]  = "Input R";
    s_port_name[P_OUT_L] = "Output L";
    s_port_name[P_OUT_R] = "Output R";
    s_port_name[P_STEP]  = "Step time (s)";
    s_port_name[P_TRANS] = "Transition time (s)";
    s_port_name[P_PHASE] = "In-phase only (0/1)";

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

    /* Phase mode: toggle, default 0 = both phases (in phase + inverted). */
    s_port_hint[P_PHASE].HintDescriptor =
        LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
    s_port_hint[P_PHASE].LowerBound = 0.0f;
    s_port_hint[P_PHASE].UpperBound = 1.0f;

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
