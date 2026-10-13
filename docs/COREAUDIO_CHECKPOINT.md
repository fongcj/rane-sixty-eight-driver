# Sixty-Four CoreAudio input checkpoint

Implementation and installation: 2026-10-12. **Installed with explicit user
approval; macOS audio service restarted without rebooting the laptop. CoreAudio
now enumerates 12 inputs at 48 kHz and reports the device alive. The user
confirmed selecting the device and successfully recording audio in Ableton.**

## Implemented

`RaneSixtyFourInput.driver` is a separate native arm64 AudioServerPlugIn,
named **Rane Sixty-Four Recording**. It exposes one interleaved Float32 stream:
12 inputs at a fixed 48 kHz, no output stream, no gain controls and no MIDI.
It cannot become the system default audio device. Choose an existing output
device separately in Ableton. Playback and MIDI remain separate milestones.

| Ableton stereo input | Documented signal |
| --- | --- |
| 1/2 | Deck 3 |
| 3/4 | Deck 1 |
| 5/6 | Deck 2 |
| 7/8 | Deck 4 |
| 9/10 | FlexFX send |
| 11/12 | Main mix or Mic, according to the mixer setting |

The driver preserves the samples and channel order; the mixer's REC DECK
selection controls pre/post-fader routing. Installing this driver does not
change that selection. See `CHECKPOINT_2.md` and `CHECKPOINT_3.md` for measured
audio evidence. The mapping comes from the official Sixty-Four manual, p23,
and recording selection from p25; untested channels are not claimed verified.

The new USB engine reuses the validated descriptor fingerprint, Float32 decoder
and continuous recording scheduler. It accepts exactly one `1cc5:0017` device,
requires idle configuration 0, opens without seizing, selects configuration 1
and recording interface 2 alternate 1, and reads only endpoint 0x82. No
Sixty-Eight initialization, playback or HID commands are used.

64 outstanding one-millisecond reads are delivered in bus-frame order.
The real-time CoreAudio read callback copies from a 65,536-frame atomic history
without locking, allocating, accessing USB or consuming other readers' audio.
Missing/overwritten frames become silence and increment an internal counter.
No automatic resampling or channel gain is applied.

The clock uses actual decoded sample counts and USB bus/host-time anchors every
16,384 samples, meeting the SDK's minimum zero-timestamp period of 10,923.
The HAL's default clock filter handles the timestamp sequence. Within the batch
crossing an anchor, its host time is interpolated at nominal 48 kHz. This is
an initial clock implementation, not a calibrated fidelity claim. A 2,048-frame
input safety offset gives scheduling margin; this checkpoint prioritizes recording,
not low-latency software monitoring. Buffer requests of 64–1,024 frames use
the host's device-configuration callback before changing the advertised size.

First client starts capture; additional clients share it; the last stop aborts
and drains USB, restores alternate 0, closes the interface, then restores
configuration 0. Callback storage is retained if cancellation cannot drain.
Any cleanup failure blocks another open for the life of the driver process.
No device is opened merely by constructing the driver or querying properties.

## Validation completed

- Full repository build on this Apple Silicon Mac; new production targets use
  `-Wall -Wextra -Werror`.
- 10 CTest tests pass, including history wrap/restart/concurrent reads and HAL
  discovery, 12-channel format/labels, property bounds, no playback, fake
  multiple-client lifecycle, restart timestamp seed and failed-start handling.
- HAL contract tests compile source into a normal test executable and replace
  the USB capture implementation. They do not load a driver or open hardware.
- Bundle property list, arm64 executable, exported factory and ad-hoc signature
  are checked before installation. This is a local development build, not a
  notarized distribution.

## Remaining acceptance tests and limitations

1. macOS must load the bundle and allow its standard IOKit USB user clients in
   the audio host sandbox. No security relaxation is proposed if access fails.
2. Ableton must show all 12 inputs, selectable as six stereo pairs, at 48 kHz.
3. With the existing Deck 1 source/post-fader setup, record 3/4 and 11/12
   together for a short test. Check both waveforms, audible continuity and
   fader response. This validates the HAL clock and buffering in the real host.
