/* Baked wait-loop skip — a recognizer, not a learner.
 *
 * The spin LEARNER is the whole cost of spin-skip: 4.78 fps on hardware, of
 * which the replay branch itself is nothing (57.22 against 57.23 with the
 * learning stripped out). So the design is to know the loop BEFORE the ROM
 * starts and watch nothing afterwards.
 *
 * What is known before the ROM starts is its bytes. The commonest SNES NMI
 * wait is four of them:
 *
 *     806b: a5 10     LDA $10        ; direct page, low WRAM
 *     806d: f0 fc     BEQ $806b      ; back to itself
 *
 * A 2497-ROM survey grouped candidates by (site pc, polled address) and found
 * one group of 51 and another of 5. Both groups are THIS shape — the grouping
 * key was the address, so it split one loop into many rows. Matching the shape
 * instead is address-agnostic, which is the point: Super Mario World and its
 * fifty ROM hacks relocate nothing here, and A Link to the Past runs the same
 * four bytes at $00:8034 polling $12.
 *
 * This is NOT the learner's replay. The learner recorded a cycle pattern and
 * replayed it, which is only sound while the loop is provably pure — that is
 * what the two-lap register/write/IO proof bought, and what spin_note() had to
 * keep paying for on every real opcode to keep honest. Here the two opcodes are
 * simply EXECUTED, natively:
 *
 *   - the load reads the polled byte out of WRAM and sets A/Z/N exactly as the
 *     interpreter would, so the register state is never stale and the loop
 *     needs no purity proof at all;
 *   - the branch is taken iff Z, which the load just set — so the loop exits by
 *     itself, on the real memory value, the first iteration after an NMI
 *     handler writes it.
 *
 * The only baked numbers are the two cycle charges, and they are guarded: the
 * accumulator width and the direct-page low byte both change what the
 * interpreter would charge, so a mismatch on either declines the replay and
 * lets the interpreter run. Declining is always safe; it only costs the win.
 */
#ifndef SNES_SPIN_BAKE_H
#define SNES_SPIN_BAKE_H

#include <stdint.h>
#include <stdbool.h>

#include "snes.h"
#include "cpu.h"

typedef struct {
  bool     on;
  uint16_t pc_load;      /* pc of the LDA dp                                  */
  uint16_t pc_branch;    /* pc of the BEQ that targets pc_load                */
  uint8_t  bank;         /* program bank the loop was found in                */
  uint8_t  bank_alt;     /* its $80 mirror, or `bank` again when unsafe       */
  uint8_t  dp_off;       /* the LDA's direct-page operand                     */
  uint8_t  mf;           /* accumulator width the charges were measured at    */
  uint8_t  dpl;          /* dp low byte the charges were measured at          */
  uint8_t  charge_load;  /* cycles*6 + memOps*2, device engine                */
  uint8_t  charge_branch;
  uint32_t sites;        /* signature matches in the ROM (1 is the norm)      */
  uint32_t laps;         /* iterations replayed — 0 means it never fired      */

  /* Gate. `on` says a loop was installed; `armed` says run_dots is currently
   * running the clone that looks for it. Everything here is touched once per
   * frame, never per opcode -- which is the entire difference between this and
   * the learner, whose gate cost 4.78 fps because it was charged per opcode. */
  bool     armed;
  uint16_t pc_load_real; /* pc_load's real value; pc_load is 0xffff when parked */
  uint32_t win_frames;   /* frames into the current observation window        */
  uint32_t win_laps;     /* `laps` at the window's start                      */
  uint32_t park_frames;  /* current backoff length, doubling on each failure   */
  uint32_t park_countdown; /* frames left before the next attempt              */
} SpinBake;

extern SpinBake g_bake;

#ifndef SNES_BAKE_NO_DMA_REENTRY
#define SNES_BAKE_NO_DMA_REENTRY 0
#endif

#ifndef SNES_BAKE_BLIND_REPLAY
#define SNES_BAKE_BLIND_REPLAY 0
#endif

/* Scan a loaded cartridge for the signature and install the first match.
 * Call after snes_loadRom (it needs cart->type to map an offset to a pc).
 * Returns true if a loop was installed. */
bool spin_bake_scan(Snes *snes);
void spin_bake_reset(void);
void spin_bake_frame_tick(void);   /* once per emulated frame: arm/disarm */

/* Replay laps for the REST OF THIS SPAN, and return the dots left over.
 *
 * There is no per-opcode guard, and that is a measurement. With a test in
 * run_dots' innermost loop a build that never installed anything still lost
 * 6.6% of A Link to the Past's drawn frames (16.44 -> 15.33); both shapes were
 * tried, the body inlined (3,917,511 insn/frame on Mario Kart) and behind a
 * call (3,844,364), against a 3,764,322 baseline. The only build that matched
 * the baseline was the one where the test did not exist.
 *
 * A wait loop is entered once and spun thousands of times, so it does not have
 * to be noticed per opcode. Noticing it once per run_dots span moves the test
 * from ~13,000 times a frame to ~1,000 and out of the loop gcc allocates
 * registers for: Mario Kart's tax fell from +2.13% to +0.37%, and Super Mario
 * World's win GREW, from -8.4% to -10.2%, because the span loop is tighter
 * than the one it replaced. */
int spin_bake_run_span(Snes *s, Cpu *cpu, int dots);

/* The hot path, shared by main_snes.c and the M7 rig so the two copies of
 * run_dots cannot drift. Call where the interpreter would be dispatched, i.e.
 * with cpuCyclesLeft == 0; returns true if it executed the opcode itself.
 *
 * `pc_load` is passed in, not read from g_bake, and the whole test is one
 * subtract against it. Both halves of that sentence were paid for: reading the
 * two pcs out of the global cost two loads on EVERY opcode, and A Link to the
 * Past -- which replays 730 laps a frame -- came out 1.3% SLOWER on the rig
 * with the win in hand. The caller hoists the field once per run_dots span
 * (nothing writes it after ROM load), so a cart that never spins pays a
 * register compare.
 *
 * The window is 3 wide rather than 2 so it stays a single unsigned compare;
 * pc_load+1 is the LDA's operand byte and is never an opcode boundary, and the
 * cold path below rejects it anyway. */
/*  * As an external call it sits inside run_dots' innermost loop, where gcc must
 * assume it clobbers memory and reloads everything around it: a build that
 * NEVER INSTALLS anything still cost 6.6%% of A Link to the Past's drawn frames
 * (16.44 -> 15.33). The test is four instructions; the call was the price. */


#endif
