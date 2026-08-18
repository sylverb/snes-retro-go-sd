
#ifndef SDD1_H
#define SDD1_H

#include <stdint.h>
#include <stdbool.h>

/* S-DD1 graphics decompression chip (Star Ocean, Street Fighter Alpha 2).
 *
 * The chip sits on HiROM carts and does two things:
 *   1. Memory-map controller (MMC): four 1 MB banks covering $C0-$FF mapped
 *      to arbitrary 1 MB ROM segments via registers $4804-$4807.
 *   2. DMA decompression: when enabled ($4800/$4801), ROM reads during DMA
 *      return decompressed data instead of raw ROM bytes.
 *
 * The decompression algorithm (by Andreas Naive, public domain) uses Golomb
 * coding with a probability estimation machine — see sdd1.c.
 *
 * Saveload: the struct is plain data (no pointers); serialize the whole thing. */

typedef struct Sdd1 {
  uint32_t version;

  uint8_t sdd1_enable;    /* $4800: channel bitmask for decompression */
  uint8_t xfer_enable;    /* $4801: channel bitmask for pending transfers */
  uint32_t mmc[4];        /* $4804-$4807: ROM base for each 1 MB window */

  struct {
    uint32_t addr;         /* $43x2-$43x4: DMA source address */
    uint16_t size;         /* $43x5-$43x6: DMA transfer size */
  } dma[8];

  /* Decompression buffer — max DMA transfer is 64 KB.
   * Keep it inline so savestates stay byte-compatible. On the device we
   * allocate the whole struct in RAM_EMU (not DTCM) to avoid blowing DTCM. */
  uint8_t buffer[65536];
  uint16_t buf_offset;
  uint32_t buf_size;
  bool buf_ready;
} Sdd1;

Sdd1 *sdd1_alloc(void);
uint32_t sdd1_size(void);
void sdd1_reset(Sdd1 *s);

/* MMIO at $4800-$4807 (and DMA shadow at $4300-$437F) */
uint8_t sdd1_mmio_read(Sdd1 *s, uint16_t addr);
void sdd1_mmio_write(Sdd1 *s, uint16_t addr, uint8_t val);

/* Cart read at $C0-$FF:$0000-$FFFF — returns decompressed or raw ROM data */
uint8_t sdd1_read(Sdd1 *s, uint32_t addr, const uint8_t *rom, uint32_t romSize);

/* Decompression engine */
void sdd1_decompress(Sdd1 *s, uint32_t in_addr, uint16_t out_len,
                     uint8_t *out_buf, const uint8_t *rom, uint32_t romSize);

#endif
