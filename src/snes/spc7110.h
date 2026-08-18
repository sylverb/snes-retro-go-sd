#ifndef SPC7110_H
#define SPC7110_H

#include <stdint.h>
#include <stdbool.h>

/* SPC7110 (Tengai Makyou Zero / Momotarou Dentetsu Happy / Super Power League 4).
 *
 * Chip provides:
 *   - HiROM program ROM in $C0-$CF and $00-$0F/$80-$8F:$8000-$FFFF (first 1 MB)
 *   - three 1 MB data-ROM windows at $D0/$E0/$F0 via $4831-$4833
 *   - fan-translation 7 MB dumps: 1 MB expansion ROM at $40-$4F (file +$600000)
 *   - arithmetic decoder (modes 0/1/2) feeding $4800 and bank $50
 *   - data-ROM port $4810, math unit $4820, optional RTC $4840
 *   - $4830 bit 7 enables the 8 KB cart SRAM at $00-$3F/$80-$BF:$6000-$7FFF
 *
 * Decompression algorithm: byuu + neviksti, ISC/public-domain (spc7110emu 0.03).
 *
 * Saveload: the struct is plain data (no pointers). Allocate in RAM_EMU on
 * the device — not DTCM. */

#define SPC7110_DECOMP_BUFFER_SIZE 64

typedef struct Spc7110 {
  uint32_t version;

  uint8_t r4801, r4802, r4803, r4804, r4805, r4806, r4807, r4808, r4809, r480a, r480b, r480c;
  uint8_t r4811, r4812, r4813, r4814, r4815, r4816, r4817, r4818, r481x;
  uint8_t r4814_latch, r4815_latch;
  uint8_t r4820, r4821, r4822, r4823, r4824, r4825, r4826, r4827;
  uint8_t r4828, r4829, r482a, r482b, r482c, r482d, r482e, r482f;
  uint8_t r4830, r4831, r4832, r4833, r4834;
  uint8_t r4840, r4841, r4842;

  uint32_t dx_offset, ex_offset, fx_offset;

  uint32_t decomp_mode;
  uint32_t decomp_offset;
  uint8_t decomp_buffer[SPC7110_DECOMP_BUFFER_SIZE];
  uint32_t decomp_buffer_rdoffset;
  uint32_t decomp_buffer_wroffset;
  uint32_t decomp_buffer_length;

  struct {
    uint8_t index;
    uint8_t invert;
  } context[32];

  /* Persistent decoder locals (were `static` in the original C++). */
  uint8_t d_val, d_in, d_span, d_buffer_index;
  uint8_t d_bitplanebuffer[16];
  int32_t d_out, d_out0, d_out1, d_inverts, d_lps, d_in_count;
  uint32_t d_pixelorder[16];

  uint8_t rtc_state;  /* 0 inactive, 1 mode, 2 index, 3 write */
  uint8_t rtc_mode;
  uint8_t rtc_index;
  uint8_t rtc_ram[16];
  uint8_t rtc_subsec; /* frames toward 1 second while the clock is running */
} Spc7110;

Spc7110 *spc7110_alloc(void);
uint32_t spc7110_size(void);
void spc7110_reset(Spc7110 *s);
void spc7110_tick(Spc7110 *s); /* one NTSC/PAL frame; advances RTC when not STOP'd */

uint8_t spc7110_mmio_read(Spc7110 *s, uint16_t addr, const uint8_t *rom, uint32_t romSize);
void spc7110_mmio_write(Spc7110 *s, uint16_t addr, uint8_t val, const uint8_t *rom, uint32_t romSize);

uint8_t spc7110_read(Spc7110 *s, uint32_t addr, const uint8_t *rom, uint32_t romSize);

#endif
