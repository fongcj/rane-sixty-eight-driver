# Sixty-Four MIDI checkpoint — 2026-10-13

## Latest result: startup experiment succeeded in producing control reports

The user explicitly approved the one-command experiment. The tool found idle
configuration 0, activated configuration 1, verified descriptors, and submitted
the single startup candidate with return 0. Input reports arrived immediately.
During the announced left 1/5 Cue/Loop exercise, captured meaningful prefixes were:

| Report prefix | Interpretation |
| --- | --- |
| `36 ff ff ff ff 01` | Startup response prefix; complete semantics unverified |
| `40 1c c5 00 17 01 f8` | One-byte MIDI timing clock payload |
| `40 1c c5 00 17 03 90 68 40` | Channel 1 Note On, note 104, velocity 64 |
| `40 1c c5 00 17 03 80 68 40` | Channel 1 Note Off, note 104, release velocity 64 |

The [official manual](https://www.ranecommercial.com/legacy/pdf/sixtyfour_manual_dj.pdf),
printed page 35, identifies note 104 / 0x68 as Deck 1 Trigger 1,
`L_CUE_1_ALT_NOTE`, matching the requested left-strip control on Deck 1.
This demonstrates actual control delivery through the non-seizing Apple HID
path after the model-derived startup request. It does not validate all controls,
LED output, persistent firmware effects, or a CoreMIDI endpoint.

The capture stopped at its 1000-report cap before 60 seconds because of timing
clock traffic. Retained tail output contains two complete press/release pairs
and another press; do not claim all five cycles were captured. The user confirmed
completion of the five presses. Initial/middle tool output was truncated, so
there is no complete retained raw trace. The tail is local at
`/tmp/sixty-four-init36-capture-tail.txt`, not published. Subsequent captures
should spool directly to a local file to avoid terminal truncation.

USB configuration 0 restoration was verified and the tool exited successfully.
No inverse command, other setup messages, driver installation, or security change
was performed. Bytes after each declared MIDI payload contained nonzero padding;
they must never be interpreted as additional MIDI events. Tests now include
observed meaningful prefixes with synthetic padding.

Follow-up: the bounded MIDI stream parser and input-only CoreMIDI bridge are
implemented and five press/release pairs were verified through a separate
CoreMIDI subscriber. See [MIDI_BRIDGE.md](MIDI_BRIDGE.md). The original next-step
plan was: implement a bounded MIDI stream parser and input-only CoreMIDI bridge,
with correct realtime-byte handling and audio/HID lifecycle coordination.
Keep the working installed audio driver unchanged until the bridge is ready
for a separately reviewed live test. LED output remains a separate milestone.

The sections below preserve the earlier investigation and preparation history;
their "pending" / "not executed" statements describe those earlier checkpoints.

## Capture during an existing audio session

Added `capture_sixty_four_controls --listen-existing-60s`. This mode verifies
the existing exact descriptor fingerprint and configuration 1, then opens the
system HID manager without seizing. It does not open the USB device, change
configuration or audio alternate settings, or send output reports. It bounds
capture to 60 seconds/1000 reports and does not restore configuration it does
not own. Start audio before running it; leave audio running throughout.

The user confirmed five presses of the left 1/5 Cue/Loop button during the
announced capture window, on the current USB A setup. The listener found
configuration 1 and completed with **zero reports**. Its final configuration
query succeeded and still returned 1. This extends the previous silent
captures to an existing audio session. It does not prove a hardware fault,
an initialization requirement, or absence of packets on the wire.

## Original Sixty-Four implementation: offline evidence

Source artifact: Rane's [SixtyFour-2.0.0b3.zip](https://cdn.inmusicbrands.com/rane/legacy-product-downloads/SixtyFour-2.0.0b3.zip),
SHA-256 `3f98fff851d2cad524300bbfc65d67b405512ea1abcb382e51c7429615715e3b`.
Inspected extracted x86_64 symbols and disassembly with `nm -C` and
`otool -tvV`; no original executable or installer was run.

- `SixtyFourMidiDriver` contains `ASixtyFourMidiDriver::ConnectToApi`
  (0x52f0), which calls `RH4_open` and registers HID-message, port-status,
  and MIDI beat-clock callbacks. It also contains `ASixtyFourHidiMessage`
  conversion methods for MIDI input and output. These are leads for tracing
  framing and routing, not yet a verified replacement decoder.
- In `SixtyFour.10.9`, `baseaudiodevice::InitHid` (0x0fac) enumerates the
  HID-class interface and selects endpoint addresses 0x81 and 0x01. It
  allocates a 64-byte input buffer and invokes `CheckHidRead` (0x152e).
- The same initialization routine allocates an output buffer and at
  0x1292/0x1298 stores the six-byte prefix `36 ff ff ff ff 01` before
  invoking an endpoint operation with length 64 at 0x12b7. This is direct
  Sixty-Four evidence of an initialization output candidate, independently
  of any Sixty-Eight protocol. Its meaning, remaining bytes, exact virtual
  call semantics, and side effects still need verification. **Do not replay
  this prefix or guess the rest of the report.**

## Next implementation gate

Trace initialization, output-buffer construction, input callback framing,
and MIDI conversion end to end in the model-specific artifact. Resolve the
candidate command's purpose and side effects before proposing a bounded
hardware write test for explicit approval. Further unchanged button-only
captures have low value.

Once real reports arrive, correlate individual controls against the official
manual, implement a bounds-checked decoder, then publish a native CoreMIDI
source for mapping. Add output/LED support only after verifying its separate
encoding and supported controls. A CoreMIDI endpoint alone does not provide
Serato hardware authorization, DVS licensing, or proprietary integration.

No MIDI driver was installed and no HID output was sent at this checkpoint.

## Follow-up offline trace

Additional addresses below refer to the x86_64 slice of the same artifact.
These are reverse-engineering findings, not firmware documentation or live
packet observations.

| Component / address | Finding |
| --- | --- |
| Kext `DoHidRequest`, 0x18cc | Allocates 64 bytes, clears them, stores command at byte 0, a host transaction counter little-endian at bytes 1–4, and up to 59 payload bytes at byte 5. |
| Kext `SendHidiMessageInternal`, 0x5da8 | Accepts 1–58 message bytes, prefixes their length, calls `DoHidRequest` with command 0x41. |
| Kext `ReadCallbackImpl`, 0x1a48 | Requires success and a 64-byte completion, queues the block to registered user clients, and schedules the next read. |
| API `ReadHidData`, 0x4d8c | Routes recognized unsolicited transaction IDs to the event handler; other IDs are matched to pending host requests. A command byte alone does not establish that a report is unsolicited. |
| API `HandleUnsolicitedData`, 0x5be6 | Command 0x40 dispatches a MIDI-message callback after checking byte 5 <=58 and copying that many bytes from byte 6. Jump table at 0x5ddc has signed offset -0x8e for index 0 (0x40), resolving to 0x5d4e. |
| API `SetHidiMessageCallback`, 0x613a | Updates host connection bookkeeping; no USB request is sent by this routine. |
| MIDI plugin `HandleHidiMessageCallback`, 0x55f0 | Calls conversion with channel argument 0x63 (preserve incoming channel) and then passes converted input onward using a host timestamp. |

`src/protocol/sixty_four_midi.h` now provides an **offline candidate envelope
decoder** for command 0x40. It requires exactly 64 bytes, bounds the length to
58, copies only the declared payload, and preserves output on failure. It is
not linked into either audio driver or the MIDI agent. It does not yet validate
MIDI grammar, apply unsolicited-ID filtering, decode control assignments, or
publish CoreMIDI events. Synthetic tests cover every length byte and command,
short/oversized/null input, transaction endianness, and ignored padding; these
fixtures are not represented as captured hardware evidence.

Startup command 0x36 remains a hardware-test gate. The initialization code
explicitly writes command 0x36, transaction 0xffffffff, and payload byte 1.
Unlike the generic request routine, the inspected initialization path does
not explicitly clear the rest of the output buffer. Its zero-fill behavior
must be established separately before claiming an exact 64-byte replay.
`CleanUpHid` (0x0ac0) aborts/releases endpoint resources; the inspected routine
does not send an inverse 0x36 request. Do not invent a disable command with
payload 0 or assume that closing our HID listener undoes firmware state.

The host code establishes where this candidate is sent, but not its complete
firmware semantics, persistence, or reversibility. No additional hardware test
has been run and no new command has been sent during this follow-up.

## Prepared startup experiment (not executed)

`capture_sixty_four_controls --describe-init36` is a hardware-free preview.
The separately gated `--activate-init36-and-listen-60s` mode:

1. Requires one matching mixer, the exact USB configuration fingerprint,
   and idle configuration 0; refuses an existing audio session.
2. Selects configuration 1, opens the HID manager without seizing, and verifies
   the exact 28-byte HID report descriptor before output.
3. Attempts exactly one output report, ID 0, length 64: `36 ff ff ff ff 01`
   followed by 58 zeros. No retries, control-dump SysEx, unmute SysEx, LED
   commands, or audio alternate selections are included.
4. On successful submission, listens for up to 60 seconds/1000 reports, then
   restores and verifies the original USB configuration. Submission success
   is not evidence that firmware accepted or acted on the packet.

Explicit hardware-write approval is pending. Before running, close audio
applications, keep speakers/headphones low, use USB A with Deck 1 on 1A, and
wait for READY before the five left 1/5 presses. If the mixer behaves
unexpectedly, stop the test; reconnect or power-cycle manually while idle if
needed. This is an operational recovery option, not a verified reversal of
command 0x36. No automatic reset or guessed disable packet is used.

The full payload is a **candidate**, not an exact observed wire replay.
Apple's [historical XNU allocation implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-3248.60.10/iokit/Kernel/IOBufferMemoryDescriptor.cpp)
conditionally clears these buffers depending on mapping/options; it does not
justify assuming every legacy host produced identical padding. Zero padding
is a deliberate deterministic choice for the experiment. The driver's startup
call site supports testing this packet, but cannot establish all firmware
effects or persistence. No installed audio-driver update is necessary.

Build and targeted tests passed. The output API was replaced by a mock in the
callback test to check the exact bytes, one attempt per invocation, error
propagation without retries, and a preview that sends nothing. Hardware setup,
delivery, firmware behavior, and cleanup remain untested for this new mode.
