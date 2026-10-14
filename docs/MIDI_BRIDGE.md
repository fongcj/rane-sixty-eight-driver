# Sixty-Four input-only CoreMIDI bridge

This foreground prototype exposes **Rane Sixty-Four Controls** as a native
CoreMIDI source. It does not install a driver or launch agent. The installed
CoreAudio driver stays unchanged. LED output and MIDI-to-mixer messages are
not implemented; there is no CoreMIDI destination.

## Run

Build `sixty_four_midi_bridge` and `monitor_sixty_four_midi` with CMake.
After approval for a live run, start audio through the Sixty-Four, then run:

```sh
/tmp/rane-playback-offline-build/sixty_four_midi_bridge --run-existing-audio
```

This path is the current development build directory. For another build,
use the corresponding executable path. Running without arguments prints
usage without opening USB or creating an endpoint.

The bridge requires exactly one mixer with the audited descriptor fingerprint
and configuration 1. It verifies the HID descriptor and physical location,
opens HID without seizing, creates the MIDI source, and sends the tested
64-byte startup report once. It never opens/configures the USB device or selects
an audio alternate setting. It sends no control-dump, unmute, or LED sequence.
A per-user lock prevents duplicate instances. Its source uses stable unique ID
`0x52363443`; an ID collision causes setup to fail before the startup report.

Keep audio running. The current audio driver owns configuration 1 only during
an audio session; the bridge exits when the HID device disappears. Stop the
bridge with Ctrl-C. It sends release events for tracked held notes **to software**
on orderly shutdown, then disposes its source. A crash or forced kill cannot
provide that cleanup. No inverse hardware startup command is known or sent.

## Mapping test

1. Wait for the bridge's READY message.
2. Start the read-only subscriber:
   `/tmp/rane-playback-offline-build/monitor_sixty_four_midi --listen-60s`.
3. With Deck 1 on 1A, press and release the left 1/5 Cue/Loop button. Expect
   `90 68 40` and `80 68 40`. The monitor reads the CoreMIDI endpoint, not HID,
   so this checks the whole input path.
4. Select **Rane Sixty-Four Controls** in the target application's MIDI input
   settings and use its learn/mapping function where supported. Application
   support and restrictions must be tested separately; no Rekordbox or Serato
   settings are changed by this program.

A working endpoint does not provide Serato hardware authorization, DVS licensing,
or proprietary native integration.

## Parsing and limits

The bridge accepts only the observed unsolicited transaction `0x1700c51c` in
64-byte command-0x40 envelopes, with payload lengths <=58. Unknown transactions,
startup responses, and padding are not MIDI data. The allocation-free parser
handles channel voice messages, running status, report fragmentation, and
interleaved realtime messages. It discards SysEx, reserved statuses, and orphan
data. Invalid reports reset partial-message state. MIDI clock is filtered by
default; `--include-clock` forwards it. Other supported realtime messages are
preserved. Callback receipt supplies the host timestamp; device timestamps and
latency calibration are not implemented.

Deck 1 Trigger 1, clock, and CC 0 traffic have been captured on hardware so far. Other
buttons, relative encoders, layers, channels, reconnect behavior, mapping
persistence, and simultaneous audio reliability need live coverage. MIDI-only
operation with no audio session and automatic relaunch are future work; the
bridge deliberately leaves USB configuration ownership to the audio driver.

## Validation

All 16 CTest tests passed after implementation. New tests cover observed
meaningful report prefixes with synthetic padding; every envelope length and
command; all channel statuses and split positions; running status/realtime
interleaving; SysEx suppression; reset behavior; unknown transactions; held-note
cleanup; device removal; and CoreMIDI delivery failure. The bridge callback test
uses mocked CoreMIDI delivery and opens no hardware. The stream parser also
passed AddressSanitizer/UndefinedBehaviorSanitizer.

The first approved live launch refused before sending a startup report because
configuration was 0. Read-only diagnostics showed audio session 4 in failed
state, with one playback scheduling fault (stage 4). This refusal does not
validate live CoreMIDI delivery. Audio must be restarted before that test.

## Successful live CoreMIDI test — 2026-10-13

