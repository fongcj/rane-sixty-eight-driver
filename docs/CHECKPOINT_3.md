# Checkpoint 3: continuous recording transport (in progress)

Local branch: `codex/sixty-four-continuous-recording`, based on checkpoint 2.
The first implementation piece is `src/transport/RecordingSchedule.h`:
a pure single-owner scheduler for 16 outstanding one-millisecond reads
(each eight high-speed microframes). It reserves consecutive USB frame
numbers, holds buffer ownership until completion, rejects duplicate/stale
completion tickets, and refuses restart while callbacks remain pending.
Late scheduling and submission failures stop the session rather than
concealing gaps. This policy is intentionally conservative for validation.

Offline tests cover 100,000 consecutive resubmissions, out-of-order and
stale completions, duplicate completion after slot reuse, submission failure,
late scheduling, overflow, and stop/drain/restart. Seven CTest tests pass.
No new hardware operation or driver loading was performed.

Remaining before the checkpoint is complete:
- Integrate with a standalone recording-only IOUSBLib owner, exact descriptor
  validation, checked configuration/alternate transitions, and restoration.
- Keep completion data in scheduled order, including out-of-order callbacks.
- Decouple storage from completion handling; account for every buffer drop,
  per-microframe status, discontinuity and short payload.
- Record sample counts and clock anchors; prove timing and continuity rather
  than just successful packet receipt.
- Drain cancellation callbacks before freeing buffers, handle unplug and
  error paths, and produce reviewable run/rollback instructions.
- Validate a continuous recording with the user's source before HAL integration.

Scheduler simulation is not evidence of real-time USB performance or a
completed recording transport. No publication/merge of this follow-on branch
is implied by creating the checkpoint-2 draft PR.

## Standalone transport implementation — 2026-10-12

Added `record_sixty_four_continuous`, independent of the HAL and legacy
transport. It requires `--record-30s NEW_12_CHANNEL_WAV` to access hardware.
Exact observed VID/PID and configuration bytes are checked before a
non-seizing open; starting configuration must be 0. Only recording interface
2 selects alternate 1, with endpoint direction/type/size/interval checked.
The descriptor fingerprint is shared with the earlier diagnostic tools.

16 outstanding one-millisecond reads cover a consecutive 30,000-frame USB
schedule. Completion callbacks decode each microframe's actual bytes and
place each batch at its scheduled index in preallocated memory (approximately
81 MB). This permits out-of-order completion without reordering stored audio.
There is no disk I/O, allocation or producer/consumer ring in the callback.
The bounded memory approach is a diagnostic design, not an unlimited recorder.

Bad completion status, malformed/empty microframes, nonfinite samples,
duplicate/missing batches, submission errors, late scheduling, interruption,
or timeout prevent a success WAV. A complete capture must also have a sample
count within 0.5% of 30 seconds at 48 kHz. This generous diagnostic count gate
is not a drift-correction or fidelity guarantee. Successful short packets are
counted separately from errors. Printed diagnostics include batch count,
packet errors, scheduling faults, sample count, first bus frame and callback
wall duration. Per-batch host completion ticks are retained in memory; these
are completion timestamps, not a precise device/host clock model.

Stop disables resubmission, aborts outstanding reads, and drains callbacks
before resource release. Recording alternate 0 and configuration 0 are
restored and verified. If cancellation does not drain within two seconds,
USB resources and buffers remain allocated until standalone process exit;
this is an explicit failure, not reusable production lifecycle handling.
Unplug/forced termination can prevent restoration; reconnect during idle if
restoration is not verified. No audio output, HID command, arbitrary vendor
request, seize fallback or installer is present.

After successful capture AND cleanup, output is written as a new 12-channel
48 kHz IEEE Float32 WAV in documented wire order, with a fact sample count.
Existing files are refused. A disk-write failure leaves an explicitly reported
incomplete file. Playback of this WAV is not automatically started.

### Offline checks

- Eight CTest tests passed. New buffer tests cover reverse-order completion,
  duplicates, missing batches, bad lengths, NaNs, and requested-size padding.
- Continuous tool builds with `-Wall -Wextra -Werror` for native arm64.
- Explicit `--offline-fixture NEW_WAV` mode calls no USB open/run functions.
  A reverse-arrival fixture produced exactly 1,440,000 12-channel frames.
  Independent Python checks verified the RIFF/fmt/fact/data sizes, format,
  sample rate and every sample, then verified an existing file was unchanged
  after a refused overwrite. Fixture removed after verification.
- Existing bounded diagnostic tools rebuilt after sharing the descriptor.

### Next coordinated hardware test

Keep current USB port/routing, REC DECK1 POST and a steady source on Deck 1,
fader up. No physical interaction is needed during the 30-second run. Example
command (active hardware operation, not part of CTest):

```sh
/tmp/rane-checkpoint-build/record_sixty_four_continuous --record-30s /tmp/sixty-four-continuous-01.wav
```

Check exit status, all 30,000 batches, zero packet/scheduling faults, plausible
sample count and both restoration results. Independently inspect WAV format,
channel levels and continuity before claiming success. This implementation
has NOT yet run on the mixer. HAL activation, indefinite streaming, precise
clock anchoring and physical unplug testing remain outstanding.
