# Agent Decisions Log

This file records **major changes** to the BMU Test Tool, the **reasoning** behind
them, and any **assumptions or risks** that a future maintainer (human or agent)
should know about. Newest entries first.

Each entry should capture: what changed, why, what was deliberately *not* done,
and anything that still needs verification (e.g. against real hardware).

---

## 2026-06-12 — Validate & apply pin-configuration sequence (TTC 2038XS)

**Change.** Added a one-click **"Validate & apply configuration"** button at the
bottom of the Pins tab that runs the documented configuration-validation sequence
so the device will accept its pin config and be allowed into Operational. It does
**not** change the NMT state (the user sends Start manually).

**Supporting code (`Ttc2038XsDevice`).** A chained SDO sequence driven from
`dispatch_result` via three new job kinds, with an `ApplyConfigState` state
machine (`Idle/WritingConfigValid/ReadingSignature/WritingSignature/Done/Failed`)
plus `apply_config_message()` / `apply_config_signature()`:

1. `ConfigValidWrite` — write `0xA5` to Configuration Valid `0x13FE`.
2. `PinSignatureRead` — read the computed signature from Pin Configuration
   Signature `0x2000`.
3. `SafetySignatureWrite` — write that exact signature to Safety Pin
   Configuration Signature `0x2010`.

Each step chains the next only on success; any abort/timeout sets `Failed` with
the SDO message. A failed-to-start guard in `poll()` prevents the sequence from
hanging if an SDO can't be issued.

**Reasoning.** The device was stuck in Pre-Operational. The signature written to
`0x2010` is **not a constant** — it must be **read from `0x2000`**, which the
device computes from the actual pin configuration. Doing this by hand via the raw
Advanced tab (write 0xA5, read 0x2000, copy the bytes, write 0x2010) is fiddly
and error-prone, and the read-back-then-write data dependency is exactly the kind
of multi-step flow the per-object UI couldn't express. The user asked to set pin
modes on the Pins tab, click one button to automate the rest, then start the node
manually — so the sequence deliberately stops before NMT Start.

**Notes / limitations.**
- The sequence intentionally omits the manual's steps 4–5 ("read back the whole
  configuration and verify") — it trusts the modes just written via the Pins tab.
  A stricter implementation could re-read and diff.
- "Configuration Valid (0x13FE) must be written even with no SRDOs, or the device
  silently refuses Operational" — this is the main value of the flow and is now
  always done.
- On failure the UI points the user at Development mode (Control tab) as the
  bench-bringup fallback.

---

## 2026-06-12 — Active NMT-state polling + heartbeat freshness (TTC 2038XS)

**Change.** Added a way to actively determine the device's current NMT state
instead of relying solely on the latched heartbeat byte. On the Control tab's NMT
section: a live state line with **freshness** ("via heartbeat 0.4s ago" /
"stale" / "no 0x700 frame seen"), a **Poll state now** button, and **Enable
heartbeat (1s)** / **Disable heartbeat** helpers. The always-visible header bar
now also tags a stale NMT state with "(stale)".

**Supporting code (`Ttc2038XsDevice`).**
- `poll_nmt_state()` — issues an SDO read of Producer Heartbeat Time (`0x1017`)
  and records the result in `DeviceStatus` (`nmt_probe_*`). A response (data *or*
  any SDO abort) proves the node is alive; only a timeout means no response. New
  `JobKind::NmtPollProbe`, with a failed-to-start guard so the UI never hangs on
  "pending".
- `set_producer_heartbeat(ms)` — writes `0x1017` to turn continuous heartbeat
  reporting on (non-zero ms) or off (0).
- `seconds_since_heartbeat()` + new `DeviceStatus` fields (`last_heartbeat_us`,
  `status_now_us`) — `poll()` samples a steady-clock "now" alongside the node's
  `last_heartbeat_us` so the UI can compute staleness.

**Reasoning.** The device was appearing "stuck in Boot-up". Root cause: the tool
latched `0x700` data byte as the NMT state, and the producer heartbeat defaults
to **disabled** (`0x1017 == 0`), so the device emits only the single boot-up
frame (`0x00`) and then goes silent on `0x700` — leaving a stale "Boot-up"
display even when the device is actually fine. The device has **no node-guarding
objects** (`0x100C`/`0x100D` absent), so there is no RTR "ping NMT state"
primitive. The two robust alternatives are (a) read any object via SDO to prove
liveness *now* (Poll state now), and (b) enable the heartbeat so the real state
is reported continuously — both are now exposed.

**Honest limitations.**
- The probe reads `0x1017`; a successful SDO proves the node is *communicating*,
  but the displayed NMT *state* still comes from the last heartbeat/boot-up byte.
  An SDO server responds in Pre-Operational/Operational/Stopped alike, so "alive"
  does not by itself disambiguate Pre-Op vs Operational — enabling the heartbeat
  (or reading `0x2400` ECU state) is the way to get the true state. The UI says
  "alive and communicating", not "Operational".
- Staleness threshold is a fixed 3 s; not derived from the configured heartbeat
  period.

