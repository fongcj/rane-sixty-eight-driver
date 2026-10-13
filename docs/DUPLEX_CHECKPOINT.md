# Sixty-Four duplex checkpoint — 2026-10-13

Status: duplex driver installed with explicit user approval; CoreAudio enumerates
12 inputs and 10 outputs at 48 kHz. The working recording-only driver remains
installed and unchanged as a fallback. Actual Ableton duplex playback remains
the next application-level acceptance test.

## Hardware evidence

USB A, exact descriptor fingerprint, interface 1 alternate 1 / OUT 0x06:

- Five-second silence test: 5,000/5,000 batches, no packet/scheduling errors,
  alternate/configuration restoration verified.
- Five-second tone test: identical successful transfer/cleanup counts. User
  confirmed a **clean tone** in Deck 1 headphone cue with source 1A. Candidate
  payload was interleaved little-endian Float32, 10 channels, six sample frames
  per microframe, with a 440 Hz -42 dBFS signal only on lanes 3/4.

This supports the playback format and Deck 1 routing for a short nominal-rate
experiment. It does not verify all destinations or long-duration clocking.

## Shared transport and HAL

`SixtyFourDuplex` owns one device and both audio interfaces. The known
recording-only implementation stays separate for rollback. Input transfers are
ordered by USB bus frame. Each decoded input batch supplies eight sample counts
to an output batch scheduled **64 USB milliseconds later**, with at most seven
frames per microframe. Output sample positions are input positions plus 3,072
frames. This explicitly tests a delayed input-count clocking hypothesis; endpoint
similarity or successful silence transfers do not establish that hypothesis.

An output batch is packed contiguously according to actual requested lengths;
280-byte storage-slot padding is not sent between shorter packets. 128 output
slots protect storage until completion. Both interfaces are aborted/drained,
restored to alternate 0 and closed before configuration 0 is restored. Cleanup
failure blocks another open in that process; undrained DMA storage is retained.

The prepared `RaneSixtyFourDuplex.driver` exposes **Rane Sixty-Four** with 12
recording inputs and 10 playback outputs at 48 kHz. One shared clock is derived
from received samples. The output WriteMix callback queues audio by CoreAudio's
output sample time; it does not consume/shift audio as an unlabelled FIFO.
Missing frames become silence. First-client/last-client lifecycle controls the
shared session. Input safety offset is 2,048 frames; output is conservatively
4,096 frames. Low-latency monitoring and automatic hotplug recovery are not
claims of this checkpoint.

Select this same duplex device for **both input and output** when testing Live.
Do not combine it with the separate recording-only device: both would compete
for the same physical USB device. The existing recording bundle is retained as
fallback, with distinct device/bundle identifiers. Neither is a default system
audio device.

## Validation

13 offline tests pass, including both recording-only and duplex HAL contracts.
The new contract test verifies two direction-specific streams, 12/10 channel
formats, labels, WriteMix sample-time forwarding, wrong-direction rejection,
client lifecycle and cleanup state. Both bundles build with warnings as errors.
The duplex bundle is arm64, locally ad-hoc signed, and its plist/signature verify.

The opt-in `probe_sixty_four_duplex --tone-deck1-6s` exercises the actual shared
engine without installing a driver. It queues a bounded low-level tone,
reads all 12 inputs by sample position, checks finite samples, missing input,
engine health and cleanup. Startup/tail output silence is intentional. A clean
short run is not a claim of long-term drift/reliability performance.

## Installation checkpoint (explicit approval required)

After the shared-engine hardware check, install only after user approval:

```sh
cp tools/sixty_four_duplex_hal.sh /tmp/rane-sixty-four-duplex-install.sh
sudo bash /tmp/rane-sixty-four-duplex-install.sh --install /tmp/rane-playback-offline-build/RaneSixtyFourDuplex.driver
```

The scoped script verifies identity/arm64/signature, refuses overwrites,
installs `/Library/Audio/Plug-Ins/HAL/RaneSixtyFourDuplex.driver` and restarts
`coreaudiod`. Existing audio streams are interrupted; no laptop reboot, firmware
change or security reduction is part of this procedure.

Rollback:

```sh
sudo bash /tmp/rane-sixty-four-duplex-install.sh --rollback
```

This moves only the duplex bundle to a unique HAL-disabled folder and restarts
macOS audio. The previous recording-only bundle stays installed. Selecting
Rane Sixty-Four Recording restores the previous Live input configuration.


## Shared-engine hardware result — 2026-10-13

On USB A, the six-second `--tone-deck1-6s` run exited successfully:

```text
Playback alternate restored: yes
Recording alternate restored: yes
Configuration restored: yes
queued=240000 read=278528 input-missing=0 late-output=0 write-attempts=3223
```

All input samples inspected were finite. Deck 1 inputs 3/4 peaked at 0.0450684;
Main/Mic inputs 11/12 peaked at approximately 0.04507. Other deck inputs were
near the noise floor; FlexFX inputs 9/10 were zero. Peaks alone do not establish
unity gain or sample-accurate loopback fidelity. The user confirmed the tone
was **clean and steady** while both directions operated.

The queued count includes the probe's leading/trailing silence. More write
attempts than accepted blocks reflect bounded producer retries while the queue
was full, not discarded successfully queued signal. No late queued frames or
missing requested recording frames were observed. This supports the delayed
input-count output policy for this short test; sustained clock stability,
other playback destinations and actual CoreAudio WriteMix delivery remain
unverified. Installing the duplex bundle for an Ableton test is the next
approval checkpoint. The original recording driver remains unchanged.


## Duplex installation and enumeration — 2026-10-13

User explicitly requested installation and completed the native administrator
prompt. Installed the signed `RaneSixtyFourDuplex.driver` bundle and restarted
`coreaudiod` successfully; no laptop reboot or security changes were required.
A read-only query of the public CoreAudio device list returned:

```text
Rane Sixty-Four
inputs=12 outputs=10 rate=48000 alive=1
```

No CoreAudio stream was started by this query. The next Live check selects
**Rane Sixty-Four for both input and output**, enables desired stereo pairs,
and routes a quiet track directly to Ext. Out 3/4 for Deck 1 on USB 1A.
The separate **Rane Sixty-Four Recording** device is the fallback and should
not be selected concurrently with the duplex device. Software monitoring on
return recording tracks should be off for the first test to avoid a feedback
loop. Full-duplex Live operation and other output pairs are not yet reported
verified at this point.
