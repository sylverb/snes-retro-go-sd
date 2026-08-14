/* Compile-time guard: fail the SNES_THUMB2_CPU=1 build if Cpu's layout drifts
 * from the numeric constants the Thumb-2 engine bakes in. No runtime cost --
 * this TU is all _Static_assert, so it lowers to an empty object. Kept in its
 * own TU (rather than folded into cpu.c) so the asserts survive even when the
 * engine's own objects are excluded by conditional compilation. */
#include <stddef.h>
#include "snes/cpu.h"
#include "cpu_thumb2_offsets.h"

#define CPU_CK(field, val) \
    _Static_assert(offsetof(Cpu, field) == (val), \
                   "Cpu::" #field " offset drift -- update cpu_thumb2_offsets.h")

CPU_CK(mem,          CPU_MEM);
CPU_CK(memType,      CPU_MEMTYPE);
CPU_CK(a,            CPU_A);
CPU_CK(x,            CPU_X);
CPU_CK(y,            CPU_Y);
CPU_CK(sp,           CPU_SP);
CPU_CK(pc,           CPU_PC);
CPU_CK(dp,           CPU_DP);
CPU_CK(k,            CPU_K);
CPU_CK(db,           CPU_DB);
CPU_CK(c,            CPU_C);
CPU_CK(z,            CPU_Z);
CPU_CK(v,            CPU_V);
CPU_CK(n,            CPU_N);
CPU_CK(i,            CPU_I);
CPU_CK(d,            CPU_D);
CPU_CK(xf,           CPU_XF);
CPU_CK(mf,           CPU_MF);
CPU_CK(e,            CPU_E);
CPU_CK(irqWanted,    CPU_IRQWANTED);
CPU_CK(nmiWanted,    CPU_NMIWANTED);
CPU_CK(waiting,      CPU_WAITING);
CPU_CK(stopped,      CPU_STOPPED);
CPU_CK(cyclesUsed,   CPU_CYCLESUSED);
CPU_CK(spBreakpoint, CPU_SPBREAKPOINT);
CPU_CK(in_emu,       CPU_IN_EMU);

/* The Snes offsets were asserted by nobody. A double added to Snes moved
 * romPageBase from 108 to 116 and the engine's inline ROM page cache went on
 * comparing the old word: it missed every time, so 79.5% of all CPU reads --
 * the ones the cache exists to serve without a call -- fell through to
 * snes_cpuRead, which the device profile then reported as 10.5% of the frame.
 * Nothing failed, nothing was slower by any test we had; it just quietly cost
 * a fifth of the bus path. Assert them. */
#include "snes/snes.h"
_Static_assert(offsetof(Snes, romPageBase)   == SNES_ROMPAGEBASE,   "SNES_ROMPAGEBASE stale");
_Static_assert(offsetof(Snes, romPageTag)    == SNES_ROMPAGETAG,    "SNES_ROMPAGETAG stale");
_Static_assert(offsetof(Snes, cpuCyclesLeft) == SNES_CPUCYCLESLEFT, "SNES_CPUCYCLESLEFT stale");
_Static_assert(offsetof(Snes, cpuMemOps)     == SNES_CPUMEMOPS,     "SNES_CPUMEMOPS stale");
_Static_assert(offsetof(Snes, ram) == SNES_RAM, "SNES_RAM stale");
