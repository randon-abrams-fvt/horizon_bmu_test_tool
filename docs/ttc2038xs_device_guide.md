# TTC 2038XS — CANopen Device Guide (for Driver Implementation)

**Audience:** an engineer or agent writing a driver for the **TTControl
TTC 2038XS** CANopen Safety I/O module. This document is self-contained: it
describes the bus parameters, the object-dictionary layout, the configuration
and startup sequence, how to enable/drive outputs, and how to read inputs and
status — with the exact object indices, sub-indices, encodings, and SDO frame
formats.

**Primary sources** (in `resources/`): `Ttc2038Xs.eds` (machine-readable OD),
`Ttc2038Xs.html` (full OD reference incl. allowed values), `Ttc2038XsUserManual.html`
(behavioural manual), `_safety_manual.txt` (safety/timing). This guide
distils those plus the working implementation in `src/canopen/`.

> **Safety scope.** The 2038XS is a functional-safety device (ISO 13849 PL d).
> This guide covers the *transport and control* needed to read/write its objects.
> It does **not** cover building a *safety-rated* function (SRDO validation, GFC,
> reaction-time budgeting). Treat any output control here as engineering/bench
> control unless you implement the full safety protocol.

---

## 1. Bus & Node Parameters

| Property | Value |
|---|---|
| Physical layer | CAN 2.0A, **11-bit** identifiers only (no extended IDs) |
| Bitrate | **500 kbit/s only** (EDS declares no other) |
| Default node-ID | **10** (configurable via BRBL flash or CiA 305 LSS) |
| Boot-up | `SimpleBootUpSlave` — emits a boot-up frame and can auto-start |
| Heartbeat | Producer Heartbeat configurable (`0x1017`); consumer slots `0x1016` |
| SYNC COB-ID | `0x80` (`0x1005`) |
| EMCY COB-ID | `0x80 + node` (`0x1014`) |

**COB-ID scheme** (CiA 301 pre-defined connection set; `node` = 1..127):

| Function | COB-ID | Notes |
|---|---|---|
| NMT control | `0x000` | broadcast, 2 data bytes `[command, node]` |
| SYNC | `0x080` | broadcast |
| EMCY | `0x080 + node` | 8-byte emergency object |
| TPDO1..4 | `0x180/0x280/0x380/0x480 + node` | device → master |
| RPDO1..4 | `0x200/0x300/0x400/0x500 + node` | master → device |
| **SDO (tx, server→client)** | **`0x580 + node`** | SDO responses |
| **SDO (rx, client→server)** | **`0x600 + node`** | SDO requests |
| Heartbeat / boot-up | `0x700 + node` | 1 data byte = NMT state |
| SRDO (safety) | `0x101`/`0x102` … (pairs) | see §9 |

Code references: `src/canopen/CanOpenDefs.h` (`function_code()`, `node_of()`,
base constants).

---

## 2. NMT State Machine

NMT state is reported in the **heartbeat/boot-up** data byte (`0x700 + node`):

| Value | State |
|---|---|
| `0x00` | Boot-up (sent once at start) |
| `0x04` | Stopped |
| `0x05` | Operational |
| `0x7F` | Pre-Operational |

**NMT command frame** — COB-ID `0x000`, 2 data bytes `[command, node]`
(`node = 0` targets all nodes):

| Command | Byte 0 | Effect |
|---|---|---|
| Start | `0x01` | → Operational |
| Stop | `0x02` | → Stopped |
| Enter Pre-Operational | `0x80` | → Pre-Operational |
| Reset Node | `0x81` | full reset (re-runs boot) |
| Reset Communication | `0x82` | reset comms only |

**Why state matters for a driver:**

- **Configuration** objects and **Safety-configuration** objects can only be
  written in **Pre-Operational**.
- **Operation** objects (output levels, duty cycles) are only applied in
  **Operational**. Writing them in Pre-Operational has no effect.
- Outputs are **off** in any non-Operational state; entering the safe state
  forces **Stopped**.