4. Stop/restart recording and close/reopen Live. Verify USB settings restore
   and capture can restart. Cancellation/unplug behavior in the host is not
   yet hardware-tested. Long-duration reliability is deferred at user request.

This first build has a fixed logical device and no plug-in-owned hotplug
notification monitor. DeviceIsAlive is checked when queried, and loading is
conditioned on matching USB hardware. Connect before opening the audio app;
automatic reconnect/recovery is not an acceptance claim. Capture errors stop
the engine rather than guessing how to recover. If cleanup fails or the device
is unplugged mid-capture, a host restart may be needed. Persistent errors or
silence are a no-go for proceeding beyond this checkpoint: roll back and inspect
host logs/clock scheduling before adding playback or MIDI.

## Build and approved installation procedure

Build and sign (does not install or load):

```sh
cmake -S . -B /tmp/rane-checkpoint-build
cmake --build /tmp/rane-checkpoint-build -j 4
ctest --test-dir /tmp/rane-checkpoint-build --output-on-failure
codesign --force --sign - /tmp/rane-checkpoint-build/RaneSixtyFourInput.driver
codesign --verify --strict /tmp/rane-checkpoint-build/RaneSixtyFourInput.driver
```

**Explicit user approval is required before the next commands.** This follows
the user's original restriction on installing/loading drivers. Save work and
stop current audio first: restarting `coreaudiod` briefly interrupts Mac audio.

```sh
cp tools/sixty_four_hal.sh /tmp/rane-sixty-four-hal-install.sh
sudo bash /tmp/rane-sixty-four-hal-install.sh --install /tmp/rane-checkpoint-build/RaneSixtyFourInput.driver
```

Staging the script in `/tmp` also lets an administrator helper run it without
requiring access to a protected Documents folder.

The reviewed script refuses to replace an existing bundle, verifies its identity,
arm64 architecture and signature, copies into
`/Library/Audio/Plug-Ins/HAL/RaneSixtyFourInput.driver`, sets root ownership,
then restarts the macOS audio service. It changes no SIP/boot security settings,
firmware, system default device, or existing driver.

After loading, the intended Live setup is CoreAudio → Audio Input Device →
Rane Sixty-Four Recording → Input Config → enable desired stereo pairs.
Keep the existing output device, use 48,000 Hz, and initially use 512 frames.
Select Ext. In 3/4 for Deck 1 and Ext. In 11/12 for the selected Main/Mic source.
Avoid software monitoring for this first recording check. These are intended
acceptance steps, not a claim that Live has already enumerated the driver.

Rollback after approval:

```sh
sudo bash tools/sixty_four_hal.sh --rollback
```

Rollback moves only this identified bundle into a unique folder under
`/Library/Audio/Plug-Ins/HAL-disabled` and restarts `coreaudiod`. It retains the
bundle for inspection and does not remove other drivers.

## Installed-device enumeration — 2026-10-12

User explicitly approved installing the driver and restarting macOS audio.
The administrator helper could not read the installer inside Documents; running
an identical staged script from `/tmp` resolved that without security changes.
Corrected `lipo` argument order before deployment. The verified signed bundle
was installed at the exact path above and `killall coreaudiod` succeeded.

A standalone read-only CoreAudio query returned:

```text
Rane Sixty-Four Recording
inputs=12 outputs=0 rate=48000 alive=1
```

This query used the public HAL device list, stream configuration, nominal sample
rate and DeviceIsAlive properties. It did not start I/O or change any settings.
At this stage, the query verified real host discovery and input channel count;
recording was subsequently confirmed by the user below.

## Ableton recording confirmed by user — 2026-10-12

After installation, the user confirmed: “I recorded into ableton, seeing it as
an input device”. This verifies device selection and actual audio recording
through the installed native CoreAudio input driver on the M2 MacBook Pro.
The initial Ableton recording checkpoint is passed.

The exact recorded channel pair, simultaneous recording of all pairs, recording
duration and stop/restart behavior were not specified. This confirmation does
not establish those additional acceptance criteria, long-duration reliability,
playback, or MIDI support. The earlier standalone post-fader measurements remain
separate evidence; a post-fader comparison inside Ableton has not been reported.
