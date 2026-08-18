/* S-DD1 graphics decompression chip emulation.
 *
 * The decompression algorithm is by Andreas Naive (August 2003, public domain).
 * This is a clean C port of his sdd1emu algorithm with the bsnes MMIO/DMA
 * interception logic adapted to LakeSnes's cart-read architecture.
 *
 * S-DD1 games (Star Ocean, Street Fighter Alpha 2) are HiROM with a 4 MB ROM.
 * The chip provides:
 *   - MMC: $4804-$4807 select which 1 MB ROM segment each of the four
 *     $C0-$CF/$D0-$DF/$E0-$EF/$F0-$FF windows sees.
 *   - Decompression: $4800 enables per-channel decompression; $4801 arms it.
 *     When a DMA channel reads from the ROM through the MMC window, the chip
 *     feeds decompressed bytes instead. */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "sdd1.h"
#include "snes_gnw_alloc.h"
#ifdef TARGET_GNW
#include "gw_malloc.h"
#endif
#ifdef HOST_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

#define SDD1_VERSION 1

/* ---------- raw ROM read through the MMC (no decompression) ---------- */

static inline uint8_t sdd1_rom_read(const Sdd1 *s, uint32_t addr,
                                    const uint8_t *rom, uint32_t romSize) {
  uint32_t mapped = s->mmc[(addr >> 20) & 3] + (addr & 0x0fffff);
  if (mapped < romSize) return rom[mapped];
  return 0;
}

/* ========== Decompression engine (Andreas Naive's algorithm) ========== */

typedef uint8_t bool8;

typedef struct {
  uint32_t byte_ptr;
  uint8_t bit_count;
  const Sdd1 *sdd1;
  const uint8_t *rom;
  uint32_t romSize;
} SDD1_IM;

static inline uint8_t IM_readByte(SDD1_IM *im, uint32_t addr) {
  return sdd1_rom_read(im->sdd1, addr, im->rom, im->romSize);
}

static void IM_prepareDecomp(SDD1_IM *im, uint32_t in_buf) {
  im->byte_ptr = in_buf;
  im->bit_count = 4;
}

static uint8_t IM_getCodeword(SDD1_IM *im, uint8_t code_len) {
  uint8_t codeword = (uint8_t)(IM_readByte(im, im->byte_ptr) << im->bit_count);
  im->bit_count++;
  if (codeword & 0x80) {
    codeword |= (uint8_t)(IM_readByte(im, im->byte_ptr + 1) >> (9 - im->bit_count));
    im->bit_count += code_len;
  }
  if (im->bit_count & 0x08) {
    im->byte_ptr++;
    im->bit_count &= 0x07;
  }
  return codeword;
}

