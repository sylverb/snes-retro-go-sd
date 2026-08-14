
#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Input Input;

#include "snes.h"

struct Input {
  Snes* snes;
  uint8_t type;
  // for controller
  uint16_t currentState; // actual state
  /* The serial ($4016/$4017) read path. Super Metroid's reimplementation never
   * touches a controller port — it is handed its buttons directly — so this was
   * stripped along with the register handler. Any actual SNES game needs it: the
   * manual read is how a great many of them poll the pad. */
  bool latchLine;
  uint16_t latchedState;
};

Input* input_init(Snes* snes);
void input_free(Input* input);
void input_reset(Input* input);
void input_latch(Input* input, bool value);
uint8_t input_read(Input* input);

#endif
