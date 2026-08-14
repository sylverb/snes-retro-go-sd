/* NMI-wait spin skip — exact-replay (SNES_SPIN_SKIP builds).
 *
 * The spin probe measured Zelda 81% / SMW 76% of gameplay opcodes inside pure
 * WRAM/DP-flag wait loops (`spin: LDA $12 / BEQ spin`). Those iterations are
 * semantic no-ops: registers bit-identical each pass, no writes, no IO reads —
 * only the NMI handler can change the polled byte, and no handler can run inside
 * a run_dots span. So inside a span the loop provably cannot exit, and each
 * iteration can be replayed without the interpreter: charge the recorded cycle
 * pattern, advance pc along the recorded ring, and let the SAME bulk-consume
 * code chunk the dots. Bit-identical state, minus the interpreter work.
 *
 * TWO-PHASE learner (the M7 rig showed the always-on version costing MORE than
 * the replay saved: +0.9M insn/frame of ring stores, register packing and purity
 * classification on every real opcode/read — an in-order core pays all of it):
 *
 *   WATCH  (default): per opcode, one PC ring store + a d<=8 revisit scan.
 *          The read hook is a single predictable branch. Nothing else.
 *   VERIFY (a PC revisited at period d): NOW pack registers, snapshot the
 *          write/io counters, and shadow the next 2*d opcodes against the
 *          candidate. Registers identical at the anchor on both laps + counters
 *          frozen => the machine state truly recurred => adopt for replay.
 *   REPLAY (adopted): the interpreter does not run; any interrupt, DMA, armed
 *          IRQ or PC mismatch drops the pattern (relearn costs 2 iterations).
 *
 * This is the ONE implementation: the device port (main_snes.c), the host gate
 * harness (tools/snes_spin) and the M7 rig compile this same file. Gate: skip
 * off vs on must produce identical state and audio hashes.
 *
 * Runtime auto-gate (address-agnostic, NO per-game list — Korean-patched and
 * rom-hacked carts behave identically): a cart that ends an observation window
 * with (almost) no replayed ops parks the learner, drops whatever pattern it
 * had adopted (see spin_frame_tick) and retries later. */
#ifndef SNES_SPIN_SKIP_H
#define SNES_SPIN_SKIP_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Cpu Cpu;

#define SPIN_PMAX 8    /* longest loop body (opcodes) we replay */
#define SPIN_WR   16   /* WATCH ring size (PCs only) */

typedef struct {
  /* adopted pattern (the loop being replayed) */
  uint32_t pc[SPIN_PMAX];
  uint8_t  charge[SPIN_PMAX];
  int      len, idx;
  bool     on;

  /* learner phase: 0 = WATCH, 1 = VERIFY */
  uint8_t  phase;

  /* WATCH: ring of recent opcode PCs, nothing else */
  uint32_t wpc[SPIN_WR];
  int      w_h;

  /* VERIFY: candidate loop being shadowed */
  uint32_t vpc[SPIN_PMAX];     /* PCs of one iteration (anchor at [0]) */
  uint8_t  vcharge[SPIN_PMAX];
  int      v_d;                /* period */
  int      v_pos;              /* opcodes seen since anchor, 0..2*d */
  uint64_t v_r1, v_r2;         /* registers at the anchor */
  uint32_t v_w, v_io;          /* write/io counters (only counted in VERIFY) */

  /* purity counters — ONLY advanced while phase==VERIFY (cheap hooks otherwise) */
  uint32_t write_seq, io_seq;

  /* auto-gate */
  bool     gate_on;
  uint32_t win_frames, win_virt_snap, win_real_snap;
  uint32_t park_frames;

  /* stats */
  uint64_t ops_real, ops_virtual;
} SpinSkip;

extern SpinSkip g_spin;

/* ROM gate: set by main_snes.c at ROM load. Default true (0721 whitelist-gap
 * fix) -- an unregistered ROM is handed to spin_frame_tick()'s auto-gate,
 * which observes 600 frames and parks itself for 1800 (cheap) if the ROM
 * isn't actually spin-heavy, retrying later; this is address-agnostic and
 * doesn't need a per-ROM entry to work. The title-hash table in
 * spin_skip.c is now only for forcing a known-bad case OFF outright (a ROM
 * whose measured skip% falls below the ~50% breakeven, so even the auto-gate's
 * brief probe overhead isn't worth paying -- Zelda: 25% skip%, +16% rig insn
 * if left on). */
extern bool g_spin_whitelist;

/* cpu.c hooks. Deliberately tiny outside VERIFY: one predictable branch.
 * cpu.c routes opcode/operand FETCHES around them entirely — spin_hook_read()'s
 * first test discards every address within ±6 bytes of the PC, which a fetch is
 * by construction, so hooking them only ever bought the branch. */
static inline void spin_hook_write(void) {
  if (g_spin.phase) g_spin.write_seq++;
}
void spin_hook_read(Cpu *cpu, uint32_t adr);   /* classifies only in VERIFY */

/* Per real opcode call (pre-call pc24, post-call ccl charge). Registers are
 * read from `cpu` INSIDE the learner, and only in VERIFY — the always-on
 * 64-bit packing was a measured chunk of the old version's overhead. */
void spin_note(Cpu *cpu, uint32_t pc24, uint8_t charge, int dispatched);

/* Is the learner doing anything at all? `gate_on` false and no adopted pattern
 * means spin_note() returns after two branch checks and every byte of the
 * caller's bookkeeping (the pc24/dispatch pack, the 64-bit ops_real increment,
 * the call itself) is dead work — a tax paid in full by any cart the table or
 * the auto-gate turns OFF, which currently includes Zelda 3.
 *
 * Sampled ONCE per opcode, before the interpreter call: neither field can
 * change inside an opcode — spin_note() (between opcodes) is the only writer of
 * `on`, spin_frame_tick() (between frames) the only writer of `gate_on`.
 *
 * `on` is in the test because a pattern must stay honest against real execution
 * even after the gate parks; spin_frame_tick() drops the pattern when it parks,
 * so that does not leave the tax running forever. */
static inline bool spin_engaged(void) {
  return g_spin.gate_on || g_spin.on;
}

/* The post-opcode half, so main_snes.c and all three rigs share one body
 * instead of four copies that can drift. */
static inline void spin_note_real(Cpu *cpu, uint32_t pc24, uint8_t charge,
                                  int dispatched) {
  g_spin.ops_real++;
  spin_note(cpu, pc24, charge, dispatched);
}

void spin_frame_tick(void);   /* once per emulated frame: auto-gate */
void spin_reset(void);
void spin_whitelist_set(const uint8_t *rom, uint32_t len);  /* call before spin_reset */

#endif
