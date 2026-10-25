// Short synthesized chimes, one per event kind, played through the board's
// I2S amplifier (16 kHz, 16-bit mono). No audio files: each chime is 2-3
// notes of a sine with a soft second harmonic, shaped by a 6 ms attack and an
// exponential decay so notes start and stop without clicks.
#include "sound.h"

#include "board.h"

#if AUDIO_I2S
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

#include "model.h"
#include "settings.h"

static const int RATE = 16000;

struct Note {
  uint16_t hz;  // 0 = rest
  uint16_t ms;
};
struct Chime {
  uint8_t n;
  Note notes[3];
};

// boosts, likes, collects, downloads, prints, comments; then the test chime.
static const Chime CHIMES[NKIND + 1] = {
  {3, {{784, 90}, {1047, 90}, {1568, 220}}},  // boost: bright rising arpeggio
  {2, {{1319, 80}, {1760, 180}}},             // like: quick high pair
  {2, {{880, 90}, {1175, 200}}},              // collect: gentle up-step
  {2, {{1175, 90}, {880, 200}}},              // download: down-step
  {3, {{659, 90}, {831, 90}, {988, 240}}},    // print: major triad
  {3, {{1047, 70}, {0, 40}, {1047, 160}}},    // comment: double tap
  {1, {{1319, 150}}},                         // test / volume check
};

static I2SClass i2s;
static QueueHandle_t q;
static bool ok = false;

static void playChime(const Chime& c, int peak) {
  static int16_t buf[256];
  for (int i = 0; i < c.n; i++) {
    const Note& nt = c.notes[i];
    int total = RATE * nt.ms / 1000, attack = RATE * 6 / 1000;
    float ph = 0, step = 2.0f * (float)M_PI * nt.hz / RATE;
    // Decay to ~1% by the end of the note.
    float decay = expf(-4.6f / total), env = 1.0f;
    for (int done = 0; done < total;) {
      int n = min((int)(sizeof(buf) / sizeof(buf[0])), total - done);
      for (int j = 0; j < n; j++, done++) {
        float v = 0;
        if (nt.hz) {
          float a = done < attack ? (float)done / attack : env;
          if (done >= attack) env *= decay;
          v = (sinf(ph) + 0.25f * sinf(2 * ph)) * 0.8f * a;
          ph += step;
          if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
        }
        buf[j] = (int16_t)(v * peak);
      }
      i2s.write((uint8_t*)buf, n * sizeof(int16_t));
    }
  }
  // Flush with silence: the DMA ring repeats its last buffer when starved.
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < RATE * 150 / 1000 / 256 + 1; i++) i2s.write((uint8_t*)buf, sizeof(buf));
}

static volatile bool playing = false;

static void soundTask(void*) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(q, &id, portMAX_DELAY) == pdTRUE && ok) {
      playing = true;
      playChime(CHIMES[id], setting_value(SET_VOLUME));
      playing = false;
    }
  }
}

// Before a firmware update: no more chimes, and the I2S DMA (and its
// interrupt) stops, so nothing runs from it while flash writes disable the cache.
void sound_stop() {
  if (!ok) return;
  ok = false;
  for (int i = 0; i < 50 && playing; i++) delay(10);
  i2s.end();
}

void sound_init() {
  i2s.setPins(AUDIO_BCLK, AUDIO_LRCLK, AUDIO_DOUT);
  ok = i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  if (!ok) {
    Serial.println("I2S init failed");
    return;
  }
  Serial.println("I2S OK");
  q = xQueueCreate(4, sizeof(uint8_t));
  xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 2, nullptr, 0);
}

static void enqueue(uint8_t id) {
  if (ok) xQueueSend(q, &id, 0);  // drop when busy rather than stall the UI
}

void sound_event(int kind) {
  if (kind >= 0 && kind < NKIND && setting_value(SET_SOUND)) enqueue(kind);
}

void sound_test() {
  if (setting_value(SET_SOUND)) enqueue(NKIND);
}

#else
void sound_init() {}
void sound_stop() {}
void sound_event(int) {}
void sound_test() {}
#endif
