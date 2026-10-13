# Recording-only USB probe — 2026-10-12

User authorized the recording test. Mixer last reported on USB A; no cable
change was requested. Standalone tool `tools/debug/record_sixty_four_packets.c`
compiled with clang -Wall -Wextra -Werror. No driver installed/loaded, no
playback, HID output or vendor initialization sent by the tool.

## Operations

Exact 1cc5:0017 and full observed configuration-descriptor match; refuse
multiple devices and any starting configuration other than 0. Non-seizing
open; configuration 1; interface 2 only, verify starting alternate 0, select
alternate 1. Validate single isochronous IN endpoint 0x82, max packet 336,
interval 1. Submit batches of 80 microframes starting 10 USB frames ahead,
for 10 seconds. These batches have intentional gaps; this is a packet probe,
not continuous recording or a latency test. Save metadata and raw buffers.
Restore alternate 0 and configuration 0 with readback verification.

## Result

500 transfers, 40,000 microframes, every microframe contained data:

- 39,770 × 288 bytes (6 × 12 × 4)
- 120 × 240 bytes (5 × 12 × 4)
- 110 × 336 bytes (7 × 12 × 4)

The initial tool summary counted only full-size success status. Its
`other-microframes` count is misleading: these are kIOReturnUnderrun
(0xe00002e7) short transfers, carrying 240/288 bytes rather than the requested
maximum 336. Offline metadata analysis establishes the distribution above;
the tool now counts successful and short nonempty transfers together. No
additional hardware run was needed to make that correction.

Under the provisional little-endian Float32 interpretation, all samples are
finite, with low nonzero values on ten channels and zeros on channels 9/10.
No known source was supplied. This is consistent with quiet audio but does
not prove format, channel mapping, gain, fidelity or post-fader routing.

Both alternate and configuration restoration passed. Evidence:
`audio-probe-01.txt`, `audio-probe-01-errors.txt`,
`audio-probe-01-analysis.json`. Raw diagnostic data remains local at
`/tmp/sixty-four-recording-probe-01.raw` (temporary, not committed; 13,760,000
bytes). Each record is 80 native IOUSBIsocFrame entries (640 bytes on this
build), then 80 fixed 336-byte requested-size data slots. Actual lengths
must be taken from metadata; padding is not audio. No WAV produced.

## Interpretation and next gate

Recording endpoint transmits without our sending a model-specific startup
command. This materially narrows the previous MIDI silence: it is not a
complete failure of USB data transfer. It does not validate playback or all
USB hardware functions, and does not imply MIDI needs the same startup.

Next: capture a known source, identify its channels and level changes, and
only then test continuous capture, clock stability and post-fader routing.
User source availability is pending. Do not change routing automatically.

## Probe 02: source playing on channel 1

User reported something playing on channel 1. Asked to keep level steady;
no source selector or fader changes were requested. Second 10-second probe
completed with 40,000 nonempty success/short microframes, 11,519,424 actual
bytes, and verified restoration of alternate 0 and configuration 0.

Float32 decoding produced no nonfinite samples. Compared with quiet probe 01:

| USB channels | Quiet RMS dBFS L/R | Source-playing RMS dBFS L/R | Source peak dBFS L/R |
|---|---|---|---|
| 3/4 | -71.56 / -97.87 | -38.69 / -38.63 | -21.87 / -22.41 |
| 11/12 | -110.55 / -110.54 | -23.58 / -23.48 | -6.72 / -6.93 |

This is strong evidence of working USB audio input and matches the manual's
Deck 1 -> USB 3/4 and main mix -> USB 11/12 assignments. It is not a complete
channel-map validation. Source type and recording tap settings have not been
confirmed. In particular, post-fader deck capture is NOT established.
No clipping was observed in these decoded samples; fidelity is not certified.

Evidence: `audio-probe-02.txt`, `audio-probe-02-errors.txt`,
`audio-probe-02-comparison.json`. Raw file remains temporary at
`/tmp/sixty-four-recording-probe-02.raw`. Same deliberately gapped packet
format as probe 01; it must not be presented as continuous playable audio.
Next targeted test: coordinated fader movement to distinguish pre/post-fader
deck recording while observing master response. Continuous capture and
CoreAudio/Ableton exposure remain separate implementation checkpoints.

## Probe 03: attempted coordinated channel-fader test

20-second probe, 998 batches / 79,840 nonempty success-or-short microframes;
restoration verified. User was told to hold the fader, then lower it fully
at approximately 11 seconds. Actual user movement/time is not yet confirmed.
Two-second RMS summaries in `audio-probe-03-analysis.json` show continuing
signal on both 3/4 and 11/12 through the end, without a clear attenuation
step. Therefore this run does NOT establish pre/post-fader behavior. Do not
interpret lack of change as evidence the deck tap is pre-fader without
confirming physical action and channel. See `audio-probe-03.txt` for batch
times; raw remains temporary at `/tmp/sixty-four-recording-probe-03.raw`.

## Probe 04: steady fader-down instruction

