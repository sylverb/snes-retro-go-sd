
#ifndef DSP1_HLE_H
#define DSP1_HLE_H

#include <stdint.h>
#include <stdbool.h>

/* DSP-1 coprocessor HLE (clean-room, from public protocol documentation:
 * snes.nesdev.org/wiki/DSP-1 for the register map and command word counts,
 * sneslab.net/wiki/DSP1 per-command pages for I/O shapes; the math itself is
 * standard fixed-point 3D — implemented fresh, no emulator code copied).
 *
 * The real chip is a NEC uPD77C25 running Nintendo's program ROM. Games talk to
 * it through two registers on the cart bus:
 *   DR (data): 16-bit transfers as two 8-bit accesses, LSB first. The host
 *              writes a command byte, then the command's parameter words, then
 *              reads its result words.
 *   SR (status): bit7 = RQM, 1 when the DSP is ready. Commands here execute
 *              instantly, so RQM is always 1.
 *
 * Mappings (snes.nesdev.org/wiki/DSP-1):
 *   LoROM: banks $30-$3f/$b0-$bf, DR $8000-$bfff, SR $c000-$ffff
 *   HiROM: banks $00-$0f/$80-$8f, DR $6000-$6fff, SR $7000-$7fff
 *
 * The saveload blob is the whole struct: keep it plain data (no pointers). */

typedef struct Dsp1 {
  uint32_t version;      /* savestate stamp */
  uint8_t state;         /* 0 idle (expect command), 1 reading params, 2 writing results, 3 raster stream */
  uint8_t cmd;
  uint8_t byteIdx;       /* byte position within current param/result stream */
  uint8_t inWords;
  uint8_t outWords;
  int16_t in[8];
  int16_t out[8];

  /* attitude matrices A/B/C, Q15, row-major */
  int16_t matrix[3][3][3];

  /* projection state (Parameter command), kept as raw inputs; raster/project/
   * target recompute from these */
  int16_t fx, fy, fz;    /* camera position (fz = height above ground) */
  int16_t lfe, les;      /* eye-to-screen, eye-to-ground reference distances */
  uint16_t aas, azs;     /* attack (pitch) and azimuth angles, 0x10000 = full turn */
  int16_t centerX, centerY;  /* ground point at screen centre (Cx, Cy result) */
  int16_t vof, vva;      /* raster offsets returned by Parameter */
  uint16_t rasterVs;     /* current scanline in raster streaming mode */

  uint32_t unknownCmds;  /* diagnostics: count of undocumented opcodes seen */
} Dsp1;

void dsp1_reset(Dsp1* d);
uint8_t dsp1_readDR(Dsp1* d);
void dsp1_writeDR(Dsp1* d, uint8_t val);
uint8_t dsp1_readSR(Dsp1* d);

#endif
