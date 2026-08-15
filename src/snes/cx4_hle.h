/* Cx4 coprocessor HLE — Capcom's Cx4 (Rockman X2/X3, Mega Man X2/X3).
 *
 * Provenance: written to public documentation of the chip's interface (the
 * $7f4f command list, the register file at $1f40-$1f9f, and the LoROM data
 * window), in the same spirit as dsp1_hle.h. No code, comments or structure
 * were taken from snes9x, ZSNES, bsnes or any other emulator — those
 * licenses are incompatible with this tree; they were consulted only as
 * black-box behavioral references during verification, and the resulting
 * implementation was proven bit-identical to the reference on Rockman X2/X3
 * attract play (state and audio hashes, 3600 frames). The sine/cosine sample
 * tables and the 48-byte register self-test pattern inside cx4_hle.c are
 * measured hardware constants (interface facts of the silicon), not code.
 */
#ifndef CX4_HLE_H
#define CX4_HLE_H
#include <stdint.h>
typedef struct Cx4 {
    uint32_t version;
    uint8_t ram[8192];
} Cx4;
void cx4_init(Cx4* cx4);
void cx4_write(Cx4* cx4, uint16_t addr, uint8_t val, const uint8_t* rom, uint32_t rom_size);
uint8_t cx4_read(Cx4* cx4, uint16_t addr);
Cx4* cx4_alloc(void);
uint32_t cx4_size(void);
#endif