**Determining the current NMT state (important):** the device reports its NMT
state only through the **heartbeat / boot-up** frame (`0x700 + node`):

- At power-up it emits **one boot-up frame** (data `0x00`), then auto-enters
  Pre-Operational.
- It emits **periodic heartbeats** (data = current state) **only if the producer
  heartbeat is enabled** — and **Producer Heartbeat Time `0x1017` defaults to
  `0` (disabled)**. With it disabled, the only `0x700` frame you ever see is the
  initial boot-up, so a naive "last 0x700 byte" tracker will appear **stuck in
  Boot-up** indefinitely even though the device is healthy.
- The device has **no node-guarding objects** (`0x100C`/`0x100D` are absent), so
  there is **no RTR "request NMT state" primitive**.

A driver should therefore do one of:

1. **Enable the heartbeat** — write a period (ms) to `0x1017` (e.g. 1000). The
   device then reports its NMT state continuously; treat the state as **stale**
   if no `0x700` frame arrives within ~2× the period.
2. **Probe liveness via SDO** — read any object (e.g. `0x1017` itself, or
   identity `0x1018`). A response (success *or* an SDO abort) proves the node is
   alive and communicating *now*. Note: an SDO server answers in Pre-Op,
   Operational and Stopped alike, so a successful SDO proves *liveness*, not a
   specific state.
3. **Read ECU diagnostic state `0x2400`** for the *operational* readiness
   (`Running (4)`), which is the most useful signal during bring-up.

Code references: `NmtCommand`, `NmtState` in `CanOpenDefs.h`;
`CanOpenClient::send_nmt()`; `Ttc2038XsDevice::poll_nmt_state()`,
`set_producer_heartbeat()`, `seconds_since_heartbeat()`.

---

## 3. SDO Transfer Protocol (read/write any object)

The driver talks to the OD via **SDO** (Service Data Object). One outstanding
transfer at a time per node. Request COB-ID `0x600+node`, response `0x580+node`,
always 8 data bytes.

### 3.1 Expedited read (upload), value ≤ 4 bytes

Request:
```
byte0 = 0x40
byte1 = index low
byte2 = index high
byte3 = sub-index
byte4..7 = 0
```
Response (`scs` = `byte0 & 0xE0` == `0x40`):
```
byte0 = 0x4n with expedited bit 0x02 set; if size-indicated (0x01),
        number of *unused* bytes = (byte0 >> 2) & 0x03
byte1..3 = echo index/sub
byte4..7 = data (little-endian), length = 4 - unused
```

### 3.2 Expedited write (download), value ≤ 4 bytes

Request:
```
byte0 = 0x23 | (n << 2)   where n = 4 - len   (0x23 = download|expedited|size)
        -> 1 byte:0x2F, 2 bytes:0x2B, 3 bytes:0x27, 4 bytes:0x23
byte1 = index low
byte2 = index high
byte3 = sub-index
byte4..7 = data (little-endian), unused bytes = 0
```
Success response: `byte0 == 0x60`, bytes1..3 echo index/sub.

### 3.3 Segmented transfers (value > 4 bytes, e.g. strings)

- **Upload:** initiate with `byte0=0x40`; server replies `0x41` (size in
  bytes4..7 if size-indicated). Then request segments with `0x60` (toggle bit
  `0x10` flips each segment); server replies `0x00|...`, data in bytes1..7,
  unused count = `(byte0>>1)&0x07`, last segment flagged by `byte0 & 0x01`.
- **Download:** initiate `0x21` with total size in bytes4..7; then send segments
  `0x00|(n<<1)` (n = 7−chunk), toggle `0x10`, last `0x01`; server acks `0x20`.

### 3.4 SDO abort

If `byte0 == 0x80`, the transfer failed; bytes4..7 are the **abort code**
(little-endian uint32). Common codes:

