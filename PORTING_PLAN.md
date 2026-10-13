# Sixty-Four porting and validation plan

2026-10-12. This plan follows [FEASIBILITY.md](FEASIBILITY.md) and the source audit in [HARDWARE_COMPARISON.md](HARDWARE_COMPARISON.md). Scope of this delivery is investigation and documents. No driver installation/loading, firmware operation, security change, hardware command or system audio setting change has been performed.

## 1. Read-only evidence collection

First record the user's exact macOS build, Live/rekordbox point releases, mixer firmware if already displayed, USB cable/adapter/hub model and which mixer USB port is connected. Do not infer firmware from bcdDevice. Record current routing without changing it. Connecting/reconnecting equipment is a separately coordinated step because it can affect an active session.

Proposed commands for an already-connected mixer, without sudo:

```sh
sw_vers
uname -m
system_profiler SPUSBDataType -json > usb-inventory.json
ioreg -p IOUSB -l -w 0 > usb-registry.txt
ioreg -r -c IOHIDDevice -l -w 0 > hid-registry.txt
```

These read OS-maintained inventory; they do not deliberately seize devices, change configurations, select alternates or send vendor commands. Save outputs to a user-chosen evidence directory. They can contain serial numbers and unrelated peripherals: retain raw files locally and redact identifiers before sharing. Their absence of an interface property is not proof that the interface does not exist. Avoid opening audio applications if that might start an already-installed driver.

Collect port A and port B inventories separately during an agreed idle period. Record device/configuration descriptors, VID/PID, bcdUSB/bcdDevice, device speed, interface number/class/subclass/protocol, every alternate, endpoint direction/type, wMaxPacketSize, bInterval, audio format/rate/channel descriptors, synchronization/usage fields and HID report descriptor/lengths. Registry inventory alone may not contain all of this.

For missing descriptors, build a **separate non-opening descriptor reader**, not a transport instance. Use cached configuration descriptors where available, exact user-selected service matching and strict bounds checks. Parse all alternates from descriptor bytes without selecting them. The existing `tools/debug/dump_usb.c` illustrates cached access but has a Sixty-Eight-only matcher and insufficient error/bounds checks; do not run it unchanged. A read-only reader must have no dependency on transport startup, no device/interface open/seize, no SET_CONFIGURATION/SET_INTERFACE, no HID output/feature reports, and no arbitrary request interface. If an OS API requires device access or an actual request to retrieve a missing descriptor, stop and specify that bounded operation separately.

If an existing standard CoreMIDI source is already present, a dedicated input-only subscriber can log timestamps and received bytes without sending setup, polling, control-dump or LED messages. An input logger should not create a MIDI destination or automatically initialize hardware. Physical control exercises and deck/bank switches should occur only in an agreed idle session. If no source exists, report that result; do not start the current RaneMIDIAgent to “check.”

Do not use `rane-selftest` (opens/configures/streams), `check_pipes` (sets configurations/alternates), `rane-stresstest` (plays tones, changes buffers and sends MIDI), the manager (control requests), or installer/cleanup scripts for read-only discovery.

**Gate A:** target inventory identified unambiguously, descriptor gaps enumerated, existing OS bindings understood, no protocol inferred from the product name or sibling device.

## 2. Establish protocol evidence

Create a versioned device profile and evidence manifest: source URLs/date, firmware, port, descriptor hashes, OS, capture method, request sequence and trace timestamps. Keep unknown fields unset. Test standard class-driver availability before committing to a custom audio path.

If passive evidence is insufficient, propose capture on an already-working legacy setup with the legitimate driver, or an external USB analyzer. This is a later approved experiment: a capture tool may be passive, but starting the official application/driver actively initializes the mixer. Do not install legacy drivers on the M2 or change its security to obtain a trace.

Capture enumeration, idle state, driver start/stop, one control at a time, and approved routing changes as separate experiments. Distinguish endpoint-zero control transfers, HID reports, bulk/interrupt packets and isochronous audio. Compare A/B ports and cold/warm starts. Identify every required write and its response before replay. Exclude firmware-transfer/update sequences.

Prove sample representation with known, low-level signals and byte-level checks: silence alone cannot distinguish Float32 from integers. Establish channel stride/order, packet length distribution, feedback mechanism and sample-clock behavior. For MIDI, correlate raw reports with notes/CCs and documented controls; characterize releases, encoder direction/acceleration, channels, banks, layers and message fragmentation. Treat any unexplained duplicated events as evidence to investigate rather than automatically suppress.

**Gate B:** minimal documented startup/stop sequence, validated audio decoder and timing model, and validated MIDI envelope. If unexplained commands remain necessary, stop at offline analysis rather than brute-force writes.

## 3. Refactor and test without hardware

- Exact allowlist plus descriptor fingerprint validation; remove permissive lookup fallbacks. Separate Sixty-Eight and Sixty-Four profiles, including identity, channel labels, transport and initialization.
- One clear owner per hardware interface. Prefer MIDI independent of audio streaming. Use a host service only if needed; do not introduce duplicated HID reads or mixed ownership casually.
- Fail closed on profile/endpoint mismatch. Check all API results, required directions and configuration transitions; prevent partial startup from being reported as success.
- Implement bounded timestamped MIDI queues and a persistent parser. Fragment output by actual report capacity; support partial reads/writes, running status, SysEx across packets, real-time interleaving, backpressure and cancellation. Restrict LED writes to evidenced controls.
- Repair ring ownership, count every under/overrun and discarded frame, validate packet length/alignment and reject invalid float payloads safely. Replace silent sample pruning as the normal drift solution with a measured clock model.
- Use distinct device/port UIDs, names, preferences and IPC. Remove world-writable unauthenticated command ingress. Preserve existing Sixty-Eight behavior behind its own profile.

