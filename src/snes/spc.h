#ifndef SPC_H
#define SPC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Spc Spc;

#include "apu.h"
#include "saveload.h"

struct Spc {
  Apu* apu;
  // registers
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t sp;
  uint16_t pc;
  // flags
  bool c;
  bool z;
  bool v;
  bool n;
  bool i;
  bool h;
  bool p;
  bool b;
  // stopping
  bool stopped;
  // internal use
  uint8_t cyclesUsed; // indicates how many cycles an opcode used
};

Spc* spc_init(Apu* apu);
void spc_free(Spc* spc);
void spc_reset(Spc* spc);
int spc_runOpcode(Spc* spc);
void spc_saveload(Spc *spc, SaveLoadFunc *func, void *ctx);

// Thumb-2 SPC700 engine (defined in thumb2/spc_thumb2.S when SPC_THUMB2_SPC=1).
// Single-opcode entry: performs the opcode fetch, base-cycle charge, and
// dispatch itself. Returns -1 if the opcode was fully handled (cyclesUsed is
// already charged), or the opcode byte (0..255) for the C dispatcher
// (spc_doOpcode) to fall through on -- the engine has still advanced pc past
// the opcode and charged cyclesUsed in that case.
int spc_thumb2_step(Spc* spc);

#endif
