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