After the user restarted playback and confirmed audible audio through the mixer,
the bridge started successfully. A separate CoreMIDI input client connected to
**Rane Sixty-Four Controls** and received all five requested left-strip button
press/release pairs (`90 68 40` / `80 68 40`). This verifies delivery through
CoreMIDI, beyond the earlier raw HID capture. The subscriber also received
channel-1 CC 0 changes. No physical movement was correlated to those CC messages;
do not label them a validated crossfader exercise. Clock was filtered by default.

During this test, audio diagnostics showed session 5 running, zero input/output
schedule faults, zero transfer errors and zero input missing frames. Counters
also showed 1024 rejected output frames and one write failure; there is no
pre-bridge baseline for this session, so attribution is unresolved. The user
confirmed button completion but did not separately confirm audio quality after
the MIDI test. This is functional coexistence evidence, not a long-duration
reliability result.

The bridge subsequently exited when the audio-owned HID session disappeared.
The final diagnostic snapshot showed session 5 failed with one output scheduling
fault (stage 4), a maximum input-callback gap of about 90 ms, and 4096 rejected
output frames. The transport schedules output 64 USB milliseconds after input;
a delayed callback exceeding that lead is consistent with missing its scheduling
deadline, but existing counters do not uniquely identify the failing branch.
The earlier pre-bridge session also failed at stage 4, so MIDI causation is not
established. The monitor completed with 786 messages; retained output contains
all five note-on/off pairs. Bridge totals were 2720 reports, 1767 forwarded
messages, 1695 filtered clock messages, and zero ignored reports.

The source is no longer running. Resolve the audio scheduling failure before
claiming reliable continuous MIDI/audio use or adding automatic launch.
No installation,
launch-at-login setting, system audio restart, or audio driver update was made.
Rekordbox mapping, encoder values, LED feedback and reconnect behavior remain
unverified. If audio stops and its HID session disappears, restart the bridge
after restarting audio.

### Follow-up with audio driver 0.2.2

After the approved audio update and user-confirmed playback recovery, an
audio-only 30-second baseline and a further 30-second watch with the bridge
running were clean: no scheduling faults, transfer errors, rejected frames,
post-start missing output frames, or write failures. The USB worker's higher
QoS request succeeded. The bridge was restarted successfully and remained
running at the end of this check. It still requires an active audio session;
this is an initial coexistence check, not long-duration validation.

## Rekordbox mapping checkpoint (initial attempt)

The next attempt to inspect Rekordbox was blocked by pending Computer Use
Accessibility/Screen Recording permissions. No Rekordbox UI state or mapping
was verified, and no application settings were changed. A read-only audio
snapshot before the attempt showed session 1 still healthy at about 259 seconds,
with zero scheduling faults, rejected frames, or post-start missing frames.

The official [Rekordbox 7 MIDI Learn guide](https://cdn.rekordbox.com/files/20241203210623/rekordbox7.0.5_midi_learn_operation_guide_EN.pdf)
places MIDI setup in Performance mode, via the MIDI button near Preferences.
First confirm that **Rane Sixty-Four Controls** appears in the connected-device
menu. CoreMIDI delivery alone does not prove this application lists a virtual
source. If present, export any existing mapping before editing. Use one Deck 1
button assignment as the first test; its verified input code is `9068` (status
0x90, note 0x68). The guide permits direct hexadecimal input, which avoids
unrelated CC 0 traffic interfering with MIDI Learn. Verify the intended action
in Rekordbox before expanding the mapping. Do not import over an existing map
or press DEFAULT merely to test detection.

If MIDI setup is unavailable, inspect the current mode and plan/unlock state;
feature availability is governed by Rekordbox, not this bridge. Do not purchase
or change subscriptions as part of the driver test.

## User-confirmed application mapping — 2026-10-13

The user subsequently confirmed manual MIDI mapping works in both the Rekordbox
trial and Ableton. This supersedes the earlier pending mapping checkpoint. It does
not validate every control, LED output, or Serato native integration. See
[SERATO_INTEGRATION.md](SERATO_INTEGRATION.md) for the separate native connection
investigation and its confirmed endpoint-aggregation failure.
