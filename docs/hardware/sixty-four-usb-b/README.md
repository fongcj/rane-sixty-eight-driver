# Checkpoint 3: USB A/B comparison

2026-10-12. User moved the same cable from mixer USB A to USB B. The new registry entry ID and session ID confirm a new enumeration. Host remains macOS 15.7.4 (24G517), arm64, through the same VIA Labs hub.

Read-only inventory and the standalone cached-descriptor reader were used. The reader ran outside the sandbox because sandboxed IOUSBLib metadata access had failed. No device/interface open, configuration/alternate change, initialization, audio streaming or MIDI output was performed.

| Evidence | USB A | USB B |
| --- | --- | --- |
| VID:PID | 1CC5:0017 | 1CC5:0017 |
| Product / manufacturer | Rane Sixty-Four / Rane Corporation | Same |
| bcdDevice / bcdUSB | 0100 / 0200 | Same |
| Reported serial string | RH.01.00 | Same |
| Configuration descriptor size | 235 bytes | 235 bytes |
| Configuration bytes | See A capture | Byte-for-byte identical to A |
| Audio endpoints | OUT 06 / IN 82 | Same |
| HID endpoints | IN 81 / OUT 01 | Same |

Both binary configuration captures have SHA-256:

`dc56db9b0317901bdba64cc0d94fadf30ecdecd7be15ce3831f5bfc7d8078472`

The evidence supports sharing descriptor expectations between these two ports on this particular mixer. It does not prove identical initialization or routing state, or generalize to all firmware versions. Firmware version remains unverified; bcdDevice and the reported serial are not treated as firmware queries.

## Implementation consequence

Renamed the observed PID constant from `RANE_PID_SIXTY_FOUR_USB_A` to `RANE_PID_SIXTY_FOUR_OBSERVED`, and removed the provisional USB A label/model UID suffix. The observed entry is still disabled for streaming. The old upstream 0014 candidate remains unverified.

VID/PID and the observed serial cannot distinguish A from B. The unchanged host location ID reflects reusing the same host cable/hub socket; it does not identify the mixer port. When both ports are connected, track separate service instances/host locations, and obtain physical A/B assignment from a verified mechanism or user label. Never infer mixer A/B solely from PID. The descriptor reader deliberately refuses multiple matching devices to avoid an ambiguous capture.

## Remaining boundary

Cached configuration comparison is complete. The HID descriptor advertises a 28-byte report descriptor, but its contents and control-message framing remain unknown. No standard MIDIStreaming interface was found. The next investigation should prepare a bounded report-descriptor/input-capture experiment or use a trace from an already-working setup. It must not silently configure the device or send the repository's initialization sequence.

See [USB A interpretation](../sixty-four-usb-a/DESCRIPTOR_FINDINGS.md) for the audio/HID layout and its limits. Raw local evidence is in `registry.txt`, `descriptors.txt` and `configuration-0.bin`; review identifying strings before publication.
