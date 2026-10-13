# Sixty-Four playback preparation — offline checkpoint

Prepared remotely on 2026-10-12 on `codex/sixty-four-playback-prep`.
**Playback is not enabled.** The installed recording driver is unchanged.
No USB traffic, driver installation, service restart, or audio settings changes
were performed for this checkpoint.

## Implemented

- `src/playback/SixtyFourPlayback.*`: a separate offline candidate library with
  no IOKit calls and no dependency from either HAL target.
- Candidate little-endian, interleaved Float32 encoding for 10 channels; 40 bytes
  per sample frame, up to 280 bytes per packet. Zero through seven frames fit
  the observed endpoint capacity. This is a hypothesis to test on OUT, not a
  claim that the IN encoding proves the OUT encoding.
- Bounded 4,096-frame single-producer/single-consumer queue. Only the consumer
  moves the read index. Overflow rejects the entire newest block, preserving
  already queued audio. No allocation or lock in push/read. Reset requires both
  callers stopped. A future HAL WriteMix callback must be the sole producer.
- Absolute sample positions reject overlapping/backward producer blocks, retain
  gaps as silence, and discard late frames only on the consumer side. Missing,
  late, rejected and invalid data are counted. These counters are diagnostic
  snapshots, not an atomically consistent multi-counter transaction.
- Eight-microframe packet assembly with separate actual lengths and zeroed
  unused storage. It requires explicit per-packet sample counts from the caller;
  it does not derive output timing from recording packets or assume six frames
  per microframe. Invalid packet plans do not consume queued audio.
- `SixtyFourOutputProfile.h`: prepared CoreAudio output metadata: 48 kHz,
  10-channel Float32 packed stream, starting channel 1, WriteMix operation,
  documented deck order 3/1/2/4 followed by FlexFX return. This is reusable
  metadata, not a registered output device or a finished HAL output callback.

Finite sample values are preserved without gain or clipping. Nonfinite blocks
are rejected before publication. A hardware diagnostic must apply an explicit
low test level and output setup; the codec itself is not a limiter.

## Evidence boundary

The official manual documents the ten playback channels and their routing.
The local descriptor fingerprint shows interface 1 alternate 1, isochronous OUT
endpoint 0x06 with 280-byte capacity and interval 1. Existing hardware recording
has established Float32 little-endian input, but playback representation,
initialization, packet cadence and clock coupling remain unverified.

No Sixty-Eight opcodes, implicit feedback policy or transport enable flag are
copied into this candidate library. A future duplex engine must share one device
owner and coordinate capture/output lifetimes; starting a separate output owner
beside the working recording driver would conflict with its idle-configuration
requirement.

## Offline validation

All 12 CTest tests pass. New tests cover:

- Golden Float32 byte sequences including negative zero, lane order and bounds;
  malformed lengths, insufficient space, null pointers and nonfinite samples.
- Whole-block overflow rejection, retry after space becomes available, late
  arrival, preserved timestamp gaps, reset and integer overflow boundaries.
- Variable 5/6/7/zero-frame packet plans, exact actual lengths and zero padding;
  invalid plan rejection without consuming queued frames.
- 100,000 frames through concurrent producer and consumer over repeated wraps,
  checking every channel for ordering/corruption.
- CoreAudio output format, direction, WriteMix operation and all ten channel
  labels against explicit expected values.

The playback tests also pass AddressSanitizer/UndefinedBehaviorSanitizer and a
separate ThreadSanitizer run with no reported errors. Reproduce offline:

```sh
cmake -S . -B /tmp/rane-playback-offline-build
cmake --build /tmp/rane-playback-offline-build -j 4
ctest --test-dir /tmp/rane-playback-offline-build --output-on-failure
clang++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer src/playback/SixtyFourPlayback.cpp tests/test_sixty_four_playback.cpp -o /tmp/rane-playback-asan
/tmp/rane-playback-asan
clang++ -std=c++17 -g -O1 -fsanitize=thread src/playback/SixtyFourPlayback.cpp tests/test_sixty_four_playback.cpp -o /tmp/rane-playback-tsan
/tmp/rane-playback-tsan
```

These are software tests; they provide no additional hardware playback evidence.

## When the user returns

1. Prepare a bounded, explicitly activated output diagnostic using the verified
   descriptors and a clearly labeled candidate clock/packet policy. It must stop
   on scheduling errors, retain callback buffers until drained, and restore USB
   settings. No automatic execution from build/tests or installation.
2. Arrange local monitoring at low level and identify the connected A/B source.
   Coordinate exclusive ownership with the recording HAL before opening USB.
   First validate zero-valued output scheduling, then a short low-level signal
   on one documented deck pair. Silence alone cannot confirm sample encoding.
3. Verify audible signal, channel routing, sample representation and stable
   packet timing. If these fail, retain the working recording-only installation
   and investigate the evidence instead of enabling outputs speculatively.
4. Implement the shared duplex USB session and actual HAL output stream/WriteMix
   path using measured timing. Respect output sample times, define prefill and
   latency, and track drift without silently pruning samples.
5. Validate all five pairs, then simultaneous Ableton playback and recording,
   stop/restart and disconnect recovery before deploying a duplex replacement.

The next hardware checkpoint remains one deck at a low test level. No user
presence is needed for the software work completed here.
