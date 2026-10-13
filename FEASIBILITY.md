# Sixty-Four port feasibility

Assessment: 2026-10-12. Target: Apple Silicon M2 MacBook Pro, Ableton Live 12 and rekordbox 7. Exact macOS version, application point releases, mixer firmware and USB descriptors remain unknown.

## Decision

**GO for a bounded discovery/prototype project; NO-GO for deploying the current driver on a Sixty-Four.** Audio and MIDI objectives are plausible, but no Sixty-Four hardware was tested. This is not a VID/PID rename. The existing program can claim unintended USB devices and sends model-specific initialization messages.

| Objective | Assessment | Release gate |
| --- | --- | --- |
| Native CoreAudio, 12 recording inputs / 10 playback outputs | Promising host architecture; actual Sixty-Four transport unverified | Exact descriptors, wire format, startup sequence and clock behavior established; full-duplex capture validated |
| Individual post-fader decks plus master in Live 12 | Hardware routing supports the proposed workflow; see comparison | Five simultaneous stereo recordings with correct source isolation, stable timing and verified fader response |
| CoreMIDI controls in rekordbox 7 | Plausible after transport discovery | Native arm64 source/destination visible; actual mapping works outside Learn mode |
| LED feedback | Conditional extension | Verified writable controls, correct state/velocity translation, no echo loops |
| Current Serato native mixer/DVS integration on M2 | No supported path established; exclude from base delivery | Separate vendor-supported compatibility or independently demonstrated legitimate integration |

## Evidence and scope

The fork's origin is `fongcj/rane-sixty-eight-driver`; its README identifies [cmdl77/rane-sixty-eight-driver](https://github.com/cmdl77/rane-sixty-eight-driver) as upstream. Local HEAD and the public upstream `main` API both resolved to `d4b190fbf67c6801cf4ce991d6a69128a39cc7c9`. A separately downloaded upstream archive had identical `src/` and README contents. Source findings below refer to that immutable revision, not marketing claims or a tested device.

Sources read: upstream README, architecture guide, build configuration, HAL, transport, engine, MIDI agent, manager MIDI service, packet/codec/ring code, tests and diagnostic tools; the [official Rane manual](https://www.ranecommercial.com/legacy/pdf/sixtyfour_manual_dj.pdf); and application vendor documentation linked below. The manual's page 23 routing table was also rendered and visually checked. No public Sixty-Four USB descriptor dump or complete low-level initialization specification was established from these materials. A user manual's MIDI tables do not establish USB encapsulation.

The detailed evidence register is in [HARDWARE_COMPARISON.md](HARDWARE_COMPARISON.md). The read-only collection procedure and approval boundaries are in [PORTING_PLAN.md](PORTING_PLAN.md).

## Audio and M2 feasibility

The reusable foundation is the AudioServerPlugIn/CoreAudio interface, C++ engine separation, atomic frame buffers and arm64 build target. `build.sh` requests Universal 2 binaries; that establishes build intent, not validated operation on the user's OS. No build, installation, daemon launch or hardware streaming was performed in this investigation.

The current HAL advertises Float32, 12 inputs, 10 outputs and 48 kHz. Its transport actually copies float samples directly. The S24 helpers and tests are a different path and cannot validate live USB audio. Before deciding whether to reuse the USB transport, determine whether macOS already binds a class driver and whether the device describes usable standard audio interfaces. Prefer an existing working class driver if evidence supports it; otherwise develop an explicit Sixty-Four transport profile. Neither class compliance nor proprietary transport is assumed.

The largest engineering risks are initialization, clock synchronization, reliable loss accounting, USB ownership and lifecycle handling. CPU architecture alone is not the main uncertainty. A user-space design is the intended path, but permissions, signing and actual IOUSBLib behavior must be validated on the exact macOS release with normal security intact.

After approved deployment, Live can enable interface channels in Audio Settings and select stereo pairs using Ext. In. Build a five-track recording set using the comparison's deck/master map, initially with monitoring Off to avoid unintended returns. Test all ten playback channels separately. Capture actual hardware master audio rather than substituting Live's software mix. These are future steps, not settings applied here. [Live 12 routing documentation](https://www.ableton.com/en/live-manual/12/routing-and-i-o/)

## MIDI and rekordbox

Separate the USB/HID decoder from MIDI message parsing and CoreMIDI publication. Reuse the virtual source/destination concept, but give each verified device/port stable distinct identities. The existing Sixty-Eight matcher, HID envelope, startup writes and echo logic require replacement or hardware-backed verification.

rekordbox's MIDI Learn guide describes Performance-mode assignments, rotary controls, MIDI OUT and indicator messages. This supports a mapping experiment, not a guarantee that this agent's virtual endpoints will appear as eligible hardware. Test discovery first, then hot cues, loop buttons/encoders, browse/load, deck switching, banks and releases. Third-party configuration is outside AlphaTheta's stated support. Export a tested mapping; do not promise every native controller feature. [Official MIDI Learn guide](https://cdn.rekordbox.com/files/20241203210623/rekordbox7.0.5_midi_learn_operation_guide_EN.pdf)

Confirm MIDI Learn entitlement on the user's account before mapping acceptance tests. Core or higher is a documented subscription route; do not assume a custom Sixty-Four endpoint grants Hardware Unlock. [AlphaTheta MIDI Learn guidance](https://support.alphatheta.com/en-us/articles/37281806232345)

For LEDs, first prove one permitted lamp, then map software state to the device's verified output values. A button-press echo is not authoritative hot-cue or loop state. Rate-limit redundant lamp updates without dropping note releases; never send broad output sweeps across mixer controls.

## Serato: four distinct questions

1. **Standard MIDI:** A functioning CoreMIDI endpoint may serve as an auxiliary controller in an otherwise valid Serato setup. Test the application's MIDI-device list and assignable controls; recognition alone is not native mixer support. [Serato MIDI mapping](https://support.serato.com/hc/en-us/articles/209377487-MIDI-mapping-with-Serato-DJ-Pro)
2. **Hardware authorization:** The original Sixty-Four unlocked DJ Pro and DVS in its supported configuration. A new endpoint name or generic CoreAudio driver does not establish that authorization. [Serato hardware page](https://serato.com/dj/hardware/rane-sixty-four)
3. **DVS:** Requires supported software/hardware authorization, correct timecode audio routing and successful calibration. MIDI notes are not timecode. The post-fader recording workflow and timecode source selection need separate tests; do not promise simultaneous DVS and isolated post-fader capture on the same deck channels.
4. **Proprietary native integration:** Device discovery, any private driver interface/handshake, application state exchange and routing behavior remain unknown. The repository's purported audio-unmute messages are not evidence of Serato licensing or native integration.

Serato lists DJ Pro 2.5.5 and macOS 10.14 Mojave as the last supported Sixty-Four Mac configuration. That is not an M2-supported configuration; installing a newer audio driver does not change Serato's support matrix. Treat current Serato native/DVS support as a separate research project with a stop condition if no supported route exists. No authorization spoofing, firmware changes or security reduction is proposed. [Official discontinued-hardware compatibility](https://support.serato.com/hc/en-us/articles/4411117032975-Serato-software-operating-system-compatibility-for-discontinued-hardware)

## What would change the decision?

Proceed to implementation when a real Sixty-Four descriptor/firmware record exists, a justified transport/initialization path can be specified, and normal-security user-space access is achievable. Stop or narrow scope if obtaining audio requires an unidentified write sequence, if the required taps cannot reach the chosen port, or if reliable hardware-clock tracking cannot be demonstrated. Audio-only or MIDI-only milestones are valid partial products but must not be presented as completion of both primary objectives.


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
