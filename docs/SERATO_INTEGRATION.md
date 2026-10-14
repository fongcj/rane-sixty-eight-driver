# Serato native integration investigation

Tracked in [GitHub issue #2](https://github.com/fongcj/rane-sixty-eight-driver/issues/2).

Read-only checkpoint, 2026-10-13. No purchase, application modification,
driver installation, hardware writes, or audio-setting changes were performed.

## Confirmed failure

The installed Serato DJ Pro is **2.5.5, build 2.5.5.83**, an x86_64 executable.
Serato DJ Lite is separately installed at **4.0.7, build 4.0.7.229**.
Do not confuse their connection behavior.

The existing Pro log `20261013-102147.14967.log` establishes:

- Lines 32–40: both our recording-only and duplex CoreAudio devices enumerate.
- Lines 104 and 114–116: USB VID 0x1cc5 / PID 0x0017 is recognized as
  Rane Sixty-Four and native connection begins.
- Line 726: device MIDI connection is not ready.
- Lines 727–729: aggregation fails because MIDI or HID endpoints cannot be found.

A second Pro log, `20261013-101740.14407.log`, has the same aggregation failure
at lines 11928 and 11953. These are existing observations, not a fresh live test.
The inspected Pro run contains no new MIDI connection entry; it does not establish
whether our bridge was running then. The later Lite log does enumerate the bridge,
but identifies it with VID/PID zero as a generic CoreMIDI input.
Raw logs stay local and are not copied into this repository.

Serato lists Pro 2.5.5 with macOS Mojave as the last supported Sixty-Four
configuration: [official compatibility table](https://support.serato.com/hc/en-us/articles/4411117032975-Serato-software-operating-system-compatibility-for-discontinued-hardware).
The installed Pro version therefore includes device recognition; the M2/macOS
15 environment remains outside that supported configuration. Updating or buying
Serato is not the proposed next step.

## Original driver presentation versus this bridge

Offline inspection used the previously extracted official SixtyFour 2.0.0b3
package (never installed or executed). Archive SHA-256:
`3f98fff851d2cad524300bbfc65d67b405512ea1abcb382e51c7429615715e3b`.
Source: [Rane legacy package](https://cdn.inmusicbrands.com/rane/legacy-product-downloads/SixtyFour-2.0.0b3.zip).

In the original x86_64 `SixtyFourMidiDriver` binary:

- `MatchDevice`, 0x5050: matches vendor 0x1cc5 and product 0x0017.
- `CreateDevice`, 0x5080: calls MIDIDeviceCreate with name/model
  “Rane Sixty-Four” and manufacturer “Rane”. Calls MIDIDeviceAddEntity at
  0x50f8 with one source and one destination.
- Driver base code calls MIDISetupAddDevice at 0x3f61 and sets
  USBVendorProduct at 0x3fb8, USBLocationID at 0x3fca, and a string property
  at 0x3fe1. CFString references and the cstring section resolve the first two
  property names. Exact value construction and the third property need further audit.
- `ConnectToApi`, 0x52f0: calls RH4_open, registers HID-message, USB-port-status,
  and beat-clock callbacks, and queries port status and beat clock.

Our foreground bridge instead creates a virtual source named
**Rane Sixty-Four Controls**, with no destination or parent hardware MIDI device.
It forwards channel MIDI input and discards SysEx. Those differences matter:
manual mapping works (user confirmed Rekordbox trial and Ableton), but the
original bidirectional device topology and native protocol are absent.

Inference: MIDI device association is the earliest demonstrated native connection
blocker. The logs do not prove that renaming an endpoint, adding metadata, or
providing output alone will fix connection. They also do not establish a direct
Serato dependency on SixtyFourApi; its use is confirmed in the original MIDI driver.

## Next checkpoint

1. Add a read-only CoreMIDI inventory diagnostic that reports parent device/entity,
   input/output topology, USBVendorProduct, USBLocationID, and stable IDs. It must
   not send MIDI, open USB, create endpoints, or change properties.
2. With the mixer connected and the existing bridge active, make one fresh Pro
   2.5.5 connection attempt and compare its log to that inventory. This separates
   an absent bridge from a present-but-unassociated endpoint. No button sequence
   is required. Starting Pro could cause it to issue its normal connection messages;
   this is a live application test, not purely passive hardware observation.
3. Audit original metadata value construction and outbound MIDI/SysEx conversion
   before implementing a reversible compatibility prototype. Bind any physical
   identity to the verified actual mixer. Do not invent authorization responses.
4. Initially capture application output locally without forwarding unknown
   messages to hardware. Passing aggregation is only a discovery milestone;
   validate any resulting initialization separately before enabling hardware writes.

Go: repeatable evidence identifies the next missing interface and it can be
implemented with verified transport semantics and preserved audio ownership.
No-go for a live write: unknown configuration/firmware messages, unaudited SysEx,
or loss of audio stability. Do not install the Intel kernel extension, weaken
security, or patch Serato to force acceptance.

## Separate success criteria

| Capability | Current evidence / required proof |
| --- | --- |
| Standard MIDI mapping | User confirmed in Rekordbox trial and Ableton |
| Serato device aggregation | Fails in existing Pro logs; fresh bridge-present test needed |
| Native controls and LED feedback | Bidirectional transport and application protocol unimplemented |
| Hardware authorization | Not demonstrated; ordinary MIDI does not supply it |
| Four-deck audio routing | Our CoreAudio outputs work; Serato-native routing unverified |
| DVS | Requires separate supported integration/entitlement and timecode tests; not demonstrated |

No purchase is required for this investigation, and no successful Serato native
connection is claimed.

## Read-only inventory tool

Build target `inventory_sixty_four_midi`, then run it with `--list`. It reports
device/entity topology, source/destination parent references, and selected identity
properties. Missing properties are omitted rather than reported as zero. It creates
only a temporary CoreMIDI client, not an endpoint, and sends no MIDI or USB requests.
No arguments prints usage. Output can include unrelated saved MIDI devices; keep
full inventories local.

The initial live inventory on 2026-10-13 contained no active Rane source or
destination, and process inspection found neither the bridge nor Serato running.
The prior audio session had ended in a failed state with an input-transfer error;
these cumulative counters do not establish when or why that happened. A new audio
session and bridge-present connection attempt are required before attributing a
fresh Serato failure to metadata. The inventory target builds with warnings treated
as errors and its usage and live enumeration paths were exercised successfully.
