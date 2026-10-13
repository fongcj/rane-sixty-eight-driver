# Open-source driver comparison

Inspected 2026-10-12. Ozzy revision `f691999b3b6099c4a6c22b7c4fdc53427d2d552c` (commit date 2026-10-12).
Source downloaded to `/tmp/rane-ozzy-review` for static inspection only.
No external builds, installers, drivers, or hardware commands were run.

## Repository identity and scope

The requested [snd-xonedb4](https://github.com/mischa85/snd-xonedb4)
URL redirects to [Ozzy](https://github.com/mischa85/Ozzy). They are not two
independent current implementations. Ozzy retains userspace HAL/daemon and
DriverKit code under `legacy/`; its current macOS implementation is organized
around a kext backend. This review compares that current source and selected
legacy paths with the existing Sixty-Eight source, rather than attributing a
separate validated userspace driver to the old repository name.

The README recommends a kext and describes a security configuration outside
this project's constraints. No such procedure is proposed here. Legacy code
is reference material, not proof of production readiness on this M2.
Source-wide searches for Rane, Sixty, 1cc5 and InitHid found no matching
implementation in the checked-out source. Neither this search nor Ozzy's
success with other mixers establishes a Sixty-Four initialization sequence.

## Protocol comparison

| Area | Ozzy source examined | Sixty-Four evidence / consequence |
|---|---|---|
| Audio wire format | Ploytec packed 24-bit junction frames; model-specific channel topology | Observed descriptors advertise 32-bit Float32, 12 input/10 output. Actual sample payload still unverified. Do not reuse Ploytec codec. |
| USB audio transport | Ploytec bulk/interrupt variants, PCM OUT 0x05 / IN 0x86 | Observed isochronous OUT 0x06 / IN 0x82. Different transport scheduling and clock validation required. |
| MIDI input | Ploytec dedicated MIDI input, rate-dependent slots with idle/control stripping | Sixty-Four exposes vendor HID IN 0x81; 64-byte report capacity. Envelope and enabling sequence remain unknown. |
| MIDI output | Ploytec embeds bytes in PCM output, paced at 2500 bytes/s | Sixty-Four has HID OUT 0x01. Do not inherit audio dependency, framing, idle-byte filtering or pacing constant. |
| Startup | Ploytec firmware/status/rate requests, status write and endpoint recovery sequence | These are model-family-specific writes. None is authorized or justified for the Rane. |
| Standard USB MIDI | Separate UsbMidi engine and common USB-MIDI codec | No standard MIDIStreaming interface in observed Sixty-Four descriptors; this codec is not automatically applicable. |

Protocol sources: [codec](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/common/devices/ploytec/ploytec_codec.h),
[endpoint definitions](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/common/devices/ploytec/ploytec_defs.h),
[MIDI framing](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/common/devices/ploytec/ploytec_midi.h),
[handshake](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/common/devices/ploytec/ploytec_handshake.c),
[models](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/common/devices/ploytec/ploytec_models.h).
Rane evidence: [A descriptors](hardware/sixty-four-usb-a/DESCRIPTOR_FINDINGS.md),
[B HID findings](hardware/sixty-four-usb-b/HID_FINDINGS.md).

## Useful engineering patterns

1. **Separate device protocol from host transport.**
   [OzzyEngine](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/macos/OzzyCore/OzzyEngine.h) and
   [IOzzyBus](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/macos/OzzyCore/IOzzyBus.h) separate profiles, lifecycle,
   transport submission and protocol completion. Adopt that boundary in the
   Rane design. The inspected packet interface lacks explicit isochronous
   microframe scheduling/results, so it is not a drop-in transport abstraction.
2. **Publish device-related timestamp anchors.**
   [Shared data](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/macos/Shared/OzzySharedData.h) and
   [HAL](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/macos/OzzyHAL/OzzyHAL.mm) expose paired sample/host times with a
   sequence counter. Use the concept to replace nominal host-clock-only
   timing, then validate against actual Rane sample counts. Do not copy the
   Ploytec packet periods or presume this proves drift-free Rane recording.
3. **Protect memory across disconnects.** The HAL pins shared-memory use
   during CoreAudio calls before allowing teardown to unmap it. This addresses
   a real lifetime problem; apply equivalent ownership to whichever Rane
   transport/IPC is chosen, without forcing a shared-memory rewrite now.
4. **Make queue ownership and ordering explicit.** Shared indices use
   acquire/release ordering, including the kernel side on arm64. Audit our
   audio ring's producer-side removal on overflow before hardware playback.
5. **Handle MIDI backpressure and lifecycle.**
   [OzzyMIDI](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/macos/OzzyMIDI/OzzyMIDI.mm) includes a pending output queue,
   drop accounting and per-port synchronization. Its recent history also
   addresses Stop deadlocks and device removal. These are useful scenarios
   for our blocking socket, partial-send and output-truncation problems.
   Bounded queues still need explicit overflow behavior; these are patterns
   to review, not proof that Ozzy is bug-free.
6. **Separate identity from display name.** Multi-device handling is useful,
   but a host USB location is not the mixer's physical A/B identity. Our A/B
   ports share VID/PID and serial text. Define session/port identity explicitly.

Selected legacy references:
[userspace USB engine](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/legacy/mac-hal/Ozzy/PloytecUSB.cpp) and
[DriverKit device](https://github.com/mischa85/Ozzy/blob/f691999b3b6099c4a6c22b7c4fdc53427d2d552c/legacy/mac-coreaudio/PloytecDriver/PloytecDevice.cpp).
The userspace engine constructor opens hardware and starts its thread;
it is not a passive diagnostic tool. DriverKit is distinct from an ordinary
userspace daemon and brings its own entitlement/deployment requirements.

## Limits and recommendation

Proceed with architectural reuse, not a backend migration or command replay.
Keep the current exact Sixty-Four allowlist and fail-closed transport gate.
Ozzy's model lookup defaults unknown IDs to a DB4 profile; do not import that
behavior into our device identification. The MIT license permits reuse with
required copyright/license notices; no source was copied into the driver in
this checkpoint.

The next code checkpoint can improve queue ownership, shutdown and MIDI
buffering offline, using meaningful overflow/disconnect/fragmentation tests.
That will improve reliability but will not unlock controls by itself.
In parallel with future implementation, the protocol gate remains unchanged:
obtain Sixty-Four-specific startup and report evidence before sending writes.
These repositories do not supply it. They also establish neither post-fader
routing/channel order nor Serato authorization, DVS or proprietary integration.

Go: use reviewed architecture and regression scenarios while preserving
normal macOS security. No-go: install Ozzy's kext, substitute Ploytec commands,
assume HID silence requires its handshake, or claim M2 audio/MIDI readiness
from source inspection. No external code was executed or performance tested.
