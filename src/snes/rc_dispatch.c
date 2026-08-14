/* rc_dispatch.c — per-bank open-addressing hash dispatch for the static recompiler.
 *
 * Portable: used by both the M7 rig (flat-linked, no XIP) and the device
 * (XIP blob cached in flash). The caller (main_snes.c on device, rig entry
 * on host) supplies the hash storage + bank-offset/mask buffers and the
 * rc_addrs/rc_fns pointers at activation time.
 *
 * NO heap allocation. The hash table (~85 KB for SMW's 8371 sites, 7 banks)
 * lives in caller-provided overlay BSS (RAM_EMU), not the 81 KB DTCM heap.
 * The previous heap-based design OOM-crashed the device on activation.
 *
 * Hash: Knuth multiplicative, per-bank open-addressing, LF~0.5.
 * Rig-measured cost: ~4.57M insn/frame (−42.3% vs interpreter 7.92M). */
#include "rc_dispatch.h"
#include <string.h>

bool g_rc_active = false;
void (**g_rc_fns)(Cpu *) = NULL;

/* Pointers into the caller-provided buffers. Set by rc_dispatch_init,
 * cleared by rc_dispatch_reset. The caller owns the storage. */
static rc_entry_t *g_rc_storage;
static uint32_t   *g_rc_bank_off;
static uint32_t   *g_rc_bank_mask;

void rc_dispatch_init(rc_entry_t *storage, uint32_t *bank_off, uint32_t *bank_mask,
                      const uint32_t *addrs, uint32_t nsites,
                      void (**fns)(Cpu *)) {
  rc_dispatch_reset();
  g_rc_fns = fns;
  g_rc_storage = storage;
  g_rc_bank_off = bank_off;
  g_rc_bank_mask = bank_mask;

  /* Pass 1: count sites per bank. */
  int counts[256] = {0};
  for (uint32_t i = 0; i < nsites; i++)
    counts[addrs[i] >> 16]++;

  /* Assign per-bank hash regions: each bank gets next_pow2(count*2) slots.
   * Zero the region so id==0 means "empty slot". */
  uint32_t off = 0;
  for (int b = 0; b < 256; b++) {
    bank_off[b] = off;
    if (!counts[b]) { bank_mask[b] = 0; continue; }
    int sz = 1;
    while (sz < counts[b] * 2) sz <<= 1;
    bank_mask[b] = (uint32_t)(sz - 1);
    memset(storage + off, 0, (size_t)sz * sizeof(rc_entry_t));
    off += (uint32_t)sz;
  }

  /* Fill: open-addressing with linear probing (Knuth multiplicative hash). */
  for (uint32_t i = 0; i < nsites; i++) {
    uint8_t b = (uint8_t)(addrs[i] >> 16);
    uint16_t pc = (uint16_t)(addrs[i] & 0xffff);
    uint32_t mask = bank_mask[b];
    uint32_t h = (pc * 2654435761u) & mask;
    rc_entry_t *ht = storage + bank_off[b];
    while (ht[h].id)
      h = (h + 1) & mask;
    ht[h].pc = pc;
    ht[h].id = (uint16_t)(i + 1);
  }

  g_rc_active = true;
}

void rc_dispatch_reset(void) {
  g_rc_storage = NULL;
  g_rc_bank_off = NULL;
  g_rc_bank_mask = NULL;
  g_rc_fns = NULL;
  g_rc_active = false;
}

uint16_t rc_dispatch_lookup(uint8_t bank, uint16_t pc) {
  uint32_t mask = g_rc_bank_mask[bank];
  if (!mask) return 0;
  rc_entry_t *ht = g_rc_storage + g_rc_bank_off[bank];
  uint32_t h = (pc * 2654435761u) & mask;
  for (;;) {
    uint16_t eid = ht[h].id;
    if (!eid) return 0;
    if (ht[h].pc == pc) return eid;
    h = (h + 1) & mask;
  }
}
