# Checkpoint 2: Sixty-Four audio mapping and decoding

2026-10-12, local branch `codex/sixty-four-port`. Source implementation and
offline validation only. No HAL installation/loading, new USB capture,
firmware operation, audio setting change or MIDI command in this checkpoint.

## Implemented

- `src/protocol/sixty_four_audio.h` / `.c`: separate documented 12-input /
  10-output mapping, 48 kHz constants, channel labels/categories, and deck
  lookup. USB deck order stays 3,1,2,4. No sample reordering is required.
- Deck labels say Record rather than Post-fader because recording tap is a
  mixer setting. Last pair says Main / Mic to reflect selectable routing.
  Physical USB A/B cannot be inferred from the shared product ID.
- Checked Float32 little-endian input decoder uses actual microframe length,
  accepts observed 240/288/336-byte payloads, rejects malformed/oversized
  payloads and nonfinite samples, checks capacity, and publishes complete
  packets only. It preserves sample values, including finite values outside
  +/-1, without clipping. The older S24 codec is not used for this path.
- HAL channel and category getters now have an exact observed Sixty-Four
  identity branch; stream/model/device strings and UID translation distinguish
  it from the Sixty-Eight. Unknown Sixty-Four firmware is not reported as the
  Sixty-Eight's hardcoded version. Existing Sixty-Eight channel labels remain.
- Input completion contains a Sixty-Four decoder branch, separate from the
  existing Sixty-Eight decode/feedback branch.

## Deliberately not enabled yet

The observed Sixty-Four identity still has `currentTransportEnabled=false`.
Consequently the new HAL profile/decode branches are preparation for the
next transport checkpoint, not an available CoreAudio device. This build
must not be presented as a working Sixty-Four driver or installed for testing.

The existing transport couples audio IN/OUT and MIDI, uses a seize-first open,
and has lifecycle/clock limitations. Enabling its device lookup now would
activate unvalidated behavior. Next implementation must introduce recording-only
ownership (interface 2), descriptor validation, checked startup and cancellation,
restoration, sample-clock anchors, buffer accounting and hotplug handling.
It must also select identity before allocating per-device preferences/meters,
separate those resources and bundle identity, and define multi-device/port
identity before publication. This checkpoint does not claim those are solved.
Output names are documented topology, not playback support. MIDI remains gated.

## Offline validation

- Native arm64 HAL bundle built in `/tmp/rane-checkpoint-build`; not loaded.
- CTest 6/6 passed, including `test_sixty_four_audio` (checks remain active
  under NDEBUG). Distinct sample values test every lane across frames;
  malformed length, capacity, null, nonfinite, and empty-payload tests verify
  rejection without partially overwriting output.
- Built only the pure C codec as `/tmp/sixty-four-codec.dylib` and replayed
  saved probes 06/07 through it with Python ctypes. All 80,000 packets /
  479,978 sample frames matched independent Python little-endian Float32
  unpacking sample-for-sample. No hardware APIs are linked by this codec.
- `git diff --check` passed.

Rebuild/check (does not run hardware executables):

```sh
cmake -S . -B /tmp/rane-checkpoint-build
cmake --build /tmp/rane-checkpoint-build --target RaneHAL test_sixty_four_audio
ctest --test-dir /tmp/rane-checkpoint-build --output-on-failure
```

Hardware evidence remains in
[recording probe findings](hardware/sixty-four-usb-a/AUDIO_PROBE_FINDINGS.md).
