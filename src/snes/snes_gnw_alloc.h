/* Device alloc helpers — map jshsakura's DTCM-heap mallocs onto dtc_*.
 * Apu/ARAM is also DTCM (apu.c): jsh put it on AHB, which is too small here. */
#ifndef SNES_GNW_ALLOC_H
#define SNES_GNW_ALLOC_H

#include <stddef.h>
#include <string.h>

#ifdef TARGET_GNW
#include "gw_malloc.h"

static inline void *snes_zalloc(size_t n)
{
  void *p = dtc_calloc(1, n);
  if (!p || (uintptr_t)p == (uintptr_t)-1)
    return NULL;
  return p;
}

static inline void snes_zfree(void *p) { (void)p; /* DTCM bump: no free */ }

#else
#include <stdlib.h>
static inline void *snes_zalloc(size_t n) { return calloc(1, n); }
static inline void snes_zfree(void *p) { free(p); }
#endif

#endif
