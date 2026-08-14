/* Two-phase exact-replay spin-skip learner. See spin_skip.h for the phase
 * design; the replay semantics (and the purity rules) are the harness-proven
 * ones — the phases only change WHEN the expensive bookkeeping runs, never what
 * is proven before a loop is adopted. Adoption requires, exactly as before:
 * the same PC revisited with registers identical and not a single write or
 * IO-classified read across two full laps. */
#include "spin_skip.h"
#include "cpu.h"

SpinSkip g_spin;
/* Default true: an unregistered ROM is handed to the auto-gate (spin_frame_tick)
 * to decide at runtime, rather than sitting permanently off. See
 * spin_whitelist_set()'s comment for why (0721 whitelist-gap fix).
 *
 * Compile-time override for a device A/B (0722): the firmware Makefile can
 * define SNES_SPIN_SKIP_DEFAULT=false to build the OFF arm without touching
 * this file. Absent any override the behaviour is byte-identical to before
 * this macro existed -- the ON arm IS the default build. */
#ifndef SNES_SPIN_SKIP_DEFAULT
#define SNES_SPIN_SKIP_DEFAULT true
#endif
bool g_spin_whitelist = SNES_SPIN_SKIP_DEFAULT;

/* Reads within +-6 bytes of the PC are the opcode/operand fetch; WRAM and ROM
 * reads are side-effect-free. Anything else ($21xx APU ports, $42xx HVBJOY/joy,
 * $43xx DMA regs, expansion) observably moves state: an iteration that touches
 * one can terminate on its own and must never be replayed. Classification only
 * runs while VERIFY shadows a candidate — outside it this is one branch. */
void spin_hook_read(Cpu *cpu, uint32_t adr) {
  if (!g_spin.phase) return;
  uint32_t pcb = ((uint32_t)cpu->k << 16) | cpu->pc;
  if (adr - (pcb - 6) <= 12) return;
  uint8_t bank = adr >> 16;
  uint16_t off = (uint16_t)adr;
  bool wram = (bank == 0x7e || bank == 0x7f) ||
              (off < 0x2000 && (bank < 0x40 || (bank >= 0x80 && bank < 0xc0)));
  if (wram) return;
  bool rom = (off >= 0x8000) || (bank >= 0x40 && bank < 0x7e) || (bank >= 0xc0);
  if (rom) return;
  g_spin.io_seq++;
}

/* Register identity is REQUIRED, not optional: a delay loop (`dey / bne`) writes
 * nothing and reads nothing yet terminates on its own — without the regs check
 * it gets adopted and replayed forever (the first harness build did exactly that
 * and died in cart_readLorom). Equal regs at the anchor on consecutive laps + no
 * writes + no IO reads = the machine state truly recurred, so the loop provably
 * cannot exit by itself. (Compared post-opcode both times: a consistent sample
 * point proves recurrence just as well as the old pre-opcode one.) */
static inline void spin_pack_regs(Cpu *cpu, uint64_t *r1, uint64_t *r2) {
  *r1 = (uint64_t)cpu->a | ((uint64_t)cpu->x << 16) |
        ((uint64_t)cpu->y << 32) | ((uint64_t)cpu->sp << 48);
  *r2 = (uint64_t)cpu->dp | ((uint64_t)cpu->k << 16) |
        ((uint64_t)cpu->db << 24) | ((uint64_t)cpu_getFlags(cpu) << 32) |
        ((uint64_t)cpu->e << 40);
}