static const uint8_t run_count[256] = {
  0x00,0x00,0x01,0x00,0x03,0x01,0x02,0x00,
  0x07,0x03,0x05,0x01,0x06,0x02,0x04,0x00,
  0x0f,0x07,0x0b,0x03,0x0d,0x05,0x09,0x01,
  0x0e,0x06,0x0a,0x02,0x0c,0x04,0x08,0x00,
  0x1f,0x0f,0x17,0x07,0x1b,0x0b,0x13,0x03,
  0x1d,0x0d,0x15,0x05,0x19,0x09,0x11,0x01,
  0x1e,0x0e,0x16,0x06,0x1a,0x0a,0x12,0x02,
  0x1c,0x0c,0x14,0x04,0x18,0x08,0x10,0x00,
  0x3f,0x1f,0x2f,0x0f,0x37,0x17,0x27,0x07,
  0x3b,0x1b,0x2b,0x0b,0x33,0x13,0x23,0x03,
  0x3d,0x1d,0x2d,0x0d,0x35,0x15,0x25,0x05,
  0x39,0x19,0x29,0x09,0x31,0x11,0x21,0x01,
  0x3e,0x1e,0x2e,0x0e,0x36,0x16,0x26,0x06,
  0x3a,0x1a,0x2a,0x0a,0x32,0x12,0x22,0x02,
  0x3c,0x1c,0x2c,0x0c,0x34,0x14,0x24,0x04,
  0x38,0x18,0x28,0x08,0x30,0x10,0x20,0x00,
  0x7f,0x3f,0x5f,0x1f,0x6f,0x2f,0x4f,0x0f,
  0x77,0x37,0x57,0x17,0x67,0x27,0x47,0x07,
  0x7b,0x3b,0x5b,0x1b,0x6b,0x2b,0x4b,0x0b,
  0x73,0x33,0x53,0x13,0x63,0x23,0x43,0x03,
  0x7d,0x3d,0x5d,0x1d,0x6d,0x2d,0x4d,0x0d,
  0x75,0x35,0x55,0x15,0x65,0x25,0x45,0x05,
  0x79,0x39,0x59,0x19,0x69,0x29,0x49,0x09,
  0x71,0x31,0x51,0x11,0x61,0x21,0x41,0x01,
  0x7e,0x3e,0x5e,0x1e,0x6e,0x2e,0x4e,0x0e,
  0x76,0x36,0x56,0x16,0x66,0x26,0x46,0x06,
  0x7a,0x3a,0x5a,0x1a,0x6a,0x2a,0x4a,0x0a,
  0x72,0x32,0x52,0x12,0x62,0x22,0x42,0x02,
  0x7c,0x3c,0x5c,0x1c,0x6c,0x2c,0x4c,0x0c,
  0x74,0x34,0x54,0x14,0x64,0x24,0x44,0x04,
  0x78,0x38,0x58,0x18,0x68,0x28,0x48,0x08,
  0x70,0x30,0x50,0x10,0x60,0x20,0x40,0x00,
};

typedef struct {
  SDD1_IM *IM;
} SDD1_GCD;

static void GCD_getRunCount(SDD1_GCD *gcd, uint8_t code_num,
                             uint8_t *MPScount, bool8 *LPSind) {
  uint8_t codeword = IM_getCodeword(gcd->IM, code_num);
  if (codeword & 0x80) {
    *LPSind = 1;
    *MPScount = run_count[codeword >> (code_num ^ 0x07)];
  } else {
    *MPScount = (uint8_t)(1u << code_num);
    *LPSind = 0;
  }
}

typedef struct {
  SDD1_GCD *GCD;
  uint8_t code_num;
  uint8_t MPScount;
  bool8 LPSind;
} SDD1_BG;

static void BG_prepareDecomp(SDD1_BG *bg) {
  bg->MPScount = 0;
  bg->LPSind = 0;
}

static uint8_t BG_getBit(SDD1_BG *bg, bool8 *endOfRun) {
  uint8_t bit;
  if (!(bg->MPScount || bg->LPSind))
    GCD_getRunCount(bg->GCD, bg->code_num, &bg->MPScount, &bg->LPSind);

  if (bg->MPScount) {
    bit = 0;
    bg->MPScount--;
  } else {
    bit = 1;
    bg->LPSind = 0;
  }

  if (bg->MPScount || bg->LPSind)
    *endOfRun = 0;
  else
    *endOfRun = 1;

  return bit;
}

typedef struct {
  uint8_t status;
  uint8_t MPS;
} SDD1_ContextInfo;

typedef struct {
  uint8_t code_num;
  uint8_t nextIfMPS;
  uint8_t nextIfLPS;
} SDD1_EvolutionState;

static const SDD1_EvolutionState evolution_table[] = {
 { 0,25,25},
 { 0, 2, 1},
 { 0, 3, 1},
 { 0, 4, 2},
 { 0, 5, 3},
 { 1, 6, 4},
 { 1, 7, 5},
 { 1, 8, 6},
 { 1, 9, 7},
 { 2,10, 8},
 { 2,11, 9},
 { 2,12,10},
 { 2,13,11},
 { 3,14,12},
 { 3,15,13},
 { 3,16,14},
 { 3,17,15},
 { 4,18,16},
 { 4,19,17},
 { 5,20,18},
 { 5,21,19},
 { 6,22,20},
 { 6,23,21},
 { 7,24,22},
 { 7,24,23},
 { 0,26, 1},
 { 1,27, 2},
 { 2,28, 4},
 { 3,29, 8},
 { 4,30,12},
 { 5,31,16},
 { 6,32,18},
 { 7,24,22}
};