**Change.** Added enable/disable controls for the device's **development mode** to
the TTC Control tab, backed by two device-layer methods:

- `Ttc2038XsDevice::enable_development_mode()` — queues SDO writes `0x65 → 0x2011`
  (Enable Development Mode) then `0x706C7664 → 0x2010` (Safety Pin Config
  Signature), per the user-manual sequence.
- `Ttc2038XsDevice::disable_development_mode()` — writes `0x00 → 0x2011` to clear
  the enable (returns the object to its EDS default).

The UI section (between Diagnostics and Persistence) requires Control mode, warns
that it bypasses safety verification, notes the device-emitted warning EMCY
(`0x0009` / `0xFE` / `0xF00A`) while active, prompts for Pre-Operational, and
gates the **Enable** button behind an "I understand (arm)" checkbox.

**Reasoning.** Development mode skips SRDO + pin-configuration verification on the
transition to Operational. It is the documented way to bring up a device that
won't go Operational because of a configuration/OD fault — directly relevant to
the observed `0xC0EF0004` "I/O Module Application" status (decoded as a
fatal/OD-config-class fault; see the session notes). Exposing a one-click toggle
beats hand-typing magic values via the Advanced raw-SDO tab.

**Encoding note.** `0x2010` is `UNSIGNED32`; the signature `0x706C7664` is written
little-endian as bytes `64 76 6C 70` ("dvlp"). `0x2011` is `UNSIGNED8`.

**Deliberately not done.**
- No read-back/indicator of whether dev mode is *currently* active. The device
  has no dedicated "is enabled" object; the intended signal is the recurring
  warning EMCY. A future improvement: surface that EMCY (and `0x1003`) in the
  Status tab so the active state is visible.
- "Disable" only clears `0x2011`; the device still needs a **node reset** (and a
  real signature for verified operation) before it re-enforces verification.
  This is documented in the button's behavior but not automated.

**Risk.** Writes to safety-configuration objects. Guarded by the arm checkbox and
Control-mode/Pre-Operational hints, but there is no hardware-state confirmation
that the writes took effect (the device may reject them outside Pre-Operational —
surfaced only as an SDO abort in the queue).

---

## 2026-06-12 — Dynamic Control tab driven by pin configuration

**Change.** Rebuilt `Ttc2038XsPanel::render_control_tab()` so the device control
UI is generated from each pin's **current Pin Mode** instead of a fixed
"Digital outputs (DO 1-8)" grid. The tab now renders one functional section per
`IoFunction` that has at least one configured pin:

- **Digital Output** — interactive tiles (configured DOP pins only).
- **PWM Output** / **Low Power Output** — per-pin duty-cycle field + Write.
- **Digital Input / Analog Input / Timer Input / SENT Input** — read-only live
  values (HIGH/low for DIN, value + unit otherwise).

NMT controls and the Safe state / Diagnostics / Persistence sections are
unchanged.

**Supporting code.**
- `Ttc2038Xs.h/.cpp`: added `IoFunction` enum, `ModeBehavior`, `mode_behavior()`
  (maps a numeric mode code → section + writability + native operation object-ID
  + unit), and `od::io_operation_index()` (computes per-pin operation objects
  from the I/O-block index layout).
- `Ttc2038XsDevice.h/.cpp`: added `PinIoValue` cache plus
  `read_pin_io()/read_all_pin_io()/write_pin_io()` and a new `JobKind::PinIo`
  with function-aware decoding. Keyed by the same flat pin handle as pin modes.

**Reasoning / addressing model.** The user asked for the Control tab to reflect
pin configuration with sections per function, using **CiA 401 aggregate
objects**, with **all sections / full control**. Those two requests partly
conflict: the CiA 401 aggregates (`0x6000` DI, `0x6200` DO, `0x6401` AI) only
exist for digital/analog I/O — there are **no aggregate objects for PWM, LPO,
TIN, or SENT**. To honour both, a hybrid was chosen:

- **DI / DO / AI** stay on the aggregate objects already plumbed in the device.
  Digital Output tiles still command via `0x6200` (the existing
  `set_digital_outputs(mask)` path); each DOP pin maps to a DO bit by its
  position within the DOP pin group.
- **PWM / LPO** writes use **per-pin-group native operation objects**
  (`group_base + 0x20 + object_id`, e.g. duty cycle object-ID `9`), because no
  aggregate exists.

**Deliberately not done.**
- No NMT-state guard on writes (device rejects out-of-state writes itself; we
  surface the SDO abort instead of pre-blocking).
- Did not wire TIN per-mode value objects to their exact per-mode object-IDs
  beyond a single representative value; timer inputs currently read the
  status/level object as a placeholder.

**Risks / to verify on hardware.**
- The **DOP pin → DO bit** mapping (pin sub-index − 1) assumes the DOP group's
  sub-indices line up with `0x6200` bit positions. Confirm against a real
  module before relying on it.
- **PWM/LPO duty-cycle scaling and width** (encoded as a 16-bit value) is
  unverified — the exact units/range come from the per-mode object and should be
  checked.
