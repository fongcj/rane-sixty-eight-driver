# Listener audit — 2026-10-12

## Completed without activating the mixer

- Targeted IOService registry snapshot: `listener-audit-registry.txt`.
  Sixty-Four 1cc5:0017 is connected, current configuration 0. No USB
  interface or HID service appears below it. The listed Chrome/ChatGPT
  device user clients do not establish exclusive ownership.
- User independently confirmed no Rane device in Audio MIDI Setup's audio
  or MIDI views. This agrees with the current unconfigured USB state.
- `test_capture_callback` directly invokes the capture tool's actual callback
  with synthetic reports; it never enters hardware discovery. Full 64-byte
  and short reports are accepted, stopped delivery is ignored, invalid
  lengths, null data, error status and non-input reports are rejected.
  A separate exact-output comparison verified all 64 bytes and the short
  report's ID and payload. Results: `callback-selftest.txt`.
- CTest: 5/5 passed. Both listener and test built for the local Mac.

These results validate formatting and callback guards, not USB delivery,
run-loop scheduling, mixer transmission, MIDI interpretation or audio.
A null-buffer guard was added; no evidence suggests that caused earlier silence.

## Alternative delivery path, prepared but not yet tested on hardware

`capture_sixty_four_controls --activate-manager-listen-60s` registers the
raw report callback on IOHIDManager before opening it, allowing the manager
to own device opening, buffers and scheduling. The original mode explicitly
opens and schedules its selected IOHIDDevice as well as its manager. That
lifecycle overlap is worth eliminating as a diagnostic variable, but it is
not a proven bug. This is an alternative within the same Apple HID stack,
not a wholly independent USB implementation.

Both modes require exact configuration-descriptor identity, refuse an already
configured device, use non-seizing opens, temporarily select configuration 1,
listen for at most 60 seconds/1000 reports, and restore/verify configuration 0.
Neither sends HID output or vendor commands or selects audio streaming alternates.
A forced termination or device disconnect can prevent restoration verification.
No active capture was run during this offline audit. Wait for the user's ready
signal and announce actual READY before asking for five presses of the left
1/5 (Cue/Loop) button with Deck 1 assigned to 1B.

If this path is also silent, do not conclude initialization is required solely
from that result. A known-working supported host or a genuinely independent
USB trace would distinguish hardware transmission from Apple HID delivery more
strongly. Do not repeat identical button captures without changing a diagnostic
variable.

## Alternative capture completed

Capture 06 used the manager-owned path. The user confirmed the five button
presses were done while the process was still running. It completed normally
with zero input reports and no logged errors. Configuration 0 restoration
was verified. See `controls-capture-06.txt` and its errors file.

The during-capture snapshot `controls-capture-06-registry.txt` confirms
configuration 1, all four USB interfaces, AppleUserUSBHostHIDDevice bound to
interface 3, the expected 28-byte report descriptor, MaxInputReportSize 64,
and an IOHIDLibUserClient belonging to the capture process. Thus absence of
HID enumeration during this window is ruled out. The original listener's
explicit device scheduling is also not necessary to reproduce the silence.
This still cannot distinguish no device transmissions from reports not
reaching user space through the common Apple HID stack. No initialization
requirement or hardware fault is proven.

## Read-only system diagnostic review

Saved existing unified logs for capture 06 in `controls-capture-06-system-log.txt`
(2026-10-12 21:25:17–21:26:20, timestamps as emitted by log). No configuration
change or new capture was performed for this review; current registry still
shows configuration 0.

- Apple HID found interrupt IN address 129 (0x81) and OUT address 1,
  obtained the 28-byte descriptor and started with return 0.
- IOHIDLibUserClient became valid and started its queue at 21:25:18.471.
  The saved during-capture registry shows ClientOpened=Yes,
  ClientSuspended=No, queue head/tail=0 and EnqueueErrorCount=0. These are
  one snapshot, not end-of-capture counters.
- Queue stop/close occurs at 21:26:18.551. Completion errors appear only
  afterwards in the selected log: e00002eb (kIOReturnAborted), then
  e00002d8 (kIOReturnNotReady), immediately around configuration restoration.
  Meanings verified against the installed SDK's IOKit/IOReturn.h.
  Timing is consistent with teardown; these are not evidence of an error
  during the button window.
- handleStop logs irc:0/irt:0, but those abbreviated counters have not been
  validated against the exact running implementation and are not treated
  as an authoritative USB packet count.
- No successful report deliveries are recorded in these existing logs.
  Logging is not a complete wire trace. Absence of entries cannot prove
  absence of packets, nor distinguish firmware gating from host-stack issues.

The available evidence does not demonstrate reports being dropped by our
callback. Further identical captures have low value. An optional direct
Mac-to-mixer cable test would remove the observed VIA hub as a variable;
otherwise model-specific initialization evidence or an independent wire
trace is needed. Do not infer a hardware fault or send Sixty-Eight opcodes
based on this silence.