typedef struct {
  SDD1_BG *BG[8];
  SDD1_ContextInfo contextInfo[32];
} SDD1_PEM;

static void PEM_prepareDecomp(SDD1_PEM *pem) {
  for (uint8_t i = 0; i < 32; i++) {
    pem->contextInfo[i].status = 0;
    pem->contextInfo[i].MPS = 0;
  }
}

static uint8_t PEM_getBit(SDD1_PEM *pem, uint8_t context) {
  bool8 endOfRun;
  uint8_t bit;

  SDD1_ContextInfo *ci = &pem->contextInfo[context];
  const SDD1_EvolutionState *st = &evolution_table[ci->status];
  uint8_t currentMPS = ci->MPS;

  bit = BG_getBit(pem->BG[st->code_num], &endOfRun);

  if (endOfRun) {
    if (bit) {
      if (!(ci->status & 0xfe))
        ci->MPS ^= 0x01;
      ci->status = st->nextIfLPS;
    } else {
      ci->status = st->nextIfMPS;
    }
  }

  return (uint8_t)(bit ^ currentMPS);
}

typedef struct {
  uint8_t bitplanesInfo;
  uint8_t contextBitsInfo;
  uint8_t currBitplane;
  uint32_t bit_number;
  uint16_t prevBitplaneBits[8];
  SDD1_PEM *PEM;
} SDD1_CM;

static void CM_prepareDecomp(SDD1_CM *cm, SDD1_PEM *pem, uint8_t first_byte) {
  cm->bitplanesInfo = first_byte & 0xc0;
  cm->contextBitsInfo = first_byte & 0x30;
  cm->bit_number = 0;
  cm->PEM = pem;
  for (int i = 0; i < 8; i++)
    cm->prevBitplaneBits[i] = 0;

  switch (cm->bitplanesInfo) {
    case 0x00: cm->currBitplane = 1; break;
    case 0x40: cm->currBitplane = 7; break;
    case 0x80: cm->currBitplane = 3; break;
    default: break;
  }
}

static uint8_t CM_getBit(SDD1_CM *cm) {
  uint8_t currContext;
  uint16_t *context_bits;

  switch (cm->bitplanesInfo) {
    case 0x00:
      cm->currBitplane ^= 0x01;
      break;
    case 0x40:
      cm->currBitplane ^= 0x01;
      if (!(cm->bit_number & 0x7f))
        cm->currBitplane = (uint8_t)((cm->currBitplane + 2) & 0x07);
      break;
    case 0x80:
      cm->currBitplane ^= 0x01;
      if (!(cm->bit_number & 0x7f))
        cm->currBitplane ^= 0x02;
      break;
    case 0xc0:
      cm->currBitplane = (uint8_t)(cm->bit_number & 0x07);
      break;
  }

  context_bits = &cm->prevBitplaneBits[cm->currBitplane];
  currContext = (uint8_t)((cm->currBitplane & 0x01) << 4);

  switch (cm->contextBitsInfo) {
    case 0x00:
      currContext |= (uint8_t)(((*context_bits & 0x01c0) >> 5) | (*context_bits & 0x0001));
      break;
    case 0x10:
      currContext |= (uint8_t)(((*context_bits & 0x0180) >> 5) | (*context_bits & 0x0001));
      break;
    case 0x20:
      currContext |= (uint8_t)(((*context_bits & 0x00c0) >> 5) | (*context_bits & 0x0001));
      break;
    case 0x30:
      currContext |= (uint8_t)(((*context_bits & 0x0180) >> 5) | (*context_bits & 0x0003));
      break;
  }

  uint8_t bit = PEM_getBit(cm->PEM, currContext);
  *context_bits = (uint16_t)((*context_bits << 1) | bit);
  cm->bit_number++;
  return bit;
}