| Abort code | Meaning |
|---|---|
| `0x06010000` | Unsupported access |
| `0x06010001` | Attempt to read a write-only object |
| `0x06010002` | Attempt to write a read-only object |
| `0x06020000` | Object does not exist |
| `0x06090011` | Sub-index does not exist |
| `0x06090030` | Value range exceeded |
| `0x08000020` | Data cannot be transferred/stored |
| `0x05040000` | SDO protocol timed out (no response) — used to detect *absent* nodes during scans |

Code references: full state machine in `CanOpenClient.cpp` (`start_sdo`,
`send_sdo_segment_request`, `handle_sdo_response`, `finish_sdo`); abort text in
`CanOpenDefs.cpp` (`sdo_abort_text`).

---

## 4. Object Dictionary — Layout & Index Math

The 2038XS OD is **structured by bit-fields in the index**. Two blocks matter
for I/O:

### 4.1 Device block (`0x2000`–`0x2FFF`)

`index = 0x2000 | (area << 9) | area_specific`. Areas: 0 Device Function,
1 Device Pin Groups, 2 Device Status, 6 System Integrator, 7 Device Info.
Within function/pin areas: `bit3-4 = object function`, `bit0-2 = object id`.

### 4.2 I/O block (`0x3000`–`0x3FFF`) — **the important one**

```
index = 0x3000 | (io_pin_group << 7) | (object_function << 5) | object_id
```

- `io_pin_group` (bits 7..11): which physical pin group (see §5).
- `object_function` (bits 5..6): **0 = Config, 1 = Operation, 2 = Safety Config,
  3 = Safety Operation.**
- `object_id` (bits 0..4): the specific object, **common across pin groups**.

So to address the same logical object on a different pin group, only change the
group bits. The **sub-index** selects the pin *within* the group.

**Helper (from the implementation):**
```
pin_mode_index(group)            = 0x3000 + (group << 7)            // object_id 0, function 0
io_operation_index(group, objid) = 0x3000 + (group << 7) + 0x20 + objid
```

Code references: `od::pin_mode_index`, `od::io_operation_index` in
`src/canopen/devices/Ttc2038Xs.h`.

---

## 5. Pin Groups, Pins & Pin Modes

Each I/O pin group has a **Pin Mode** object (`0x3000 + group<<7`,
`UNSIGNED8`, `rw`, one sub-index per pin). The pin's mode determines which
operation objects are valid for it.

### 5.1 Groups, indices, pins (sub-index → terminal)

| Group | Pin Mode index | Pins (sub 1, 2, 3, …) |
|------:|:---------------|:----------------------|
| 0 — ADC 4-mode | `0x3000` | J4, H4, E4, D4, C4, B4, A3, A4 |
| 2 — TIN/SENT | `0x3100` | E3, D3, C3, B3 |
| 3 — TIN/CL | `0x3180` | G4, F4 |
| 4 — DOP HS 4A+CS/LPO | `0x3200` | K2, J2, H2, G2, F2, E2 |
| 5 — PWM HS 4A+CM+FM | `0x3280` | K1, J1, H1, G1, F1, E1, D1, C1 |
| 8 — PWM LS 4A+CM+FM | `0x3400` | A1, B1 |

### 5.2 Pin Mode values (per group — only listed values are accepted)

| Code | Mode | Group 0 | Group 2 | Group 3 | Group 4 | Group 5 | Group 8 |
|---:|---|:-:|:-:|:-:|:-:|:-:|:-:|
| 0 | Not used | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| 1 | ADC Voltage | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| 2 | ADC Current | ✓ |  | ✓ |  |  |  |
| 3 | ADC Ratiometric | ✓ |  |  |  |  | ✓ |
| 4 | ADC Resistance | ✓ |  |  |  |  |  |
| 5 | DIN | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| 6 | DOP |  |  |  | ✓ |  |  |
| 8 | LPO PVG |  |  |  | ✓ |  |  |
| 9 | LPO VOUT |  |  |  | ✓ |  |  |
| 10 | LPO VOUT PID |  |  |  | ✓ |  |  |
| 11 | LPO ESO |  |  |  | ✓ |  |  |
| 12 | PWM |  |  |  |  | ✓ | ✓ |
| 13 | PWM PID |  |  |  |  | ✓ | ✓ |
| 14 | SENT |  | ✓ |  |  |  |  |
| 15 | TIN PWD |  | ✓ | ✓ |  |  |  |
| 16 | TIN PWD CL |  |  | ✓ |  |  |  |
| 17 | TIN CNT |  | ✓ | ✓ |  |  |  |
| 18 | TIN INC |  | ✓ | ✓ |  |  |  |

