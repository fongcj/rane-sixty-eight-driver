# Sixty-Eight implementation versus Sixty-Four hardware

Assessment: 2026-10-12. Evidence labels: **D** = official documentation; **S** = inspected source, not measured hardware; **U** = unknown on the target. Source baseline: upstream/local commit `d4b190fbf67c6801cf4ce991d6a69128a39cc7c9`. The Sixty-Eight column describes this repository's implementation, not a claim that every Sixty-Eight firmware behaves identically.

## Documented Sixty-Four facts (D)

[Official manual](https://www.ranecommercial.com/legacy/pdf/sixtyfour_manual_dj.pdf), printed pp. 22-23, 25, 29-39:

- Each USB port: 12 record / 10 playback channels, 48 kHz Float32.
- Deck record source: PH/CD before selection, or post-fader. DVS needs PH/CD.
- Deck audio follows A/B source selection; master recording reaches both ports. Master pair can instead carry Mic 1 or Mic 2.
- MIDI routing follows deck assignment; non-deck routing is separately selected. Channel/layer state matters.
- Dedicated mixer controls transmit but reject MIDI control/LED writes; software controls permit LED feedback.
- LED note matches button note. Velocities: 0-30 off, 31-47 primary dim, 48-78 primary full, 79-95 secondary dim, 96-127 secondary full.

| USB pair | Record to host | Playback from host |
| --- | --- | --- |
| 1-2 | Deck 3 | Deck 3 |
| 3-4 | Deck 1 | Deck 1 |
| 5-6 | Deck 2 | Deck 2 |
| 7-8 | Deck 4 | Deck 4 |
| 9-10 | FlexFX send | FlexFX return |
| 11-12 | Master/Mic selection | None |

This is logical channel order, not proof of packet layout. Exact post-fader tap position relative to crossfader, processing and master gain needs measurement. Do not equate isolated decks with separate copies of all shared effects.

## USB and audio audit

| Area | Sixty-Eight repository evidence (S) | Sixty-Four status / consequence |
| --- | --- | --- |
| Identification | `src/protocol/rane_defs.h`: VID candidates 0x1CC5, 0x13E5, 0x29E2; Sixty-Eight PID candidates 0x0004, 0x0008, 0x0080, 0x000E | U: actual port A/B VID, PID, bcdDevice, strings and serial. Sixty-Four 0x0014 is a source-table claim only |
| Matching | `rane_lookup_device` accepts any VID for many entries because `kDevices[i].vid == 0x1CC5` is an OR clause; PID 4/8 also bypass exact matching | Must use exact observed tuples and descriptor validation; unrelated vendors with matching PIDs can currently be selected |
| Device metadata | Sixty-Four table entry says 12-in / 12-out; active HAL and transport use fixed 12/10 | Metadata is internally inconsistent. Adding a table entry is not device support |
| Configuration | `openDevice` seizes first, falls back to ordinary open, sets configuration 1; `openInterfaces` opens broadly and sets interfaces 1/2 to alt 1 | U: target configuration, interface classes/subclasses/protocols, alternates and ownership. Do not execute these guesses |
| Endpoints | PCM OUT 0x06, IN 0x82; nominal MIDI IN 0x81, OUT 0x01; pipe selection matches address without requiring expected transfer type | U: every endpoint, type, interval, sync/usage bits, feedback association, packet capacity and HID report descriptor |
| Startup validation | Interface-open/alternate failures logged but not consistently fatal; `claimPipes` only requires OUT, and caller ignores result | Fail closed if either required audio direction or profile validation is missing |
| Active audio representation | Transport header and `submitOut` / `onInComplete`: interleaved native float, 4-byte samples, 40-byte OUT frames, 48-byte IN frames | D supports Float32 API intent; U: endian, actual encoding, interleave, padding/headers and packet cadence |
| Competing codec path | `rane_codec.c` / `rane_packet.c` use packed S24LE, 30/36-byte frames, nominal 48-frame packets; not called by active streaming path | Do not port these helpers as the target audio codec without evidence; README's 16/16 and architecture's S24 descriptions are stale relative to source |
| Transfer scheduling | Eight transfer slots, eight microframes each; nominal six sample frames/microframe, accepted feedback counts 5-7; default endpoint maxima 280/336 bytes | U: bus speed, service interval and variable packet pattern. Matching total channel counts proves none of these constants |
| Clock control | Input sample counts feed output scheduling as implicit feedback; no verified target feedback descriptor | U: implicit versus explicit feedback, clock source and rate requests. Inspect descriptors and captures before copying algorithm |
| Sample-rate setup | `setSampleRate` changes host variables/tick conversion only. HAL advertises 48 kHz, but configuration-change handler accepts other rates | Enforce profile rates consistently; host variable changes do not program hardware |
| Identity / multiple ports | Singleton transport and fixed Sixty-Eight UID, names, preferences, shared-meter and socket paths | Introduce per-device/port identity; do not conflate two ports or two mixers |

Code references: [definitions](src/protocol/rane_defs.h), [transport](src/transport/IOUSBLibTransport.cpp), [transport constants](src/transport/IOUSBLibTransport.h), [HAL](src/hal/RaneHALPlugin.cpp).

## Initialization and control messages

The running [MIDI agent](src/midi-agent/main.cpp), `onDeviceMatched`, automatically writes a 64-byte HID initialization report with opcode 0x36, then 0x41 envelopes containing SysEx `F0 17 04 00 F7` and `F0 17 52 07 01 F7`. Comments call these initial-dump/unmute operations, but the comments do not prove their effects on the Sixty-Four. Return values for these initialization writes are not used to prove successful activation.

The transport also defines `sendHardwareHandshake` using a different 0x04 prefix and slightly different payloads. No call site for that helper was found. Distinguish this unused helper from the agent's active startup path. `controlRequest` is a generic device-request facility, not a discovered Sixty-Four protocol specification.

The manager's `RaneMidiService.swift` has separate Sixty-Eight HID matching and automatically requests a control dump. Its controls, CC assignments, LED clearing behavior and presets must be audited against target evidence. Do not launch the existing manager as a passive monitor.

U: whether initialization is required at all; exact request direction/type/value/index/data; response/acknowledgment and sequence rules; routing readback; persistent versus volatile effects; reset/wake behavior. A request called “get controls” is still an outbound command and is outside passive diagnostics.

## MIDI transport and buffering audit

- Agent matches only VID 0x1CC5 with PID 0x0004/0x0008, not its own repository's Sixty-Four candidate. It opens HID and creates virtual CoreMIDI endpoints. U: target uses USB MIDI event packets, HID, vendor transport, or a combination.
- Agent input accepts 0x40/0x41 envelopes with length at byte 5, data at byte 6, or a 0xFF prefix. Output uses 0x41, a four-byte sequence and at most 58 payload bytes in a 64-byte report. Longer CoreMIDI packets are truncated, not fragmented.
- Parser state is local to each received buffer. Split SysEx/messages and running status are not retained; interleaved real-time bytes need proper handling. `send_midi` publishes timestamp zero, losing capture timing. Preserve boundaries and timestamps in the port.
- Echo suppression is a 128-entry, nominal 1.5-second queue. It matches before expiring entries, can suppress a real matching button event, and ignores real-time messages when registering echoes. Prove actual echo behavior before enabling it.
- HAL-side `MidiBridge` passes bytes over a blocking Unix stream socket, does not handle short sends, ignores partial ring writes, and loses message boundaries. The agent's socket is mode 0777. Replace with authenticated, bounded IPC and explicit framing if IPC remains needed.
- HAL also schedules four 512-byte reads on the guessed MIDI IN pipe while audio runs. Its MIDI OUT ring has no discovered `getMidiOutByte` consumer, and the guessed OUT pipe is not a completed send path. Active outbound hardware MIDI is the agent's HID path. Avoid duplicate ownership/input publication between paths.
- `onMidiInComplete` only resubmits on success with nonzero bytes; a zero-byte completion or error can permanently lose a receive slot. Add bounded recovery and disconnect handling.

## Audio buffering audit

[Transport](src/transport/IOUSBLibTransport.cpp) and [ring](src/engine/RaneRing.h): buffers request 8192 frames in bytes, then round capacity to a power of two. Output pre-roll is two host periods, clamped to 256-2048 frames. Excess buffered samples are discarded; underruns inject zeros and restart pre-roll. Input ring overflow is not counted.

`writeOutput` can pop from the output ring on overflow while the USB worker also pops. This violates the ring's single-consumer assumption; atomic indices alone do not make two consumers safe. Repair before relying on recordings or playback. Resets must occur only after all users and callbacks are quiescent.

`getZeroTimeStamp` derives sample time from the host clock at nominal rate rather than measured device sample progression. Input/output callbacks ignore their supplied sample times. Implicit packet sizing does not itself solve HAL clock drift. Clock tracking, discontinuity seeds and latency reporting need hardware measurements, especially with two simultaneous applications.

Current unit tests cover helper codecs and basic ring operations, not actual Float32 USB decoding, concurrency, MIDI framing or long-run clock stability. The stress tool measures callback gaps and sends audio/MIDI; its success banner is not proof of lossless capture. No test execution was needed for this documentation-only audit.

## Reuse decision

| Component | Expected reuse |
| --- | --- |
| HAL property/stream plumbing, arm64 build scaffolding | Reuse after identity, error propagation, rate and latency corrections |
| Engine/transport abstraction | Reuse; add immutable per-model profiles and a no-write discovery path |
| Frame ring | Reuse after ownership/concurrency repair and real overflow accounting |
| CoreMIDI endpoint publication | Reuse concept; redesign parsing, queuing and device lifecycle |
| USB scheduler / feedback | Conditional on target descriptors and captured timing |
| S24 codec, HID envelopes, handshakes, manager CC maps | Not assumed reusable |
| Installer, cleanup scripts, selftest, stress test | Not discovery tools; separate approval and audit required |


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
