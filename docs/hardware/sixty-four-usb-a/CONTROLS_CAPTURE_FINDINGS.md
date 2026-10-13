# USB A control capture — 2026-10-12

User reported ready on A following instructions to connect USB A, select
Deck 1 source 1A, and confirm CONTROL illumination. Physical settings were
not independently observed. The user confirmed completion of five left
1/5 (Cue/Loop) button presses while capture 07 was still running.

The manager-owned listener activated configuration 1, reached READY, and
completed 60 seconds with zero reports and no logged errors. Restoration
to configuration 0 was verified. No HID output, vendor initialization, or
audio alternate-setting selection was performed by the capture tool.

Evidence: `controls-capture-07.txt`, `controls-capture-07-errors.txt`, and
`controls-capture-07-registry.txt` (during-capture registry snapshot).

Result matches USB B capture 06 with the same listener. Changing port and
corresponding deck assignment did not produce reports in these tests. This
does not prove a firmware initialization requirement, hardware fault, or
absence of packets on the wire. Both tests share the Mac, hub, HID stack
and lack of verified model-specific initialization. No further identical
port-switching capture is justified by these results.
