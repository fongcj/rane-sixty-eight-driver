# Sixty-Four installation and updates

This is an experimental Apple Silicon userspace CoreAudio driver and a separate
foreground CoreMIDI bridge. It requires no kernel extension or reduced macOS
security. The tested host is an M2 MacBook Pro on macOS 15.7.4.
Do not use `scripts/install.sh`: that script installs the Sixty-Eight products.
There is currently no packaged Sixty-Four installer or MIDI launch-at-login setup.

## Current development Mac

On 2026-10-13 the installed duplex bundle was verified as **0.2.2, build 4**.
Its executable SHA-256 exactly matched the tested build:
`982473051c0805fc91efddf2bdbdca5762010f2c0a519acc8a0f3aae0fad732b`.
No reinstall is needed for this checkpoint. Documentation, MIDI tools, and Git
changes do not require an audio-service restart. A stopped audio session or MIDI
bridge is not by itself a reason to reinstall.

## Build

Prerequisites: Xcode Command Line Tools and CMake. From the repository root:

```sh
cmake -S . -B build-sixty-four -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-sixty-four -j4
ctest --test-dir build-sixty-four --output-on-failure
codesign --force --sign - build-sixty-four/RaneSixtyFourDuplex.driver
codesign --verify --strict build-sixty-four/RaneSixtyFourDuplex.driver
```

The build and tests do not install drivers or run hardware probes. The build
also compiles inherited Sixty-Eight targets; install only the bundle named below.

## First audio installation

Close audio applications first. This command requires administrator authentication
and interrupts all audio by restarting `coreaudiod`. A full laptop restart is not
normally needed. Run only when ready to install:

```sh
sudo bash tools/sixty_four_duplex_hal.sh --install "$PWD/build-sixty-four/RaneSixtyFourDuplex.driver"
```

The helper validates the bundle identity, signature, and arm64 architecture. It
installs `/Library/Audio/Plug-Ins/HAL/RaneSixtyFourDuplex.driver` and refuses to
overwrite an existing bundle. Reopen the audio app and select **Rane Sixty-Four**.
Use 48 kHz. It exposes 12 inputs and 10 outputs. Deck 1 playback is channels 3/4;
set its mixer selector to 1A or 1B to match the connected USB port. Existing macOS
speaker assignments can override the default pair; see
[output routing](DUPLEX_CHECKPOINT.md). Installation does not choose your default
system device or change saved speaker assignments.

The old **Rane Sixty-Four Recording** bundle is an optional recording-only fallback,
not a prerequisite. Select the duplex device for normal use. This helper leaves
that fallback untouched; do not run competing audio sessions on both drivers.

## Update or disable

Build, test, and sign the replacement before stopping audio. To update an existing
installation, first archive it using the helper, then run the installation command
above. These are two separate service restarts:

```sh
sudo bash tools/sixty_four_duplex_hal.sh --rollback
sudo bash tools/sixty_four_duplex_hal.sh --install "$PWD/build-sixty-four/RaneSixtyFourDuplex.driver"
```

Despite its name, `--rollback` **disables and archives the current bundle**; it does
not automatically restore a previous version. It prints the retained path under
`/Library/Audio/Plug-Ins/HAL-disabled/`. Save that path. To restore it, disable any
currently installed replacement, then use `--install` with that exact archived
bundle path. Do not delete backups until the replacement is verified.

## MIDI use

No additional installed driver is required for the present MIDI prototype. Start
and verify audio through the duplex device first, then keep a Terminal open with:

```sh
./build-sixty-four/sixty_four_midi_bridge --run-existing-audio
```

Wait for READY and select **Rane Sixty-Four Controls** for application MIDI
mapping. This explicitly sends the tested startup report once. Ctrl-C stops the
bridge. It exits when its audio-owned HID session disappears; restart audio and
then the bridge. It does not start automatically after login, reconnect, or reboot.
No LED output, Serato native connection, hardware authorization, or DVS is supplied.
See [MIDI behavior and limits](MIDI_BRIDGE.md).

Optional read-only diagnostics:

```sh
./build-sixty-four/sixty_four_diagnostics
./build-sixty-four/inventory_sixty_four_midi --list
```

The existing local binaries under `/tmp/rane-playback-offline-build` can still be
used during this session, but `/tmp` is not a durable installation location.
Building in `build-sixty-four` provides a repeatable location without an audio reinstall.
