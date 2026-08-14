/* rc_dispatch.h — per-ROM static recompiler dispatch layer.
 *
 * When activated (g_rc_active == true), cpu_runOpcode consults a per-bank
 * open-addressing hash table to find a native site function for the current
 * PC. If found, the site runs instead of the interpreter. If not found
 * (0.01% of opcodes on SMW), the interpreter handles it.
 *
 * NO heap allocation. The hash table (~85 KB for SMW's 8371 sites) lives in
 * caller-provided overlay BSS (RAM_EMU), not the DTCM heap. The previous
 * heap-based design OOM-crashed the device (85 KB > 81 KB DTCM heap).
 *
 * Lookup: Knuth multiplicative hash + linear probe, O(1) average.
 *
 * Activation is ROM-specific: main_snes.c checks the ROM and, if it matches
 * a pre-compiled rc blob, caches the blob into flash and calls
 * rc_dispatch_init. Non-matching ROMs leave g_rc_active false — zero
 * overhead (one never-taken branch in cpu_runOpcode). */
#ifndef RC_DISPATCH_H
#define RC_DISPATCH_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

/* One hash slot: (pc, site_id). id=0 means empty. */
typedef struct { uint16_t pc; uint16_t id; } rc_entry_t;

/* Runtime flag: when true, cpu_runOpcode uses rc dispatch. */
extern bool g_rc_active;

/* Build the per-bank open-addressing hash in caller-provided static storage.
 * NO malloc — the caller owns the buffers (overlay BSS).
 *
 *   storage   — RC_HASH_CAP entries, caller-allocated (overlay BSS)
 *   bank_off  — 256 entries: offset of each bank's hash region in storage
 *   bank_mask — 256 entries: (size-1) mask for each bank (0 = bank inactive)
 *   addrs     — RC_NSITES 24-bit kpc values (k<<16 | pc), from XIP blob
 *   nsites    — site count
 *   fns       — function pointer table, from XIP blob */
void rc_dispatch_init(rc_entry_t *storage, uint32_t *bank_off, uint32_t *bank_mask,
                      const uint32_t *addrs, uint32_t nsites,
                      void (**fns)(Cpu *));

/* Clear the dispatch state. g_rc_active = false. NO free (caller owns buffers). */
void rc_dispatch_reset(void);

/* Look up a (bank, pc) pair in the hash table.
 * Returns site_id (1-based) if found, 0 if not (interpreter fallback).
 * Called from cpu_runOpcode on every opcode when g_rc_active is true. */
uint16_t rc_dispatch_lookup(uint8_t bank, uint16_t pc);

/* Call site id (1-based). Thin wrapper so cpu.c doesn't need the fn table. */
static inline void rc_dispatch_call(uint16_t id, Cpu *cpu) {
  extern void (**g_rc_fns)(Cpu *);
  g_rc_fns[id - 1](cpu);
}

#endif /* RC_DISPATCH_H */
