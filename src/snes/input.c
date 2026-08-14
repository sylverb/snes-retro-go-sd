
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "input.h"
#include "snes.h"
#include "snes_gnw_alloc.h"

Input* input_init(Snes* snes) {
  Input* input = snes_zalloc(sizeof(Input));
  if (!input) return NULL;
  input->snes = snes;
  // TODO: handle (where?)
  input->type = 1;
  input->currentState = 0;
  return input;
}

void input_free(Input* input) {
  snes_zfree(input);
}

void input_reset(Input* input) {
  input->latchLine = false;
  input->latchedState = 0;
}

/* Bits come out in the order the shift register holds them: B, Y, Select, Start,
 * Up, Down, Left, Right, A, X, L, R — which is what $4218/$4219 report, MSB first.
 * After the twelve real buttons the line reads back as 1s, as on hardware. */
static uint16_t input_serialState(Input* input) {
  uint16_t x = input->currentState, r = 0;
  for (int i = 0; i < 16; i++, x >>= 1)
    r = r * 2 + (x & 1);
  return r;
}

void input_latch(Input* input, bool value) {
  input->latchLine = value;
  if (input->latchLine)
    input->latchedState = input_serialState(input);
}

uint8_t input_read(Input* input) {
  if (input->latchLine)
    input->latchedState = input_serialState(input);
  uint8_t ret = (input->latchedState >> 15) & 1;
  input->latchedState = (uint16_t)((input->latchedState << 1) | 1);
  return ret;
}