void spin_note(Cpu *cpu, uint32_t pc24, uint8_t charge, int dispatched) {
  SpinSkip *s = &g_spin;

  /* keep an adopted pattern honest against real execution */
  if (s->on) {
    if (dispatched || pc24 != s->pc[s->idx])
      s->on = false;          /* pattern broken — fall through to WATCH */
    else {
      s->idx = (s->idx + 1) % s->len;
      return;                 /* pattern alive — WATCH ring + scan are wasted work */
    }
  }
  if (!s->gate_on) return;

  if (s->phase) {
    /* ---- VERIFY: shadow the candidate for two laps ---- */
    if (dispatched) { s->phase = 0; return; }
    int pos = s->v_pos, d = s->v_d;
    if (pos < d) {
      /* first lap: record */
      s->vpc[pos] = pc24; s->vcharge[pos] = charge;
      s->v_pos = pos + 1;
      if (pos + 1 == d) {
        /* anchor reached again: registers must match the entry sample */
        uint64_t r1, r2; spin_pack_regs(cpu, &r1, &r2);
        if (r1 != s->v_r1 || r2 != s->v_r2) { s->phase = 0; return; }
      }
      return;
    }
    /* second lap: compare */
    if (s->vpc[pos - d] != pc24 || s->vcharge[pos - d] != charge) {
      s->phase = 0;
      return;
    }
    s->v_pos = pos + 1;
    if (pos + 1 == 2 * d) {
      /* anchor a third time: regs identical + not one write/IO across both laps */
      uint64_t r1, r2; spin_pack_regs(cpu, &r1, &r2);
      if (r1 == s->v_r1 && r2 == s->v_r2 &&
          s->write_seq == 0 && s->io_seq == 0) {
        for (int q = 0; q < d; q++) { s->pc[q] = s->vpc[q]; s->charge[q] = s->vcharge[q]; }
        s->len = d; s->idx = 0; s->on = true;
      }
      s->phase = 0;
    }
    return;
  }

  /* ---- WATCH: one ring store + a short revisit scan, nothing else ---- */
  int h = s->w_h;
  s->wpc[h] = pc24;
  s->w_h = (h + 1) & (SPIN_WR - 1);
  if (s->on || dispatched) return;
  for (int d = 1; d <= SPIN_PMAX; d++) {
    if (s->wpc[(h - d) & (SPIN_WR - 1)] == pc24) {
      /* candidate loop of period d: start shadowing it */
      s->phase = 1;
      s->v_d = d; s->v_pos = 0;
      s->write_seq = 0; s->io_seq = 0;
      spin_pack_regs(cpu, &s->v_r1, &s->v_r2);
      return;
    }
  }
}

/* Observation-window auto-gate. Window = 600 frames (~10 s). A cart that ended
 * the window with (almost) no replayed ops is not spinning — park the learner
 * for 1800 frames (~30 s) so it costs nothing, then retry (loading screens end).
 * An adopted pattern keeps replaying even while parked; only LEARNING pauses. */
#define SPIN_WIN_FRAMES  600u
#define SPIN_PARK_FRAMES 1800u

void spin_frame_tick(void) {
  SpinSkip *s = &g_spin;
  if (!s->gate_on) {
    if (s->park_frames > 0 && --s->park_frames == 0) {
      s->gate_on = g_spin_whitelist;
      s->win_frames = 0;
      s->win_virt_snap = (uint32_t)s->ops_virtual;
      s->win_real_snap = (uint32_t)s->ops_real;
    }
    return;
  }
  if (++s->win_frames < SPIN_WIN_FRAMES) return;
  /* win_virt_snap holds (truncated) ops_virtual at window start; the delta over
   * 600 frames always fits 32 bits. */
  uint32_t replayed = (uint32_t)s->ops_virtual - s->win_virt_snap;
  uint32_t real = (uint32_t)s->ops_real - s->win_real_snap;
  s->win_frames = 0;
  s->win_virt_snap = (uint32_t)s->ops_virtual;
  s->win_real_snap = (uint32_t)s->ops_real;
  /* Park unless the learner skipped at least as many opcodes as it charged for.
   *
   * The old test was "did it replay more than ~3 ops a frame", which asks only
   * whether the game spins at all. A device profile answered the question it
   * was standing in for: Zelda 3 replays 25% of its opcodes and is still a NET
   * LOSS -- turning the learner off is worth +2.1 fps there -- while SMW at 53%
   * wants it. The tax is per REAL opcode and the benefit is per REPLAYED one,
   * so the honest comparison is between those two counts, and breakeven sits
   * between 25% and 53%. Requiring replayed >= real puts it at 50%: Zelda parks,
   * SMW keeps it, and neither is named in a table.
   *
   * The per-ROM table stays as an override for a game that measures badly, but
   * it is no longer what decides the common case -- it could not be, since it
   * matches on a ROM hash and the dump in the user's hand was not the one in
   * the table, which is exactly how Zelda ended up paying this tax all along. */
  if (replayed < real) {
    s->gate_on = false;
    s->phase = 0;
    /* Drop the adopted pattern too, or the park is not actually free. An
     * adversarial review (0722) found the reachable case: with H-IRQ armed,
     * run_dots' replay branch is blocked outright (`!s->hIrqEnabled`) while
     * spin_note()'s own checks don't look at it -- so `on` stays true, replay
     * stays zero, the window parks the gate, and every real opcode goes on
     * paying for a pattern that cannot fire. `gate_on || on` is what keeps the
     * caller's bookkeeping alive, so clearing `on` here is what makes a parked
     * learner cost nothing.
     * We are giving up at most the ~3 replayed ops/frame that failed this very
     * window, and dropping a pattern is safe by construction: it only means the
     * interpreter runs, which is the reference behaviour. */
    s->on = false;
    s->park_frames = SPIN_PARK_FRAMES;
    return;
  }
}

