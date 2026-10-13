# Sixty-Four playback diagnostics

Duplex bundle version 0.2.0 adds read-only telemetry for investigating intermittent
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