> Source of the numeric codes: the per-group "Allowed values" lists in
> `resources/Ttc2038Xs.html`. They are **not** in the EDS (which types Pin Mode
> as a bare `UNSIGNED8`).

### 5.3 Writing a pin mode

Write `UNSIGNED8` to `pin_mode_index(group)` at `sub = pin sub-index`, **while in
Pre-Operational**. Example: set pin K2 (group 4, sub 1) to DOP:
SDO download `0x06` (value 6) to index `0x3200`, sub `0x01`.

Code references: pin model in `Ttc2038Xs.cpp` (`pin_groups()`); mode→behaviour in
`mode_behavior()`; device API `read_pin_mode/write_pin_mode` in
`Ttc2038XsDevice.cpp`.

---

## 6. Configuration & Startup Sequence

The device verifies its pin configuration before it will go Operational. Two
paths:

### 6.1 Normal (verified) bring-up

1. Enter **Pre-Operational** (NMT `0x80`).
2. Write all configuration via SDO (pin modes `0x3000…`, limits, etc.).
3. Write `0xA5` to **Configuration Valid** `0x13FE` (sub 0).
   - **Required even if no SRDOs are used.** If skipped, the device silently
     fails to go Operational (no EMCY).
4. Read the actual signature from **Device - Pin Configuration Signature**
   `0x2000`.
5. Read back the whole configuration and verify it.
6. Write that signature to **Device - Safety Pin Configuration Signature**
   `0x2010`.
7. NMT **Start** (`0x01`) → Operational.

If the signature check fails: EMCY with error source *Safety mechanism*
(`0x0009`), scope *Device internal* (`0xFE`), code *Request failed* (`0xF008`).

> **Key point:** the value written to `0x2010` is **not a constant** — it is the
> signature the device *computes* from your pin configuration and exposes at
> `0x2000`. Read it from `0x2000` after writing `0x13FE = 0xA5`, then write that
> exact value back to `0x2010`. (In this tool, steps 3–6 are automated by
> `Ttc2038XsDevice::apply_configuration()` / the Pins tab "Validate & apply
> configuration" button, which does `0x13FE=0xA5` → read `0x2000` → write
> `0x2010`, without changing the NMT state.)

### 6.2 Development mode (skips verification — bench only)

1. Write `0x65` to **Device - Enable Development Mode** `0x2011`.
2. Write `0x706C7664` to **Device - Safety Pin Configuration Signature**
   `0x2010`.
3. NMT **Start** → Operational (pin-config + SRDO verification skipped).

> Development mode must be **off** in production: the device continuously emits a
> warning EMCY (source `0x0009`, scope `0xFE`, code *Warning* `0xF00A`) while it
> is enabled.

### 6.3 Persisting configuration

- **Store:** write the ASCII string `"save"` (`0x73 0x61 0x76 0x65`) to
  **Store Parameters** `0x1010` sub 1.
- **Restore defaults:** write `"load"` (`0x6C 0x6F 0x61 0x64`) to
  **Restore Default Parameters** `0x1011` sub 1.
- A stored configuration is applied after a reset/power cycle.

Code references: `Ttc2038XsDevice::store_parameters/restore_defaults`. Note: the
current tool does **not** implement the `0x13FE`/`0x2000`/`0x2010` verified
sequence (it relies on development mode or pre-stored config) — a production
driver should implement §6.1.

---

## 7. Enabling & Driving Outputs