void spin_reset(void) {
  SpinSkip *s = &g_spin;
  *s = (SpinSkip){0};
  s->gate_on = g_spin_whitelist;
}

/* ROM exceptions table (0721 whitelist-gap fix): an unregistered ROM now
 * defaults to true (g_spin_whitelist init above) and is handed to
 * spin_frame_tick()'s auto-gate, which is address-agnostic and already
 * proven -- it observes 600 frames, and parks itself for 1800 if replayed
 * ops stay under ~3/frame (i.e. this ROM isn't actually spin-heavy), retrying
 * later. A 2281-ROM sweep (/tmp/snes_2k_spin.tsv, 1792 ROMs measured) found
 * 941 (52.5%) clear the ~50% skip-rate breakeven where the mechanism nets a
 * host-cycle win purely from letting the auto-gate run -- they were never
 * getting the chance before, because this table's old "unregistered = OFF,
 * permanently" default vetoed the auto-gate outright.
 *
 * This table is now ONLY for forcing a known-bad case OFF outright (skip%
 * measured below breakeven, so even paying for the auto-gate's own brief
 * WATCH/VERIFY overhead before it parks isn't worth it -- Zelda at 25%).
 * A `true` entry is redundant with the new default but harmless to keep as a
 * documented, pre-measured case (SMW at 56.6%).
 *
 * Key = FNV-1a 32-bit hash of the 21-byte internal title at the LoROM
 * (0x7fc0) or HiROM (0xffc0) header offset. To force a ROM off: measure its
 * 1200-frame skip% in the spin rig (tools/m7_qemu_rig/run_snes_spin.sh <rom>
 * 1200), confirm it's below the ~50% breakeven, compute its title hash, add
 * a `false` entry. */
typedef struct { uint32_t hash; bool enable; const char *name; } spin_entry_t;
static const spin_entry_t spin_table[] = {
  { 0xFB0BD0ECu, true,  "SUPER MARIOWORLD  (skip% 56.6% — ON, matches new default)"  },
  { 0x9C75F6EEu, false, "THE LEGEND OF ZELDA  (skip% 25.0% — forced OFF, below breakeven)" },
};
#define SPIN_TABLE_LEN (int)(sizeof(spin_table) / sizeof(spin_table[0]))

void spin_whitelist_set(const uint8_t *rom, uint32_t len) {
  g_spin_whitelist = SNES_SPIN_SKIP_DEFAULT;   /* unregistered default: let the auto-gate decide */
  static const uint32_t offs[2] = { 0x7fc0, 0xffc0 };
  for (int i = 0; i < 2; i++) {
    if (offs[i] + 21 > len) continue;
    uint32_t h = 2166136261u;
    for (int j = 0; j < 21; j++) {
      h ^= rom[offs[i] + j];
      h *= 16777619u;
    }
    for (int k = 0; k < SPIN_TABLE_LEN; k++) {
      if (spin_table[k].hash == h) {
        g_spin_whitelist = spin_table[k].enable;
        return;
      }
    }
  }
}
