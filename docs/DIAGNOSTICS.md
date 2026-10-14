# Sixty-Four playback diagnostics

Duplex bundle version 0.2.0 and later provide read-only telemetry for investigating intermittent
choppiness. It does not change playback scheduling, buffer limits, sample format,
latency policy or system-output eligibility. This is instrumentation, not a fix
for the reported symptom.

## Collecting a report

Build `sixty_four_diagnostics` with the normal CMake build. After explicitly
approved installation of the new duplex bundle, run:

```sh
/tmp/rane-playback-offline-build/sixty_four_diagnostics
/tmp/rane-playback-offline-build/sixty_four_diagnostics --watch 60 > /tmp/rane-diagnostics.jsonl
```

The second command prints one snapshot per second for 60 seconds. Watch duration
is bounded to 1–3,600 seconds; Ctrl-C stops it. It queries only CoreAudio
properties: it does not start playback/recording, change settings, restart audio,
open USB or save any audio. Each line is JSON with host wall time and a versioned
snapshot. No client names, serial numbers, file paths or audio samples are included.

Keep Ableton's current working buffer setting (last observed: 512 frames / 48 kHz).
If choppiness recurs, note the wall time and whether you had just started playback,
changed buffer size or opened another audio app. Save a short report spanning
before/after the event. No need to force a long reliability test.

Before the update, the tool correctly reported that the installed older bundle
had no `r64d` property rather than displaying invented zero counters. Version
0.2.0 was subsequently installed with explicit user approval (see below).
Live counter validation during playback is still pending. Existing install
scripts refuse overwrites; preserve the old bundle before deploying an update.

## Reading the fields

| Fields | Interpretation |
| --- | --- |
| `schema`, `session`, `session_after` | Schema 1; session attempt number. Discard snapshots spanning a session change. |
| `state` | 0 not started/starting; 1 ready; 2 cleanly stopped; -1 failed. |
| `buffer_frames`, `buffer_changes_lifetime` | Current configured buffer and number of applied changes since driver load. |
| `input_safety_frames`, `output_safety_frames` | Advertised safety offsets, separate from the buffer size. |
| `input_missing_frames` | Requested input frames unavailable in the sample history. Repeated readers can count the same missing position. |
| `output_startup_missing_frames` | Requested output positions with no data before the consumer first delivers a queued frame. |
| `output_post_start_missing_frames` | Missing positions after first delivered frame; useful for underrun/gap investigation. Explicit queued silence is data, not missing. |
| `output_missing_frames` | Startup and post-start missing positions combined. Silence is substituted for these positions. |
| `output_late_frames` | Queued frames discarded because their sample position has already passed. |
| `output_rejected_frames`, `output_invalid_blocks` | Entire blocks rejected for queue capacity/position/nonfinite data. Repeated producer retries are counted repeatedly. |
| `queue_high_water_frames` | Largest producer-observed occupancy (capacity 4,096). Conservative observation, not an instantaneous synchronized queue length. |
| `write_calls`, `write_failures`, `invalid_write_times` | Engine write calls, failures and malformed sample-time arguments. |
| `max_write_gap_ns` | Largest gap between write calls; compare with actual workload and configured buffer, not a fixed glitch threshold. |
| `max_input_callback_gap_ns`, `max_output_callback_gap_ns` | Largest USB callback gaps. Callbacks can arrive in bursts; gaps alone do not prove dropped samples. |
| `input_batches`, `output_batches`, `input_frames`, `output_frames` | Successfully delivered input / completed output totals. Output is intentionally delayed; totals need not match at a snapshot or cancellation. |
| `input_schedule_faults`, `output_schedule_faults`, `output_slot_busy` | Schedule/clock query failures, failed output preparation or unavailable transfer storage. |
| `input_transfer_errors`, `output_transfer_errors`, `decode_errors` | Rejected USB completions/submissions or malformed/status-failed input microframes. Normal short IN packets are not errors. |
| `worker_stalls`, `start_failures`, `cleanup_failures` | No input progress, failed initialization, or restoration/drain failure. |
| `last_failure_stage`, `last_ioreturn` | Most recent recorded failure stage and optional unsigned IOReturn; zero code means no code recorded. Stage 1=start, 2=input schedule, 3=input transfer/decode, 4=output schedule, 5=output transfer, 6=stall, 7=cleanup. |
| `invalid_io_blocks_lifetime`, `max_output_block_frames_lifetime` | Invalid HAL buffer/pointer requests and largest output block seen since load; helps detect requests outside the advertised bounds. |

Counters and maxima reset before each new transport session unless suffixed
`_lifetime`. Property reads do not reset them. Snapshots use independent atomic
loads: they are approximate, not a transaction. Even matching session numbers
cannot guarantee every field was sampled at exactly the same instant. Compare
nearby snapshots in a stable session. Startup, tail silence and an app ceasing
writes can produce expected missing frames; correlate with what was audible.

