# LADSPA plugins — XTC / ITD-ILD sweeps

LADSPA ports of the two realtime sweep scripts in `../`:

| Script | LADSPA plugin | Label |
|--------|---------------|-------|
| `test_xtc_sweep.py` (inversion & sum) | `xtc_sweep_ladspa.so` | `natambio_xtc_sweep` |
| `test_xtc_ild_itd_sweep.py` (ITD/ILD parametric) | `ild_itd_sweep_ladspa.so` | `natambio_ild_itd_sweep` |

Same algorithms as the Python versions:

- **Two audio inputs, downmixed to mono** (`mono = (L+R)/2`) and fed to both
  channels, so the two outputs differ *only* by the swept effect.
- **Infinite loop**: the step sequence runs forever (no `--repeat`; it always
  wraps).
- **Variable step time** and **transition (crossfade) time** as control ports,
  with the same defaults as the scripts (`2.0 s` / `0.1 s`).
- On every step change the plugin **prints a line to stderr** so you can follow
  the sweep live when hosting it under ecasound.

The one implementation difference from the scripts: the Python ITD/ILD version
gets its delay by reading a looping in-RAM WAV at `position - D`. A streaming
plugin has no such buffer, so `ild_itd_sweep_ladspa` keeps a real **delay line**
(a ring buffer of the mono signal) instead. The delay and attenuation laws are
identical.

## Ports

| # | Port | Type | Default |
|---|------|------|---------|
| 0 | Input L | audio in | — |
| 1 | Input R | audio in | — |
| 2 | Output L | audio out | — |
| 3 | Output R | audio out | — |
| 4 | Step time (s) | control in | 2.0 |
| 5 | Transition time (s) | control in | 0.1 |

`-el:LABEL,STEP,TRANSITION` — omit the two numbers to take the defaults.

## Build & install

```sh
make            # -> xtc_sweep_ladspa.so, ild_itd_sweep_ladspa.so
make install    # copy both to ~/.ladspa
```

Make sure ecasound looks in `~/.ladspa`:

```sh
export LADSPA_PATH=$HOME/.ladspa:/usr/lib/ladspa
```

Check they are found:

```sh
ecasound -c -E "ladspa-register" </dev/null | grep -i natambio
```

## Use with ecasound

The stderr messages (step / gain / channel / inversion, or angle / delay /
atten) appear in the terminal as the sweep advances. Examples:

**Live: 2 JACK inputs → sweep → 2 JACK outputs** (JACK client `testing_XTC`):

```sh
ecasound -G:jack,testing_XTC,notransport \
         -f:f32,2,48000 \
         -i:jack \
         -el:natambio_xtc_sweep \
         -o:jack
```

This registers a JACK client named **`testing_XTC`** exposing two input ports
(`testing_XTC:in_1`, `testing_XTC:in_2`) and two output ports
(`testing_XTC:out_1`, `testing_XTC:out_2`). With no peer client after `-i:jack`
/ `-o:jack`, ecasound makes **no** automatic connections — wire the ports in
your patchbay (qpwgraph, QjackCtl, `jack_connect`, …): feed the two inputs from
your source and route the two outputs to the audio processor. Swap the label
for `natambio_ild_itd_sweep` to run the ITD/ILD sweep, and append `,step,trans`
to override the timings. Set `-f`'s sample rate to match your JACK/PipeWire
server (48000 here) so no resampling is inserted.

**Play a file and send the sweep to NatAmbio's JACK inputs** (2 s per step,
0.1 s crossfade — the defaults):

```sh
ecasound -f:f32,2,44100 -i track.wav \
         -el:natambio_xtc_sweep \
         -o:jack,natambio
```

**ITD/ILD sweep, custom timing** (1.5 s per step, 0.08 s transition):

```sh
ecasound -i track.wav \
         -el:natambio_ild_itd_sweep,1.5,0.08 \
         -o:jack,natambio
```

**Render to a file instead of JACK** (for inspection):

```sh
ecasound -f:f32,2,44100 -i track.wav \
         -el:natambio_xtc_sweep,2,0.1 \
         -o sweep_out.wav
```

`-o:jack,natambio` connects ecasound's two outputs to NatAmbio's input ports in
order (`front_input_left`, `front_input_right`), the same destination the
Python scripts use. Adjust the client/port name to match your JACK graph
(`ecasound -o:jack` with no client auto-connects to the physical playback
ports).

> Note: these plugins print from `run()`, so they are **not** flagged
> `HARD_RT_CAPABLE`. That is fine for ecasound; it is only relevant for strict
> hard-realtime hosts.