Outputs require: (a) the pin configured to an output mode, (b) the device in
**Operational**, and (c) for DOP/PWM, the relevant **safety switch** enabled and
the ECU in the **Running** state. There are two addressing styles.

### 7.1 Digital outputs — two equivalent options

**Option A — CiA 401 aggregate (simplest).** Object **`0x6200` sub 1**
(`Write Digital Output 8-Bit`), a bitfield for DO 1..8. Pin mapping (manual,
"Digital Output Mapping"):

| Bit | Pin | | Bit | Pin |
|---:|:--|---|---:|:--|
| 0 | K2 | | 3 | G2 |
| 1 | J2 | | 4 | F2 |
| 2 | H2 | | 5 | E2 |
| 6/7 | (no effect) | | | |

Write `UNSIGNED8` mask to `0x6200` sub 1. Read it back to confirm applied state.
(These pins are the DOP group's pins K2,J2,H2,G2,F2,E2 — i.e. DO bit = DOP-group
pin sub-index − 1.)

**Option B — native per-pin.** **DOP Level** is operation object-id **15**, so
index `group_base + 0x20 + 15` (e.g. DOP HS group 4 → `0x322F`), `sub = pin sub`,
`BOOLEAN` (1 byte 0/1). Use this to drive one pin without touching the aggregate.

> DOP level may be modified at most every **10 ms**; more frequent writes are
> ignored.

### 7.2 PWM outputs

**PWM/LPO Duty Cycle** is operation object-id **9** → `group_base + 0x20 + 9`
(e.g. PWM HS group 5 → `0x32A9`), `sub = pin sub`. Period is config object-id 18
(`0x32 92` region per group). Duty cycle is writable every **1 ms** (PWM) /
**10 ms** (LPO). Encode as the device's duty-cycle unit (16-bit); verify exact
scale/units against `Ttc2038Xs.html` for your group.

### 7.3 Low Power Outputs (LPO)

Same duty-cycle object-id 9 (`group_base + 0x20 + 9`, e.g. group 4 → `0x3229`).
LPO PVG/VOUT/PID modes are **not** for safety-related outputs.

### 7.4 Safety switches (gate for DOP/PWM)

DOP and PWM outputs are physically gated by internal **safety switches**:

- **SSW Mode** `0x2020` sub 1: automatic (enabled on entering Operational after
  startup tests) or manual.
- **SSW Enable** `0x202A` sub 1 (manual mode only; writable every 10 ms).
- **SSW Enable Feedback** `0x202B` sub 1 — confirm the switch actually engaged
  (state is not applied instantly).
- **SSW Status** `0x2028` sub 1 — status codes (see manual; e.g. `0x00000000`
  OK, startup-test/short-circuit codes otherwise).

Manual safety switches drop whenever Operational is left.

### 7.5 Safe state

Write **Device - Request Safe State** `0x2008` sub 0 = `0x01` to force the safe
state (device enters **Stopped**, all safety outputs off). `0x00` clears the
request. This object is honoured regardless of NMT state.

Code references: `set_digital_outputs` (0x6200), `write_pin_io` (native objects),
`request_safe_state` (0x2008) in `Ttc2038XsDevice.cpp`; behaviour map in
`mode_behavior()`.

---

## 8. Reading Inputs & Status

### 8.1 Digital inputs

- **Aggregate:** `0x6000` (`Read Digital Input 8-Bit`), **sub 1..4** = DI 1-8,
  9-16, 17-24, 25-32 (each `UNSIGNED8`). Concatenate for a 32-bit field.
- **Native per-pin:** **DIN Level** operation object-id **1**
  (`group_base + 0x20 + 1`), `sub = pin sub`.
- Update rate: 2 ms.

### 8.2 Analog inputs

- **Aggregate:** `0x6401` (`Read Analog Input 16-Bit`), **sub 1..32** = AI 1..32,
  **INTEGER16** (signed). Reads 0 for pins not configured for ADC. Pin→sub map in
  the manual ("Analog Input Mapping").
