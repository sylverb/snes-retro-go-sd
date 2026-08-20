# SNES Retro-Go SD Core (LakeSnes)

Standalone **SNES** dynamic core for
[Game & Watch Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd),
built from the freestanding Cortex-M7 core template (ABI via
`gw_firmware_abi_t`).

Based on the jshsakura LakeSnes port, adapted to this
firmware’s memory model:

| Region | Role in this core |
|--------|-------------------|
| **ITCM** (~54 KiB) | Thumb-2 65816 engine (+ bus helpers) |
| **DTCM** (`dtc_*`) | Hot control structs (Snes/Cpu/Dma/Ppu/Spc/Dsp/…) |
| **AHB** (`ahb_*`) | Leftover newlib heap only (~56 KiB) — **not** used for Apu |
| **RAM_EMU** | Core image + WRAM/VRAM/cart SRAM + **Apu/ARAM** (`ram_*`) |

`SCALING_OFF` renders into the firmware LCD framebuffer (no private 155 KiB
FB). FULL/FIT allocate a staging FB via `ram_malloc` on first use.

## Build

```bash
make          # or: make docker
make host     # Linux/macOS SDL preview (same src/main_snes.c)
```

Produces `snes.bin` → copy to `/cores/snes.bin`. ROMs under
`/roms/snes/` (`.sfc` `.smc` `.fig` `.swc`).

Requirements: `arm-none-eabi-gcc`, Make, Python 3 + Pillow; or Docker image
`sylverb/retro-go-sd-builder:v1.5`.

**Host preview** (optional): native `cc`, pkg-config, SDL2 (`brew install sdl2`
or `libsdl2-dev`). On macOS if pkg-config fails:
`export PKG_CONFIG_PATH="$(brew --prefix)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"`.

```bash
make host
./snes_host /path/to/game.sfc   # Esc / close window to quit
# F1 = save state, F2 = load state → ./host_saves/
```

Host uses the C interpreters (no Thumb-2 ASM). Scale with `HOST_SCALE=2`
(default); SDL3 via `make host HOST_SDL=3`.

## Layout

| Path | Notes |
|------|--------|
| `src/main_snes.c` | Frame loop, present, audio stretch, savestate |
| `src/snes/` | LakeSnes + Thumb-2 engines |
| `host/` | SDL shim for desktop preview |
| `snes_core.ld` | RAM_EMU + ITCM segments |
| `sdk/` | Vendored ABI bridge / headers / packer |

See `CLAUDE.md` for the general core memory map and porting checklist.

## Releases (GitHub tags `v*`)

Pushing a tag `vX.Y.Z` (with a matching `## [vX.Y.Z]` section in
`CHANGELOG.md`) creates a GitHub Release with **two** zip assets only:

| Asset | Contents |
|-------|----------|
| `snes-vX.Y.Z.zip` | SD layout: `cores/snes.bin` |
| `snes-vX.Y.Z-debug.zip` | `snes_core.elf`, linker `.map`, and a short README |

Unzip the install archive onto the SD card root. For a crash PC/LR:

```bash
unzip snes-v1.0.0-debug.zip
arm-none-eabi-addr2line -e snes_core.elf -f -C -a 0x<PC> 0x<LR>
```

Needs `arm-none-eabi-addr2line` on `PATH`, or the `sylverb/retro-go-sd-builder`
Docker image. From a repo checkout you can also use
`python3 scripts/resolve_addr.py --elf …` — see the README inside the debug zip.
