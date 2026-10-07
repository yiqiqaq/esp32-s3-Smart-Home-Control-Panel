#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Onboard audio for the 4.3C: ES8311 speaker codec (playback) + ES7210 mic
 * ADC (capture) on shared I2S port 1 (pins in bsp_43c.h), I2C control on the
 * shared bus. The speaker amplifier is enabled through CH422G IO3.
 * Voice pipeline uses 16 kHz / 16-bit / mono in both directions. */

#define BSP_AUDIO_SAMPLE_RATE 16000
#define BSP_AUDIO_MS_TO_BYTES(ms) ((size_t)(BSP_AUDIO_SAMPLE_RATE * 2 * (ms) / 1000))

esp_err_t bsp_audio_init(void);
bool bsp_audio_ready(void);

/* Speaker amplifier (CH422G IO3). Playback helpers enable it on demand. */
esp_err_t bsp_audio_pa_enable(bool on);

/* Blocking capture into buf (16k/16-bit/mono). */
esp_err_t bsp_audio_record(uint8_t *buf, size_t bytes, uint32_t timeout_ms);
/* Blocking playback of 16k/16-bit/mono PCM. Enables the PA while playing. */
esp_err_t bsp_audio_play(const uint8_t *buf, size_t bytes);

esp_err_t bsp_audio_set_volume(int volume);  /* 0..100 */
esp_err_t bsp_audio_set_mic_gain(float gain); /* dB, 0..37.5 per ES7210 */

#ifdef __cplusplus
}
#endif