static void OL_launch(SDD1_CM *cm, uint8_t bitplanesInfo,
                        uint16_t length, uint8_t *buffer) {
  uint8_t i, register1, register2;

  switch (bitplanesInfo) {
    case 0x00:
    case 0x40:
    case 0x80:
      i = 1;
      do {
        if (!i) {
          *buffer++ = register2;
          i = (uint8_t)~i;
        } else {
          register1 = register2 = 0;
          for (i = 0x80; i; i >>= 1) {
            if (CM_getBit(cm)) register1 |= i;
            if (CM_getBit(cm)) register2 |= i;
          }
          *buffer++ = register1;
        }
      } while (--length);
      break;

    case 0xc0:
      do {
        register1 = 0;
        for (i = 0x01; i; i <<= 1) {
          if (CM_getBit(cm)) register1 |= i;
        }
        *buffer++ = register1;
      } while (--length);
      break;
  }
}

/* ========== Public decompression entry point ========== */

void sdd1_decompress(Sdd1 *s, uint32_t in_addr, uint16_t out_len,
                      uint8_t *out_buf, const uint8_t *rom, uint32_t romSize) {
  SDD1_IM im;
  im.sdd1 = s;
  im.rom = rom;
  im.romSize = romSize;
  IM_prepareDecomp(&im, in_addr);

  SDD1_GCD gcd = { .IM = &im };

  SDD1_BG bg[8];
  for (uint8_t i = 0; i < 8; i++) {
    bg[i].GCD = &gcd;
    bg[i].code_num = i;
    BG_prepareDecomp(&bg[i]);
  }

  SDD1_PEM pem;
  for (uint8_t i = 0; i < 8; i++)
    pem.BG[i] = &bg[i];
  PEM_prepareDecomp(&pem);

  uint8_t first_byte = sdd1_rom_read(s, in_addr, rom, romSize);
  SDD1_CM cm;
  CM_prepareDecomp(&cm, &pem, first_byte);

  OL_launch(&cm, (uint8_t)(first_byte & 0xc0), out_len, out_buf);
}

/* ========== Chip lifecycle ========== */

Sdd1 *sdd1_alloc(void) {
#ifdef TARGET_GNW
  /* Allocate the full struct (including 64KB buffer) in RAM_EMU, not DTCM. */
  return (Sdd1 *)ram_calloc(1, sizeof(Sdd1));
#else
  return (Sdd1 *)snes_zalloc(sizeof(Sdd1));
#endif
}

uint32_t sdd1_size(void) {
  return (uint32_t)sizeof(Sdd1);
}

void sdd1_reset(Sdd1 *s) {
  s->version = SDD1_VERSION;
  s->sdd1_enable = 0;
  s->xfer_enable = 0;
  s->mmc[0] = 0 << 20;
  s->mmc[1] = 1 << 20;
  s->mmc[2] = 2 << 20;
  s->mmc[3] = 3 << 20;
  for (int i = 0; i < 8; i++) {
    s->dma[i].addr = 0;
    s->dma[i].size = 0;
  }
  s->buf_offset = 0;
  s->buf_size = 0;
  s->buf_ready = false;
}

/* ========== MMIO: $4800-$4807 and DMA shadow $4300-$437F ========== */

uint8_t sdd1_mmio_read(Sdd1 *s, uint16_t addr) {
  switch (addr) {
    case 0x4804: return (uint8_t)(s->mmc[0] >> 20);
    case 0x4805: return (uint8_t)(s->mmc[1] >> 20);
    case 0x4806: return (uint8_t)(s->mmc[2] >> 20);
    case 0x4807: return (uint8_t)(s->mmc[3] >> 20);
  }
  return 0;
}