- **Native per-pin:** ADC Voltage = op id **2** (mV), ADC Current = op id **3**
  (mA), ADC Resistance = op id **4** (Ohm). Index `group_base + 0x20 + id`,
  `sub = pin sub`. Values may be truncated to 16-bit vs the native ADC range.
- Update rate: 2 ms.

### 8.3 Timer inputs (TIN) and SENT

- TIN modes (PWD/CNT/INC) expose per-mode value objects in the operation area of
  their group; read by `sub = pin sub`. (The tool currently reads a single
  representative value; consult `Ttc2038Xs.html` for the exact per-mode object.)
- **SENT Fast Data** = operation object-id **4** (e.g. `0x3124`), plus SENT
  status/nibble/serial objects. Update rate 10 ms.

### 8.4 Per-pin status & error decode

Each pin group has a **Status** object at operation object-id **0**
(`group_base + 0x20`, e.g. `0x3020`, `0x3120`, `0x3220`, `0x3320`). A 32-bit
status word; `0x00000000` = OK. The low 16 bits follow the TTControl "Error
Status" table (e.g. `0x1009` short-to-ground, `0x100A` open load, `0xF001` pin
disabled). Startup-test errors are reset via the group's *Reset Status* object.

### 8.5 Device-level status (object 0x24xx area & standard objects)

| What | Object | Notes |
|---|---|---|
| Error Register (CiA 301) | `0x1001` | 8 bits: generic/current/voltage/temp/comm/… |
| Predefined Error Field | `0x1003` | sub0 = count, sub1..N = recent EMCY codes (newest first); write 0 to sub0 to clear |
| Identity | `0x1018` | sub1 Vendor, sub2 Product, sub3 Revision, sub4 Serial |
| Device/HW/SW name | `0x1008/0x1009/0x100A` | strings (segmented SDO) |
| Board temperature | `0x2048` | INTEGER16, 0.1 °C |
| **ECU diagnostic state** | `0x2400` | outputs only apply once this reads **Running (4)** |
| Reporting device / status | `0x2401` / `0x2402` | which device raised the active error |
| Core component health | `0x2403` sub1..13 | 0 = OK; non-zero = **fatal** → safe state |
| SMU alarm groups | `0x2404` sub1..14 | non-zero = SMU-triggered safe state (non-recoverable until power cycle) |
| Supply voltages (mV) | `0x22A9` sub1 (BAT+ CPU), `0x22C9` sub1 (BAT+ Power) | |

**ECU state gotcha:** the device only applies received output values and allows
safety-switch control once `0x2400` reaches **Running (4)** (startup tests
complete). Poll it before driving outputs.

Code references: `Ttc2038XsDevice::refresh_status()` shows the exact read set;
decoders `decode_error_register`, `error_status_text`, `ecu_state_text`,
`device_status_name`, `smu_alarm_name` in `Ttc2038XsDevice.cpp`.

---

## 9. SRDO (CANopen Safety) — overview

Safety-related data uses **SRDOs** (EN 50325-5): each SRDO is a **pair of CAN
frames**, a normal frame and its **bit-inverted** twin, on consecutive COB-IDs.

- Communication params: `0x1301`–`0x131E` (up to 22 SRDOs). Mapping params:
  `0x1381`–`0x139E`.
- Per SRDO: *Information Direction* (sub1), *Refresh/Safeguard Cycle Time* (sub2,
  default **50 ms**), *Safety-Related Validation Time* (sub3, default **20 ms**),
  *Transmission Type* (sub4 = `0xFE`), *COB-ID 1* (sub5), *COB-ID 2* (sub6).
- Defaults: SRDO 1 = COB-IDs **257/258** (`0x101`/`0x102`), SRDO 2 = 259/260, …
- A *consumer* validates that frame 2 is the bitwise complement of frame 1 over
  its DLC, within the validation time.

If an object is mapped to an SRDO it becomes safety-related and **cannot** be
written by SDO/RPDO (write attempt → SDO abort); mapping the same object to both
an SRDO and an RPDO prevents going Operational (no EMCY).

