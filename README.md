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
```

Produces `snes.bin` → copy to `/cores/snes.bin`. ROMs under
`/roms/snes/` (`.sfc` `.smc` `.fig` `.swc`).

Requirements: `arm-none-eabi-gcc`, Make, Python 3 + Pillow; or Docker image
`sylverb/retro-go-sd-builder:v1.5`.

## Layout

| Path | Notes |
|------|--------|
| `src/main_snes.c` | Frame loop, present, audio stretch, savestate |
| `src/snes/` | LakeSnes + Thumb-2 engines |
| `snes_core.ld` | RAM_EMU + ITCM segments |
| `sdk/` | Vendored ABI bridge / headers / packer |

See `CLAUDE.md` for the general core memory map and porting checklist.
