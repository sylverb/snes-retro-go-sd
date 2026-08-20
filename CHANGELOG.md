# Changelog

When you cut a release:

1. Move items from `[Unreleased]` into a new `## [vX.Y.Z] - YYYY-MM-DD` section.
2. Commit the changelog update.
3. Push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`

CI reads the matching section and uses it as the GitHub Release notes. Assets
attached to the release:

- `<binary>-<tag>.zip` — SD layout only (`cores/` + packed `.bin`)
- `<binary>-<tag>-debug.zip` — ELF + linker map (use `arm-none-eabi-addr2line` for crash PC/LR → function/line)

## [Unreleased]

## [v0.0.1] - 2026-08-12

Initial public release.

### Install

- Unzip the release archive onto the SD card root (`cores/snes.bin`).
- Place ROMs under `/roms/snes/`