Code references: `check_srdo()` in `Ttc2038Xs.cpp`; SRDO COB-ID constants in
`Ttc2038Xs.h`. (Full SRDO transport is **not** implemented in this tool — only
pair validation of captured frames.)

---

## 10. Emergency (EMCY) Messages

COB-ID `0x080 + node`, 8 bytes:

| Bytes | Field |
|---|---|
| 0–1 | CANopen error code (little-endian) |
| 2 | Error register snapshot |
| 3–4 | TTControl error **source** |
| 5 | TTControl error **scope** |
| 6–7 | TTControl error **status/code** |

**Scope** selects how to read source/status:

- `0xFE`/`0xFD` → device-internal source (e.g. `0x0009` Safety mechanism,
  `0x000A` CAN bus, `0x0002` Board temperature).
- `0xFC` → SRDO error; source = SRDO number.
- `0x01`/`0x03`/`0x05`/… → pin-mode scope; source encodes pin group + index.

Common status codes (bytes 6–7): `0xF008` Request failed, `0xF002` Init failed,
`0xF009` Safe state entered, `0xF00A` Warning, plus the per-pin fault codes in
§8.4. Full tables in `Ttc2038XsUserManual.html` ("Error Sources/Scopes/Status").

---

## 11. Timing the driver must respect

| Concern | Value |
|---|---|
| Device internal cycle | 1 ms (fixed) |
| DIN level / ADC update | 2 ms |
| Input/output status objects | 10 ms |
| DOP level write cadence | ≤ every 10 ms (faster ignored) |
| PWM duty write cadence | ≤ every 1 ms |
| LPO duty / SSW enable write cadence | ≤ every 10 ms |
| SRDO refresh / validation (default) | 50 ms / 20 ms |
| Core failure-reaction time | `frtCore` = 60 ms; app jitter `tJ` = 100 ms |
| Max driving cycle | 24 h (some diagnostics only run at startup) |

A polling driver doing per-pin SDO reads should budget for **one SDO round-trip
per object** and serialize them (one outstanding SDO per node). For many
channels prefer the **aggregate** objects (`0x6000`/`0x6401`/`0x6200`) or PDOs
over per-pin SDO.

---

## 12. Minimal Driver Bring-up Checklist

1. Open the bus at **500 kbit/s**, 11-bit IDs. Know the **node-ID** (default 10).
2. (Optional) NMT **Reset Node**; wait for boot-up (`0x700+node` = `0x00`).
3. NMT **Enter Pre-Operational** (`0x80`).
4. Read identity (`0x1018`, `0x1008`) to confirm the device.
5. **Configure pins:** write Pin Mode (`0x3000+group<<7`, sub=pin) for each used
   pin (§5).
6. Apply config: write `0xA5` to `0x13FE`; read `0x2000`; write it to `0x2010`
   (or use development mode §6.2). Optionally `"save"` to `0x1010` sub1.
7. NMT **Start** (`0x01`). Poll **ECU state** `0x2400` until **Running (4)**.
8. Ensure safety switches enabled (`0x2020`/`0x202A`, confirm `0x202B`).
9. **Drive outputs**: `0x6200` mask (digital) or native duty objects (PWM/LPO).
10. **Read inputs**: `0x6000` (digital), `0x6401` (analog), per-pin status
    `group_base+0x20`.
11. Monitor **heartbeat** (`0x700+node`) and **EMCY** (`0x080+node`); on fault,
    decode per §10 and react (write `0x2008` to force safe state if needed).

---

## 13. Quick Object Reference

