# Quick reference — natambio / jackd under systemd (<user>)

> **Disclaimer:** `start_jackd_systemd.sh` and `start_natambio_systemd.sh` are
> scripts from a specific audio system and are **not generic**. They hard-code the
> sound cards installed on that system (Focusrite Scarlett 6i6 USB, Echo AudioFire4/8,
> Edirol FA66/FA101) and reference a particular directory layout and naming convention
> for natambio's XML configuration files. Before using them on a different system,
> both scripts must be adapted to match the audio interfaces present and the local
> repository of XML configs. They are provided as reference examples to be modified
> as needed when designing a natambio-as-a-service setup for each specific case.

## User services (`systemctl --user`)

| Service | Function | Policy |
|---|---|---|
| `natambio-jackd.service` | jackd (JACK server) | `Restart=always` |
| `natambio.service` | natambio (DSP processor) | — |

**Relationship:** natambio lives exactly as long as jackd.

- If jackd crashes → systemd restarts it within 1 s and brings natambio back up.
- If you stop jackd → natambio stops with it (`BindsTo`).
- If jackd comes back → natambio restarts on its own (`Upholds`).

Replaces the bash watchdog (`check_natambio.sh`). CPU cost ≈ 0 (no polling).

## Files

```
~/control_scripts/start_jackd_systemd.sh      # jackd launcher, foreground
~/control_scripts/start_natambio_systemd.sh   # natambio launcher, foreground
~/control_scripts/wait_jack.sh                # waits for JACK ports
~/.config/systemd/user/natambio-jackd.service
~/.config/systemd/user/natambio.service
```

> **Note:** `systemctl --user` does NOT require sudo. Only `loginctl enable-linger` and editing `/etc/rc.local` need sudo.

---

## 1. Initial setup *(once only, to cut over from the old watchdog)*

```sh
# Allow services to start at boot without a login session:
sudo loginctl enable-linger <user>

# Register and enable:
systemctl --user daemon-reload
systemctl --user enable natambio-jackd.service natambio.service

# Stop the bash watchdog and whatever rc.local started:
sudo pkill -f check_natambio.sh
pkill -9 -x natambio ; pkill -9 -x jackd

# Start the new setup (jackd brings natambio up via Upholds):
systemctl --user start natambio-jackd.service

# Prevent rc.local from relaunching the watchdog on next boot:
sudo patch -p1 /etc/rc.local < ~/control_scripts/rc.local.patch
```

---

## 2. Daily use

**Status:**

```sh
systemctl --user status natambio-jackd natambio
```

**Logs:**

```sh
journalctl --user -u natambio-jackd -u natambio -f   # live
journalctl --user -u natambio -e                     # last lines
```

**Start everything:**

```sh
systemctl --user start natambio-jackd.service        # natambio follows
```

**Stop everything** (will NOT auto-restart, won't fight you):

```sh
systemctl --user stop natambio-jackd.service         # natambio stops with it
```

**Restart natambio only** (e.g. after editing the XML):

```sh
systemctl --user restart natambio.service
```

**Restart jackd** (natambio follows):

```sh
systemctl --user restart natambio-jackd.service
```

---

## 3. Development / debug / testing — how to disable

**Level 1 — Pause** (resumes on next boot):

```sh
systemctl --user stop natambio-jackd.service
# ...make changes...
systemctl --user start natambio-jackd.service
```

**Level 2 — Do not start at boot** (still available manually):

```sh
systemctl --user disable --now natambio-jackd.service natambio.service
# revert:
systemctl --user enable --now natambio-jackd.service
```

**Level 3 — Full lock** (nothing can start them: not dependencies, not boot, not start):

```sh
systemctl --user mask natambio-jackd.service natambio.service
# ...debug your version manually (see below)...
systemctl --user unmask natambio-jackd.service natambio.service
```

**Run manually** (after `stop` or `mask`), in the foreground with visible output:

```sh
~/control_scripts/start_jackd_systemd.sh        # terminal 1
~/control_scripts/start_natambio_systemd.sh     # terminal 2
# or run jackd/natambio directly with your own arguments.
```

> **Mental model:**
> - `stop` / `disable` = comfortable pause for tinkering (nothing restarts you).
> - `mask` = hard lock while testing an alternative build (remember to `unmask` + `start` when done).

---

## 4. Notes

- **`jackd.service` / `jackd.socket`** remain masked to `/dev/null` on purpose
  (prevents D-Bus from auto-starting the packaged jackdbus). Do not touch. That
  is why our service is called `natambio-jackd`, not `jackd`.

- **Cards supported by `start_jackd_systemd.sh`:** the script works with USB (ALSA)
  and FireWire (FFADO) interfaces. USB cards are identified by the `USB_EXPECTED`
  string (the card name as reported by `/proc/asound/cards`), which lets the script
  select the right interface when more than one USB audio device is connected.
  FireWire cards are identified by their GUID; each supported device has its GUID
  defined as a constant in the script (AudioFire4 ×1/×2, AudioFire8, Edirol FA66,
  Edirol FA101).

- **Multi-process mode** (separated: 2 natambio processes): instead of `natambio.service`
  use an instantiated `natambio@.service` template; see the comment at the end of
  `~/.config/systemd/user/natambio.service`.

- After editing any `.service` file: `systemctl --user daemon-reload`.
