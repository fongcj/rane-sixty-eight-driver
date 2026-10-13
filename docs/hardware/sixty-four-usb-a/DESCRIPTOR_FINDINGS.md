# Checkpoint 2: cached USB descriptors

2026-10-12, user-confirmed USB A, observed 0x1CC5:0x0017.

## Method and safety boundary

Built `tools/debug/read_sixty_four_descriptors.c` as a standalone arm64 executable with warnings treated as errors. It matches the exact observed identity, rejects multiple matches, rechecks identity, and calls `GetConfigurationDescriptorPtr` without opening the device. Apple's installed SDK explicitly documents that this API does not require the device to be open. It has no device-request, interface-open, configuration-setting or HID-output path and does not link the driver engine.

Initial sandbox access failed with 0xe00002be. The identical reader succeeded when permitted outside the sandbox. This is evidence that sandbox restrictions affected this reader; it does not prove that every earlier System Profiler warning had the same cause. No macOS security setting was changed.

Saved decoded/raw hex in `descriptors.txt` and reconstructed the complete 235-byte configuration in `configuration-0.bin`. Descriptor boundaries, lengths and total length were checked during reconstruction. This is configuration index zero, configuration value one; reading it did not select it. The empty error file represents the successful run.

## Observed interface layout

| Interface | Alternate | Class/subclass/protocol | Endpoints |
| --- | --- | --- | --- |
| 0 | 0 | FF / 01 / 20 | None |
| 1 | 0 | FF / 02 / 20 | None |
| 1 | 1 | FF / 02 / 20 | OUT 06, attributes 05, max 280 bytes, interval 1 |
| 2 | 0 | FF / 02 / 20 | None |
| 2 | 1 | FF / 02 / 20 | IN 82, attributes 25, max 336 bytes, interval 1 |
| 3 | 0 | 03 / 00 / 00 | IN 81 and OUT 01, interrupt, max 64 bytes each, interval 1 |

Endpoint attribute decoding follows the installed Apple SDK USBSpec.h / AppleUSBDefinitions.h: audio OUT is asynchronous isochronous data; audio IN is asynchronous isochronous with implicit-feedback usage. The HID descriptor advertises a 28-byte report descriptor; those 28 bytes are **not** included in this cached configuration. The saved bytes do not describe MIDI message payloads.

The FF class on interfaces 0-2 is vendor-specific, despite audio-like subclass/protocol values and class-specific descriptors. This does not advertise ordinary Audio class 01 for those interfaces. No standard USB MIDIStreaming interface is present in this configuration. MIDI-over-HID remains a hypothesis requiring report-level verification, rather than standard USB-MIDI event-packet handling.

## Audio interpretation and limits

The descriptors resemble USB Audio 2 layouts: the streaming general descriptors contain a format bitmap of 0x00000004 and channel counts 10 (OUT) / 12 (IN); format descriptors contain Type I, four-byte subslots and 32 valid bits. Under that layout, these are consistent with Float32, matching the manual and the repository's active streaming implementation. Because the interfaces are vendor-specific, retain this as a descriptor interpretation until actual samples confirm it. No sample-rate query or sample capture was performed.

The endpoint addresses, packet capacities and alternate settings match the current Sixty-Eight transport's constants. This supports investigating transport reuse. It does **not** establish identical startup commands, sample-clock behavior, routing, HID report framing, sequence/acknowledgment requirements or firmware behavior. Actual 48 kHz operation, byte order and float values still require validation.

## Changes and remaining evidence

Added 0x0017 as observed Sixty-Four USB A metadata and covered it in the offline lookup test. It remains explicitly disabled for the current transport. Retained 0x0014 only as an unverified upstream candidate; it is not relabeled as USB B.

No cached HID report descriptor was found in the scoped mixer registry. The snapshot does not expose child audio/HID interfaces. Do not select configuration one merely to make those nodes appear during a passive inspection.

The next passive comparison is a user-coordinated USB B inventory. To advance MIDI beyond descriptors, we will need the report descriptor and actual control reports, either from an existing working setup/capture or a separately reviewed access experiment. Audio validation similarly requires an explicit startup/streaming experiment. The current reader cannot answer those questions and deliberately does not escalate to device open, configuration changes or guessed handshakes.

No driver installation/loading, firmware operation, application routing change, sound output or MIDI output was performed.

## USB B comparison completed

The later [USB B capture](../sixty-four-usb-b/README.md) has the same VID/PID and identical configuration bytes. The provisional USB A-specific constant/name was therefore replaced with a port-neutral observed identity. Physical port selection cannot be inferred from PID on this mixer.