- The Control tab now requires **"Read pin modes"** first so it knows which pins
  belong to which section. If a user expects DO tiles without reading modes,
  consider a static fallback.

---

## 2026-06-12 — Pins tab: per-pin mode configuration

**Change.** Added a new **"Pins"** sub-tab to `Ttc2038XsPanel` that reads and
writes each I/O pin's **Pin Mode** (object `0x3000 + (group << 7)`, one
sub-index per pin). Each pin gets a dropdown of the modes its group allows, plus
Read/Write buttons; a "Read all pin modes" action reads every pin.

**Supporting code.**
- `Ttc2038Xs.h/.cpp`: added the pin model — `PinModeOption`, `PinInfo`,
  `PinGroup`, `pin_groups()`, `pin_mode_name()`, and
  `od::pin_mode_index(group)`.
- `Ttc2038XsDevice.h/.cpp`: `PinModeValue` cache, `read_pin_mode()`,
  `read_all_pin_modes()`, `write_pin_mode()`, `pin_mode_handle()`,
  `pin_mode_count()`, and a `JobKind::PinMode`.

**Reasoning.** Pin configuration is the prerequisite for any meaningful I/O
control: the device's I/O objects only behave per the pin's configured mode.
A dedicated tab keeps the per-pin grid separate from the flat "Configure"
parameter table.

**Data source.** The numeric **mode codes are not in the EDS** (Pin Mode is
declared as a bare `UNSIGNED8`). They were extracted from the per-group
"Allowed values" lists in `resources/Ttc2038Xs.html` (the full object-dictionary
reference). The pin↔sub-index↔terminal mapping came from the EDS `[3000]`,
`[3100]`, `[3180]`, `[3200]`, `[3280]`, `[3400]` blocks. The pin-group → object
index relationship (`index = 0x3000 | group<<7`) is from the "I/O Block" section
of the user manual.

**Pin groups modelled** (group number → Pin Mode index):

| Group | Index | Pins | Allowed modes |
|------:|:------|:-----|:--------------|
| 0 ADC 4-mode | `0x3000` | J4,H4,E4,D4,C4,B4,A3,A4 | 0,1,2,3,4,5 |
| 2 TIN/SENT | `0x3100` | E3,D3,C3,B3 | 0,1,5,14,15,17,18 |
| 3 TIN/CL | `0x3180` | G4,F4 | 0,1,2,5,15,16,17,18 |
| 4 DOP HS 4A+CS/LPO | `0x3200` | K2,J2,H2,G2,F2,E2 | 0,1,5,6,8,9,10,11 |
| 5 PWM HS 4A+CM+FM | `0x3280` | K1,J1,H1,G1,F1,E1,D1,C1 | 0,1,5,12,13 |
| 8 PWM LS 4A+CM+FM | `0x3400` | A1,B1 | 0,1,3,5,12,13 |

**Deliberately not done (this pass).** Per the agreed scope: Pin Mode only — no
per-mode configuration objects (ADC limits, DIN thresholds, PWM period, etc.),
and no Pre-Operational guard or Configuration-Valid / Pin-Config-Signature
apply sequence (`0x13FE` / `0x2010`). Those are required for a *safe* apply but
were out of scope; the existing "Store to NVM" on the Control tab persists
written modes.

---

## (Earlier, pre-log) — CANopen support for the TTC 2038XS

These items predate this log; recorded here for context so later entries make
sense.

- **Hand-rolled CANopen client** (`src/canopen/`) rather than vendoring a stack
  (CANopenNode / Lely). It implements NMT, heartbeat tracking, a full SDO client
  (expedited + segmented up/download), and traffic statistics. Rationale: the
  tool only needs a *master/monitor* for one device over one bus, and a small
  self-contained implementation avoids a large external dependency and its build
  integration. (Note: the separate `resources/canopen_safety_integration.md`
  proposal — which targets the *other* Horizon BMU application — does choose
  CANopenNode; that decision is specific to that app, not this tool.)
- **PEAK PCAN backend** (`PcanBackend.*`) with the PCANBasic API declared
  locally and `PCANBasic.dll` resolved at runtime. Rationale: the project builds
  and runs without the PEAK SDK installed; absence of the DLL degrades to
  "driver unavailable" instead of a link error.
- **Control vs Monitor modes.** Monitor opens the adapter listen-only (never
  ACKs/transmits) so the tool can passively watch the live BMU↔device
  conversation; Control acts as an active master. Rationale: safe observation on
  a live bus without disturbing it.
- **Curated parameter table** instead of an EDS parser. The OD entries the UI
  exposes are hand-listed in `Ttc2038XsDevice.cpp`. Rationale: the device has
  hundreds of objects; only a curated subset is useful in the UI, and a parser
  would add complexity for little gain. The Advanced tab provides a raw SDO
  escape hatch for any other object.
- **SDO job sequencer** in `Ttc2038XsDevice`. All reads/writes are queued and
  serviced one-at-a-time from the UI's `poll()`; the `CanOpenClient` only allows
  one in-flight SDO transfer. Rationale: CANopen SDO is inherently a single
  outstanding request per server; the queue keeps the UI non-blocking.
