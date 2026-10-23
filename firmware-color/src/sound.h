#pragma once
// Event chimes on boards with an I2S amplifier (AUDIO_I2S). Everything here
// is a no-op elsewhere. Playback runs on its own task, so calls never block.

void sound_init();
void sound_event(int kind);  // chime for a notified event (respects Sound)
void sound_test();           // confirmation chime after a Sound/Volume change