| Object | Index | Sub | Type | Access | Purpose |
|---|---|---|---|---|---|
| Device Type | `0x1000` | 0 | U32 | ro | identification |
| Error Register | `0x1001` | 0 | U8 | ro | CiA 301 error bits |
| Predefined Error Field | `0x1003` | 0..N | U8/U32 | rw/ro | error history |
| SYNC COB-ID | `0x1005` | 0 | U32 | rw | |
| Device/HW/SW name | `0x1008/9/A` | 0 | str | const/ro | |
| Store Parameters | `0x1010` | 1 | U32 | rw | `"save"` |
| Restore Defaults | `0x1011` | 1 | U32 | rw | `"load"` |
| EMCY COB-ID | `0x1014` | 0 | U32 | rw | |
| Consumer Heartbeat | `0x1016` | 1..5 | U32 | rw | |
| Producer Heartbeat | `0x1017` | 0 | U16 | rw | ms |
| Identity | `0x1018` | 1..4 | U32 | ro | vendor/product/rev/serial |
| SDO Server | `0x1200` | 1..2 | U32 | ro | COB-IDs |
| SRDO comm params | `0x1301`+ | 0..6 | mixed | rw | safety (see §9) |
| Configuration Valid | `0x13FE` | 0 | U8 | rw | write `0xA5` to validate |
| Pin Config Signature | `0x2000` | — | U32 | ro | read during apply |
| Enable Development Mode | `0x2011` | — | U8 | rw | `0x65` |
| Safety Pin Config Signature | `0x2010` | — | U32 | rw | write read-back sig |
| Request Safe State | `0x2008` | 0 | U8 | rw | `0x01` = safe state |
| SSW Mode / Enable / Status / Feedback | `0x2020/202A/2028/202B` | 1 | mixed | rw/ro | safety switch |
| Board Temperature | `0x2048` | — | I16 | ro | 0.1 °C |
| ECU diagnostic state | `0x2400` | — | U8 | ro | Running = 4 |
| Core component health | `0x2403` | 1..13 | U32 | ro | fatal faults |
| SMU alarms | `0x2404` | 1..14 | U32 | ro | non-recoverable safe state |
| Supply BAT+ CPU / Power | `0x22A9/0x22C9` | 1 | U16 | ro | mV |
| **Pin Mode** | `0x3000 + group<<7` | pin | U8 | rw | §5 |
| **`<group>` Status** | `group_base + 0x20` | pin | U32 | ro | §8.4 |
| **DIN Level** | `group_base + 0x21` | pin | — | ro | digital input |
| **ADC Voltage/Current/Resistance** | `group_base + 0x22/0x23/0x24` | pin | I16/U32 | ro | analog input |
| **DOP Level** | `group_base + 0x2F` | pin | bool | rw | digital output |
| **PWM/LPO Duty Cycle** | `group_base + 0x29` | pin | U16 | rw | output duty |
| Read Digital Input (aggregate) | `0x6000` | 1..4 | U8 | ro | DI 1-32 |
| Write Digital Output (aggregate) | `0x6200` | 1 | U8 | rw | DO 1-8 |
| Read Analog Input (aggregate) | `0x6401` | 1..32 | I16 | ro | AI 1-32 |

> `group_base = 0x3000 + (io_pin_group << 7)`. Verify exact per-mode object-ids
> and value scaling for your specific group/mode in `resources/Ttc2038Xs.html`
> before relying on writes; the offsets above match this tool's implementation
> and the user manual's operation-object lists.

---

## 14. Implementation Cross-Reference (this repo)

A working reference implementation of everything above lives in:

- `src/canopen/CanOpenDefs.h` — COB-IDs, NMT, SDO command specifiers.
- `src/canopen/CanOpenClient.{h,cpp}` — SDO client state machine, NMT, traffic.
- `src/canopen/PcanBackend.{h,cpp}` — CAN transport (PEAK PCAN).
- `src/canopen/devices/Ttc2038Xs.{h,cpp}` — OD constants, pin model
  (`pin_groups`), I/O function model (`mode_behavior`), SRDO check.
- `src/canopen/devices/Ttc2038XsDevice.{h,cpp}` — typed device API: identity,
  status refresh set, pin-mode read/write, pin-I/O read/write, control commands.

These are the closest thing to a tested driver for this device and should be the
first stop when verifying any detail in this guide.