## Implementation and validation

The device advertises `r64d` through AudioServerPlugIn's custom-property list as
a read-only CFString. JSON formatting/allocation occurs only in the non-realtime
property getter. Audio paths update fixed-size lock-free counters and timestamps;
there is no new per-buffer logging, file I/O, allocation or mutex. Existing
failure messages are unchanged. No background monitor or service is installed.

13 CTest tests pass, including telemetry startup/post-start separation, queue
high-water/reset, read-only property size/bounds, buffer-change reporting, and
existing buffer/codec/HAL behavior. Playback tests pass ASan/UBSan and TSan.
The new telemetry has not been used to diagnose an actual choppy episode yet.


## Installed verification — 2026-10-13

User explicitly approved updating the duplex driver and restarting macOS audio.
The signed arm64 0.2.0 bundle was staged and verified, and the previous bundle
was preserved at:

`/Library/Audio/Plug-Ins/HAL-disabled/sixty-four-before-diagnostics.YgXmVI/RaneSixtyFourDuplex.driver`

Installation and `coreaudiod` restart succeeded without a laptop reboot or
security changes. A read-only native query returned valid schema-1 JSON with
session=0, state=0, buffer_frames=512, sample_rate=48000 and zero counters. No
stream had started since restart; zeros do not establish glitch-free playback.
CoreAudio also reported 12 inputs, 10 outputs and alive=1. The query did not
start playback/recording or change settings. Counter validation during ordinary
Ableton playback remains the next check; no continuous monitor was left running.

## 0.2.2 scheduling investigation (built, not installed)

During the CoreMIDI test the user confirmed an audio cutoff. Session 5 ended at
failure stage 4 with a maximum input callback gap of 90.45 ms. The duplex
transport schedules output 64 USB milliseconds after the corresponding input;
a missed output deadline is consistent with these observations, but 0.2.1's
stage-4 counter also covers clock-query and packet-preparation failures. A
stage-4 failure also occurred before the MIDI bridge attached. Causation by
MIDI has not been established.

The 0.2.2 candidate requests USER_INTERACTIVE QoS for the independent USB worker
before opening transfers. It previously used an ordinary std::thread without
an explicit scheduling request. QoS is advisory, not realtime deadline
scheduling, and is not a verified cure for these dropouts. Timing offsets,
buffer sizes and fail-closed behavior are unchanged.

New additive schema-1 fields distinguish the failure paths:

- `output_deadline_misses`: target USB frame was at or behind the current frame.
- `max_output_deadline_late_bus_frames`: largest lateness in 1-ms USB frames;
  equality is a miss with zero lateness.
- `output_clock_failures`: output bus-clock query errors; actual IOReturn is
  now retained in `last_ioreturn`.
- `output_prepare_failures`: packet preparation rejected the batch.
- `worker_qos_result`: return from the scheduling request (zero is success).

Installing this candidate and restarting CoreAudio requires explicit approval.
Acceptance requires a live audio-only baseline followed by audio+MIDI under
comparable use, with no new scheduling faults and user-confirmed continuous
sound. If it still fails, the new counters narrow the cause; do not keep raising
latency or restarting silently. The installed driver remains 0.2.1 until approved.

### Approved installation completed — 2026-10-13

The user approved the 0.2.2 update and completed the macOS administrator dialog.
Installed the arm64 ad-hoc-signed bundle and restarted `coreaudiod`. Strict
signature verification passed. The previous 0.2.1 bundle is preserved at:

`/Library/Audio/Plug-Ins/HAL-disabled/sixty-four-before-qos.2p3b0I/RaneSixtyFourDuplex.driver`

The installed plist reports 0.2.2. A live CoreAudio query returned the new
scheduling diagnostic fields, confirming the new implementation loaded.
The initial snapshot was session 0/state 0: no audio stream had started yet.
Its zero counters, including `worker_qos_result`, therefore do not validate the
QoS request or dropout mitigation. Playback validation is pending. The MIDI
bridge remains stopped for the audio-only baseline. No laptop restart or
security setting change was performed.

### Initial 0.2.2 live baseline

The user switched output to internal speakers and back to Rane, then confirmed
sound. Session 1 accepted the worker QoS request (`worker_qos_result=0`).
A 30-second audio-only watch followed by a 30-second watch with the foreground
MIDI bridge running stayed in state 1. Both completed with zero scheduling
faults, transfer errors, missed output deadlines, rejected output frames,
post-start missing output frames, and write failures. Startup missing frames
remained constant at 5477. At the end the session had processed about 127
seconds of input; maximum input callback gap was 13.50 ms and output callback
gap 22.56 ms. This short check supports the mitigation but is not a long-run
reliability guarantee or controlled proof of the original cause.

The MIDI bridge remained running after the check. No new button sequence was
requested; actual five-pair CoreMIDI delivery was validated in the previous test.