Offline tests: positive/negative device matching; malformed descriptor parsing; golden captured audio/MIDI fixtures; packet fragmentation/coalescing; long SysEx; real-time interleaving; queue overflow; concurrent ring wraparound; simulated clock drift; callback cancellation/hotplug. Keep assertions enabled: the existing Release build's assert-based tests alone are weak evidence. Run sanitizers where applicable without loading the HAL or starting USB transport.

Build arm64 artifacts into a workspace staging directory and inspect architecture/link dependencies and signatures. Do not invoke install scripts or launch produced binaries with hardware access. A successful build is not hardware validation.

**Gate C:** offline tests pass, hardware writes are enumerated and reviewable, build artifacts are native arm64, and installation/rollback instructions require no security weakening.

## 4. Approved hardware prototype

Before this phase, obtain explicit approval for the concrete installation/loading procedure and bounded hardware operations. Supply exact artifact paths, hashes, target install paths, services to start/stop, expected commands and rollback. Security reduction and firmware flashing are not fallback steps. If normal-security access fails, report the actual error and reassess.

Start with the smallest justified experiment. Audio capture may still need configuration/start writes, and this repository uses input feedback to drive output; “input-only” must not be advertised as passive. Begin with muted/isolated physical outputs and zero-valued playback where required by the verified protocol. Use low-level, one-channel-at-a-time playback tests only after approval. Never replay the Sixty-Eight handshake merely because discovery succeeded.

Validate return codes, byte counts, sample counters and source routing against expected results. Then validate CoreMIDI input with audio stopped, followed by one permitted LED and bidirectional operation. Record every deviation, not just audible symptoms.

**Gate D:** reproducible full-duplex audio and input MIDI on the actual M2/OS, no unexpected routing/LED changes, no persistent changes, clean disconnect and stop.

## 5. Application acceptance

These tests involve application/device configuration and require the user's approval before execution.

| Test | Proposed pass criterion |
| --- | --- |
| HAL enumeration | Exactly 12 inputs / 10 outputs at the evidenced rate, correct names and stereo pairing; no phantom devices |
| Live deck/master recording | Five stereo tracks captured together using the comparison map; each deck isolated; master verified independently; fader, crossfader, EQ and FX tap behavior recorded |
| Playback | All five output pairs identified independently, correct L/R and intended hardware destination; no crosstalk or channel swaps |
| Clock integrity | At least 60 minutes full duplex at a conservative buffer, no unexplained sample losses/duplications or ring drift; measured timestamps and latency, not callback-gap statistics alone |
| Buffer choices | Start at 512, then qualify 256/128 only if stable; record actual round-trip/input timing and realistic CPU load |
| MIDI | Repeat every requested control across decks/banks/layers; correct press/release and encoder direction; no missing, duplicated or stuck events during simultaneous audio |
| rekordbox | Endpoint eligible in MIDI Learn; mapping works after Learn closes and after restart; entitlement documented; tested mapping exported |
| LEDs | Actual application state reflected for supported controls; velocity/color handling verified; no feedback loop or suppression of genuine input |
| Lifecycle | At least ten start/stop and reconnect cycles; wake recovery in an idle test; no crash, stale endpoint, unexpected loud output or unreleased handle |
| Concurrent apps | Live recording while rekordbox controls/playback run, if desired; no exclusive-ownership conflict or MIDI double mapping |

Label latency/dropout findings with the exact OS, firmware, cable, port, buffer and app versions. If a test fails, limit the supported matrix rather than claiming universal M1-M4 compatibility. Separately validate port B before advertising it. Dual-computer routing is an additional milestone, not assumed from a single-port test.

## 6. Serato investigation and release decision

Follow the four-way distinction in FEASIBILITY.md. First test auxiliary MIDI only in an already legitimate, supported setup. Separately ask whether the intended Serato version recognizes the hardware as primary, selects the audio interface, authorizes features, calibrates DVS and exchanges native state. Passing one does not imply the others. No private protocol or authorization behavior is established by this audit.

Release the CoreAudio/CoreMIDI port only after the audio and mapping acceptance gates pass. Publish LED support as a control-by-control matrix. Mark current Serato native/DVS support unsupported unless independently demonstrated through a legitimate compatible path. If Serato native support is mandatory for the whole project, the present decision is NO-GO until that separate compatibility gap is resolved.

Deliverables after investigation: evidence bundle, reviewed profile, offline test fixtures, staged native artifacts, approved installation/rollback procedure, measured acceptance report, Live recording template and exported rekordbox mapping. The next useful action is the read-only inventory in phase 1, not running the existing driver.


## Open-source comparison follow-up — 2026-10-12

See [Open-source driver comparison](docs/OPEN_SOURCE_DRIVER_COMPARISON.md).
`snd-xonedb4` now redirects to Ozzy. Its current and legacy sources offer
useful lifecycle, timing and buffering patterns, but no verified Sixty-Four
protocol. Retain the normal-security requirement and model-specific startup
evidence gate; do not adopt its Ploytec handshake or current kext backend.


## Recording hardware checkpoint — 2026-10-12

Bounded recording-only probes now demonstrate USB input data and Deck 1
pre/post-fader selection on the connected Sixty-Four. With REC DECK1 POST,
USB 3/4 carried signal with the fader up and became digital zero after the
first batch with the fader down; master USB 11/12 fell to the noise floor.
A first-batch transient is retained and documented. These gapped probes
do not validate continuous recording, other decks, playback, CoreAudio/Live
integration or MIDI. No driver installation was performed. See
[recording evidence](docs/hardware/sixty-four-usb-a/AUDIO_PROBE_FINDINGS.md).