User replied "ready" to instructions to lower the second-from-left Deck 1
fader fully and hold it. We explicitly stated that interpretation before
capture; physical position and main-output silence remain unconfirmed.
500 batches / 40,000 nonempty microframes, 11,519,472 bytes; alternate 0 and
configuration 0 restoration both verified. No nonfinite decoded samples.

USB 3/4 RMS: -37.68/-37.81 dBFS; USB 11/12 RMS: -25.66/-26.05 dBFS.
Compared with probe 02, deck audio remains strong and the presumed master
pair also remains strong (about 2–3 dB lower RMS, not a mute). Track content
varies across captures. This does not establish a post-fader deck tap or
confirm the master routing. Need user confirmation of actual main-output
response and physical channel before further conclusions or settings changes.

Evidence: `audio-probe-04.txt`, `audio-probe-04-errors.txt`,
`audio-probe-04-analysis.json`. Raw temporary file:
`/tmp/sixty-four-recording-probe-04.raw`. No continuous WAV claimed.

### Correction to probe 04

User subsequently clarified the fader was NOT down when they said ready.
Probe 04 must be treated as another fader-up baseline, not evidence of a
routing anomaly. Probe 03 movement timing remains uncertain.

## Probe 05: confirmed fader down

User explicitly stated "fader is down" before capture. Instructed to keep
track playing and fader down until completion. 500 batches / 40,000 nonempty
success-or-short microframes, 11,519,472 bytes. Both restoration checks passed.
No nonfinite samples under Float32 decoding.

| Pair | Probe 04 fader-up RMS dBFS | Probe 05 fader-down RMS dBFS |
|---|---|---|
| USB 3/4 (Deck 1) | -37.68 / -37.81 | -37.85 / -37.86 |
| USB 11/12 (master) | -25.66 / -26.05 | -110.44 / -110.51 |

Deck recording remains essentially unchanged while master falls to the quiet
noise floor (about 85 dB reduction). With the reported fader action, this is
strong evidence Deck 1's current recording tap is pre-fader and the master
recording follows that channel's fader. This does not yet validate selecting
the optional post-fader deck tap, the other decks, continuous capture or
CoreAudio application integration. No mixer recording-route settings changed.

Evidence: `audio-probe-05.txt`, `audio-probe-05-errors.txt`,
`audio-probe-05-analysis.json`; raw temporary file
`/tmp/sixty-four-recording-probe-05.raw`. Still a deliberately gapped packet
probe, not a continuous recording. Next routing test: manually select the
manual-documented Deck 1 post-fader record source in a coordinated step,
then repeat steady fader-up/down captures.

## Probe 06: post-fader option, fader up

After instructions to select REC DECK1 POST, user requested "Start post-fader
test — fader up." Actual menu selection is user-mediated, not read back by
the tool. 500 batches / 40,000 nonempty success-or-short microframes,
11,519,568 bytes; alternate/configuration restoration verified. Mixer record
source setting is not reverted by USB cleanup.

USB 3/4 RMS is -23.15/-22.99 dBFS, matching USB 11/12 to the reported
precision. Peaks also match (-7.88/-6.90 dBFS). No nonfinite samples.
This differs from the earlier pre-selector captures and is consistent with
the selected post-fader route. Fader-down comparison is still required to
validate its response; do not yet claim that gate complete.

Evidence: audio-probe-06.txt, audio-probe-06-errors.txt,
audio-probe-06-analysis.json; raw temporary data at
/tmp/sixty-four-recording-probe-06.raw. Same gapped diagnostic format.

## Probe 07: post-fader option, confirmed fader down

User explicitly requested post-fader test with fader down. Track held playing.
500 batches / 40,000 nonempty success-or-short microframes, 11,519,376 bytes;
alternate 0 and configuration 0 restoration verified. No nonfinite samples.

Full-capture RMS on USB 3/4: -72.21/-79.24 dBFS; USB 11/12:
-72.21/-79.24 dBFS. These averages include a brief first-batch transient
(cause not established). Only batch 1 of 500 had a peak above -60 dBFS on
these pairs. Excluding that batch, USB 3/4 is exactly digital zero and USB
11/12 is at -110.50/-110.55 dBFS RMS. Exclusion is explicit, not a claim
that startup was transient-free. Deck/master are not sample-identical.

Compared with probe 06 fader-up levels around -23 dBFS RMS, this validates
Deck 1 recording follows its fader with REC DECK1 POST selected, while the
master pair also mutes to the noise floor. Together with probe 05, it
establishes selectable pre/post-fader behavior for this deck on this setup.
Other deck routes, continuous streaming, playback, CoreAudio/Live integration
and MIDI remain unvalidated. The post-fader mixer setting remains selected.

Evidence: audio-probe-07.txt, audio-probe-07-errors.txt,
audio-probe-07-analysis.json, audio-probe-07-transients.json,
audio-probe-07-settled.json. Raw: /tmp/sixty-four-recording-probe-07.raw
(temporary, same deliberately gapped diagnostic format).
