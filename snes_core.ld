/*
 * SNES (LakeSnes) core — RAM_EMU + ITCM for the Thumb-2 65816 engine.
 *
 * Match the monolithic firmware: 65816 Thumb-2 (~58KB) owns ITCM; SPC Thumb-2
 * (~14KB) stays in RAM_EMU — both do not fit in 64KB ITCM together.
 */

INCLUDE gnw_ram_emu.ld
INCLUDE gnw_itcm_core.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  ITCM    (xrw) : ORIGIN = __ITCM_CORE_START__, LENGTH = __ITCM_CORE_LENGTH__
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  itcm PT_LOAD FLAGS(5);
}

SECTIONS
{
  /* Segment 0 first so ITCM's AT>RAM_EMU LMA lands after RAM_EMU code/data
   * (not overlapping __RAM_EMU_START__ — same ordering as cores/pce). */
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    /* SPC Thumb-2 engine — RAM_EMU, same placement as monolithic overlay. */
    *(.itcm_snes_interp.thumb2_spc .itcm_snes_interp.thumb2_spc.*)

    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __CORE_INIT_ARRAY_END__ = .;
  } >RAM_EMU :text

  .data :
  {
    *(.data*)
  } >RAM_EMU :data

  __CORE_CODE_END__ = .;

  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
  } >RAM_EMU :data

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: SNES core code+data+bss overflows RAM_EMU")

  .core_itcm __ITCM_CORE_START__ :
  {
    __CORE_ITCM_CODE_START__ = .;
    *(.itcm_snes_interp.thumb2 .itcm_snes_interp.thumb2.*)
    *(.itcm_snes_interp*)
    . = ALIGN(4);
    __CORE_ITCM_CODE_END__ = .;
  } >ITCM AT>RAM_EMU :itcm

  __CORE_ITCM_BSS_END__ = __CORE_ITCM_CODE_END__;

  ASSERT(__CORE_ITCM_BSS_END__ <= __ITCM_CORE_START__ + __ITCM_CORE_LENGTH__,
         "Error: SNES core ITCM (65816 Thumb-2) overflows 64K ITCM")

  __exidx_start = .;
  __exidx_end = .;

  /DISCARD/ :
  {
    *(.fini_array*)
    *(.preinit_array*)
    *(.ARM.exidx*)
    *(.ARM.extab*)
    *(.comment)
    *(.ARM.attributes)
  }
}
