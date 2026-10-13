# HID descriptor and bounded input experiment

2026-10-12, USB B, 1CC5:0017. This checkpoint advances beyond cached metadata reads; the exact additional operations are recorded below.

## Report descriptor

The non-opening reader's explicit `--hid-report` option issued one standard device-to-host GET_DESCRIPTOR: bmRequestType=81, bRequest=06, wValue=2200, wIndex=3, wLength=28. Timeouts: one second no-data, two seconds completion. It succeeded with 28 bytes while the device reported configuration 0. No configuration or HID output was sent by this reader.

Raw bytes: `06 00 ff 09 01 a1 01 19 01 29 02 15 00 25 01 75 08 95 40 81 02 19 01 29 02 91 02 c0`.

HID item interpretation:

- Usage page FF00, usage 01, application collection.
- Usage range 01-02; logical range 0-1.
- Report size 8 bits, count 64.
- Input and output are Data/Variable/Absolute (02).
- No Report ID item and no Feature report item.

This defines 64-byte IN and OUT report containers, not the meaning of their payloads. Do not interpret the logical range as proof that MIDI byte values are restricted to 0/1. The bytes alone do not establish the upstream 40/41 envelope or 36 initialization opcode.

## Capture tool and rollback

`tools/debug/capture_sixty_four_controls.c` requires `--activate-and-listen-60s`. It verifies the complete observed configuration fingerprint, requires exactly one matching mixer and initial configuration 0, opens without seizing, temporarily selects configuration 1, and listens for up to 60 seconds or 1,000 reports. It installs no custom driver, selects no audio streaming alternate, and sends no MIDI/HID output, vendor initialization or firmware request. Normal built-in macOS HID binding can occur after configuration selection.

The final listener uses IOHIDManager/IOHIDDevice input callbacks. Cleanup closes its HID access and restores the original USB configuration, then checks that restoration. SIGINT/SIGTERM request cleanup; forced termination, process crash or physical disconnect cannot guarantee rollback. If restoration fails, the tool reports it explicitly; reconnect USB during an idle session before further experiments.

Attempt 01 used a raw USB interface open and received 0xe00002c5 (exclusive access). It stopped without forcing access and verified restoration to configuration 0. Attempt 02 uses the built-in HID service instead. Neither attempt sends the Sixty-Eight handshake. See individual logs for measured results.

## Remaining unknowns

No output-report payload is authorized by descriptor similarity alone. A silent input capture cannot distinguish required initialization from control routing, no controls exercised, or other protocol behavior. Correlate user actions with captured reports before assigning MIDI meaning. Keep audio streaming disabled while establishing this evidence.

## Attempt 02 result

The built-in HID path opened successfully and reached READY. The 60-second window completed with **zero reports** and verified configuration 0 restoration. At the time of this entry, no user confirmation of controls exercised during the window had arrived. This is not a validated negative button test. Next, coordinate readiness before a repeat capture; log physical control/layer and A/B routing state. Do not infer a required vendor initialization from this result.

Both standalone diagnostics compiled natively for arm64 with `-Wall -Wextra -Werror`, and their CMake targets built successfully. Runtime evidence establishes descriptor read, HID access and restoration, but no MIDI payload decoding yet.

## Attempt 03: coordinated control exercise

User confirmed readiness, then received the READY prompt and confirmed completion while the capture was still running. Requested sequence: left 1/5 Cue/Loop button three press/releases, LOOP once, SCROLL/LOAD three clicks each direction, then one knob press. Source selectors, deck assignment and layer settings were left unchanged and their positions have not yet been recorded.

Result: zero HID input reports over 60 seconds; no reported capture error; configuration 0 restoration succeeded and was verified. This is a negative result for this listener and the existing routing state, not proof that the mixer cannot send MIDI or that a particular initialization command is required. The first follow-up is to observe the left control strip's deck assignment and that deck's USB source selection: the manual documents that deck MIDI follows USB routing. Do not send an unverified initialization sequence to compensate for an unrecorded routing state.

## Attempt 04: Deck 1 routed to USB B

User reported the channel source on 1B and the CONTROL indicator under Deck 1 lit before capture. The same left-strip control sequence was requested after READY. Capture completed with zero reports, no logged error, and verified restoration to USB configuration 0. Confirmation that the sequence was completed during this window is still pending at this entry.

The tool's configuration restoration does not restore physical source selectors: Deck 1 remains at the user-selected 1B position unless the user changes it. No HID initialization or output report was sent. Next diagnosis must distinguish a silent device from input-capture path behavior; no particular vendor handshake is justified by the absence of reports alone.

## Attempt 05: clarified button sequence

User confirmed readiness with Deck 1 previously set to 1B. After READY, instructions explicitly distinguished the LOOP button from the loop knob: left 1/5 three times, LOOP button once, SCROLL/LOAD three clicks each way and one press. User replied 'done'; that confirmation arrived after the process finished, so individual actuation timestamps are not independently established.

Result: zero reports, no logged capture error, and verified restoration to configuration 0. No MIDI/HID output or audio streaming was performed. Repeating this unchanged experiment is unlikely to resolve the uncertainty. Next investigate the IOHID capture path and whether the Sixty-Four requires a documented input-report activation sequence; do not assume the Sixty-Eight handshake is appropriate. User control exercises should resume only when a changed diagnostic can distinguish these possibilities.
