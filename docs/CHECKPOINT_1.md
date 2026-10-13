# Checkpoint 1: fail-closed device selection

2026-10-12. Code preparation and read-only OS inventory only.

- Replaced wildcard-like USB matching with exact VID/PID matching.
- Separated candidate model metadata from permission to use the existing transport. Sixty-Four, Sixty-Two and SL4 metadata cannot activate the Sixty-Eight transport through `findDevice`.
- Corrected Sixty-Four metadata to 12 inputs / 10 outputs. Its candidate PID remains unverified on hardware.
- Added an offline regression test covering exact identity, vendor/product combinations, every vendor for PID 4/8, unknown IDs, channel metadata and disabled transport for other models. Checks remain enabled under NDEBUG.

The existing Sixty-Eight table entries remain enabled; this preserves their previous intended availability rather than independently verifying them. Descriptor fingerprint validation is still future work. Do not install this build as a Sixty-Four driver.

Host inventory: macOS 15.7.4 (24G517), arm64. `system_profiler SPUSBDataType -json` and the IOUSB registry showed host controllers but no attached mixer. This establishes only that the mixer was not visible during the query, not whether a cable, port or mixer is faulty.

Validation: native arm64 Debug build of the four offline tests and static engine library succeeded. All four CTest tests passed. Engine code was compiled only, never executed. No HAL or MIDI agent was loaded, and no USB configuration, hardware commands, firmware or audio settings were changed.

The original feasibility/comparison documents describe the upstream baseline; the matching and metadata defects recorded there are addressed by this checkpoint. Transport, sample framing, clocking and MIDI compatibility remain unverified.

Next: connect the powered mixer during an idle session, identify port A/B, and repeat read-only inventory. Do not run the existing selftest, stress test or manager for discovery.