void sdd1_mmio_write(Sdd1 *s, uint16_t addr, uint8_t val) {
  if (addr >= 0x4300 && addr < 0x4380) {
    unsigned ch = (addr >> 4) & 7;
    switch (addr & 15) {
      case 2: s->dma[ch].addr = (s->dma[ch].addr & 0xffff00) | ((uint32_t)val << 0);  break;
      case 3: s->dma[ch].addr = (s->dma[ch].addr & 0xff00ff) | ((uint32_t)val << 8);  break;
      case 4: s->dma[ch].addr = (s->dma[ch].addr & 0x00ffff) | ((uint32_t)val << 16); break;
      case 5: s->dma[ch].size = (s->dma[ch].size & 0xff00) | val;        break;
      case 6: s->dma[ch].size = (s->dma[ch].size & 0x00ff) | (val << 8); break;
    }
#ifdef HOST_BUILD
    {
      const char *log = getenv("HOST_SDD1_LOG");
      if (log && log[0] && ((addr & 15) >= 2 && (addr & 15) <= 6)) {
        fprintf(stderr, "sdd1: dma ch=%u reg=%x val=%02x addr=%06x size=%04x\n",
                ch, (unsigned)(addr & 15), val,
                (unsigned)s->dma[ch].addr, (unsigned)s->dma[ch].size);
      }
    }
#endif
    return;
  }
  switch (addr) {
    case 0x4800: s->sdd1_enable = val; break;
    case 0x4801: s->xfer_enable = val; break;
    case 0x4804: s->mmc[0] = (uint32_t)val << 20; break;
    case 0x4805: s->mmc[1] = (uint32_t)val << 20; break;
    case 0x4806: s->mmc[2] = (uint32_t)val << 20; break;
    case 0x4807: s->mmc[3] = (uint32_t)val << 20; break;
  }
#ifdef HOST_BUILD
  {
    const char *log = getenv("HOST_SDD1_LOG");
    if (log && log[0] && addr >= 0x4800 && addr <= 0x4807) {
      fprintf(stderr, "sdd1: mmio %04x=%02x en=%02x xfer=%02x mmc=%02x/%02x/%02x/%02x\n",
              addr, val, s->sdd1_enable, s->xfer_enable,
              (unsigned)(s->mmc[0] >> 20), (unsigned)(s->mmc[1] >> 20),
              (unsigned)(s->mmc[2] >> 20), (unsigned)(s->mmc[3] >> 20));
    }
  }
#endif
}

/* ========== Cart read: $C0-$FF:$0000-$FFFF ========== */

uint8_t sdd1_read(Sdd1 *s, uint32_t addr, const uint8_t *rom, uint32_t romSize) {
  if (s->sdd1_enable & s->xfer_enable) {
    for (unsigned i = 0; i < 8; i++) {
      if (s->sdd1_enable & s->xfer_enable & (1 << i)) {
        if (addr == s->dma[i].addr) {
          if (!s->buf_ready) {
            s->buf_offset = 0;
            s->buf_size = s->dma[i].size ? s->dma[i].size : 65536;

#ifdef HOST_BUILD
            {
              const char *log = getenv("HOST_SDD1_LOG");
              if (log && log[0]) {
                fprintf(stderr,
                        "sdd1: decompress ch=%u in=0x%08x size=%u mmc=%08x/%08x/%08x/%08x\n",
                        i, (unsigned)addr, (unsigned)s->buf_size,
                        (unsigned)s->mmc[0], (unsigned)s->mmc[1],
                        (unsigned)s->mmc[2], (unsigned)s->mmc[3]);
              }
            }
#endif

            uint8_t saved = s->sdd1_enable;
            s->sdd1_enable = 0;
            sdd1_decompress(s, addr, (uint16_t)s->buf_size, s->buffer, rom, romSize);
            s->sdd1_enable = saved;

            s->buf_ready = true;
          }

          uint8_t data = s->buffer[s->buf_offset++];
          if (s->buf_offset >= s->buf_size) {
            s->buf_ready = false;
            s->xfer_enable &= ~(1 << i);
          }
          return data;
        }
      }
    }
  }

  return sdd1_rom_read(s, addr, rom, romSize);
}
