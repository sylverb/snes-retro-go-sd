
#ifndef SNES_H
#define SNES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Snes Snes;

#include "cpu.h"
#include "apu.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "input.h"
#include "saveload.h"

struct Snes {
  Cpu* cpu;
  Apu* apu;
  Ppu* ppu, *snes_ppu, *my_ppu;
  Dma* dma;
  Cart* cart;
  Input *input1;
  Input *input2;
  // input
  bool debug_cycles;
  bool debug_apu_cycles;
  bool disableRender;
  uint8_t runningWhichVersion;

  // ram
  uint32_t ramAdr;
  uint8_t *ram;
  uint8_t padx[4];

  // frame timing
  uint16_t hPos;
  uint16_t vPos;
  uint32_t frames;
  // cpu handling
  uint8_t cpuCyclesLeft;
  uint8_t cpuMemOps;
  uint8_t padpad[2];
  double apuCatchupCycles;
  uint32_t apuDotsAccum;   /* integer dot accumulator — converted to double in snes_catchupApu to avoid per-dot VCVT+VMUL */
  // nmi / irq
  bool hIrqEnabled;
  bool vIrqEnabled;
  bool nmiEnabled;
  uint16_t hTimer;
  uint16_t vTimer;
  bool inNmi;
  bool inIrq;
  bool inVblank;
  // joypad handling
  uint16_t portAutoReadX[4]; // as read by auto-joypad read
  bool autoJoyRead;
  uint16_t autoJoyTimer; // times how long until reading is done
  bool ppuLatch;
  // multiplication/division
  uint8_t multiplyA;
  uint16_t multiplyResult;
  uint16_t divideA;
  uint16_t divideResult;
  // misc
  bool fastMem;
  uint8_t openBus;
  /* ROM fetch-page cache for snes_cpuRead(). Deliberately declared AFTER
   * openBus: snes_saveload() serializes the byte range hPos..openBus, so
   * anything past it stays out of the savestate -- which is what we want,
   * these are a derived host pointer and its tag, not emulated state, and a
   * savestate must never carry a host address across a load. */
  const uint8_t *romPageBase;
  uint32_t romPageTag;
  /* PAL (header $ffd9): 312 lines / 50 Hz. After romPageTag so Thumb-2
   * SNES_ROMPAGE* offsets stay valid. Not in the savestate range. */
  bool pal;
  uint16_t vcount; /* 262 NTSC, 312 PAL */
};

Snes* snes_init(uint8_t *ram);
void snes_free(Snes* snes);
void snes_reset(Snes* snes, bool hard);
void snes_set_region(Snes* snes, bool pal);
void snes_runFrame(Snes* snes);
// used by dma, cpu
uint8_t snes_readBBus(Snes* snes, uint8_t adr);
void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val);
uint8_t snes_read(Snes* snes, uint32_t adr);
void snes_write(Snes* snes, uint32_t adr, uint8_t val);
uint8_t snes_cpuRead(Snes* snes, uint32_t adr);
void snes_cpuWrite(Snes* snes, uint32_t adr, uint8_t val);
// debugging
void snes_debugCycle(Snes* snes, bool* cpuNext, bool* spcNext);

void snes_handle_pos_stuff(Snes *snes);

// snes_other.c functions:

bool snes_loadRom(Snes* snes, const uint8_t* data, int length);
void snes_setPixels(Snes* snes, uint8_t* pixelData);
void snes_setSamples(Snes* snes, int16_t* sampleData, int samplesPerFrame);
void snes_saveload(Snes *snes, SaveLoadFunc *func, void *ctx);
/* One scanline of dot-clock events in one call — see snes.c. */
void snes_run_line(Snes *snes);
uint8_t snes_readBBusOrg(Snes *snes, uint8_t adr);
void snes_catchupApu(Snes *snes);

extern int snes_frame_counter;
#endif
