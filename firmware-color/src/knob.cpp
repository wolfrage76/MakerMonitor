// KY-040 rotary encoder: quadrature decoded in an ISR, button debounced in loop.
#include "knob.h"

#include <Arduino.h>

#include "board.h"

#if INPUT_KNOB

static volatile int32_t rawCount = 0;
static volatile uint8_t lastAB = 0;
static int32_t consumed = 0;

// Index = (prev AB << 2) | new AB; invalid transitions count 0.
static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR onEdge() {
  uint8_t ab = (digitalRead(KNOB_PIN_A) << 1) | digitalRead(KNOB_PIN_B);
  rawCount += QDEC[(lastAB << 2) | ab];
  lastAB = ab;
}

static bool swDown = false;
static uint32_t swChangedAt = 0;
static uint32_t swDownAt = 0;
static bool longFired = false;
static const uint32_t DEBOUNCE_MS = 25;
static const uint32_t LONG_MS = 800;

void knob_init() {
  pinMode(KNOB_PIN_A, INPUT_PULLUP);
  pinMode(KNOB_PIN_B, INPUT_PULLUP);
  pinMode(KNOB_PIN_SW, INPUT_PULLUP);
  lastAB = (digitalRead(KNOB_PIN_A) << 1) | digitalRead(KNOB_PIN_B);
  attachInterrupt(KNOB_PIN_A, onEdge, CHANGE);
  attachInterrupt(KNOB_PIN_B, onEdge, CHANGE);
}

KnobInput knob_poll() {
  KnobInput in;
  int32_t c;
  noInterrupts();
  c = rawCount;
  interrupts();
  int32_t detents = (c - consumed) / KNOB_STEPS_PER_DETENT;
  if (detents) {
    consumed += detents * KNOB_STEPS_PER_DETENT;
    in.steps = KNOB_REVERSE ? -detents : detents;
  }

  uint32_t now = millis();
  bool down = digitalRead(KNOB_PIN_SW) == LOW;
  if (down != swDown && now - swChangedAt > DEBOUNCE_MS) {
    swDown = down;
    swChangedAt = now;
    if (down) {
      swDownAt = now;
      longFired = false;
    } else if (!longFired) {
      in.click = true;
    }
  }
  if (swDown && !longFired && now - swDownAt >= LONG_MS) {
    longFired = true;
    in.longPress = true;
  }
  return in;
}

#else
// Board without a knob (touch or built-in controls).
void knob_init() {}
KnobInput knob_poll() { return KnobInput(); }
#endif
