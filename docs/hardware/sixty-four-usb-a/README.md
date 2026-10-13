# Sixty-Four USB A: initial passive inventory

Observed 2026-10-12 on macOS 15.7.4 (24G517), arm64. User identified the physical mixer connection as USB A. Collected with `system_profiler SPUSBDataType -json`, `ioreg -p IOUSB -w 0`, and the scoped read `ioreg -r -n 'Rane Sixty-Four' -l -w 0`. No custom driver or diagnostic binary was started.

| Property | Observed value |
| --- | --- |
| Product | Rane Sixty-Four |
| Manufacturer | Rane Corporation |
| VID | 0x1CC5 (7365) |
| PID | 0x0017 (23) |
| bcdDevice | 0x0100 (1.00; not established as firmware version) |
| bcdUSB | 0x0200 |
| Configuration count | 1 |
| Device class/subclass/protocol | 0 / 255 / 255 |
| Endpoint-zero maximum packet size | 64 bytes |
| Registry speed fields | USBSpeed=3; Device Speed=2 (raw values retained) |
| Connection path | VIA Labs USB2.0 hub, then host controller |

The observed PID differs from the upstream candidate Sixty-Four constant 0x0014. Do not assume that 0x0014 is USB B or another firmware variant. USB B remains unobserved. No transport enablement or ID change was made from this inventory alone.

The scoped registry contains no audio/MIDI interface or endpoint descriptors. It lists device user clients belonging to Chrome and ChatGPT; this does not establish that those applications claimed an interface or prevent driver access. Do not terminate them or seize the device based solely on these entries.

System Profiler emitted four `IOCreatePlugInInterfaceForService failed 0xe00002be` warnings but still returned the mixer identity. Their cause and association with individual devices are not established. Registry and System Profiler agree on the mixer VID/PID.

Next evidence needed: cached configuration descriptors (all interfaces/alternates/endpoints and class-specific descriptors), HID report descriptors if available, and a separate USB B inventory during an agreed idle reconnection. Any descriptor reader must avoid opening/seizing/configuring the device or sending initialization writes. These observations establish enumeration only, not audio/MIDI functionality or class compliance.

`registry.txt` is a local scoped snapshot; review identifying strings before publishing. It is intentionally not committed or uploaded by this investigation.

## Follow-up

Cached configuration descriptors have now been retrieved successfully without opening the device. See [checkpoint 2 findings](DESCRIPTOR_FINDINGS.md) and [descriptor bytes](descriptors.txt). The earlier missing-descriptor observation above describes the registry-only phase, not the current evidence set. The observed PID is now represented as disabled metadata in source; streaming remains blocked.
