#ifndef _AUDIO_CODEC_UTIL_H_
#define _AUDIO_CODEC_UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Downsamples 16kHz mono PCM to 8kHz (2-tap average of each adjacent pair,
// then drop every other sample — cheap anti-aliasing, adequate for voice)
// and G711 A-law encodes the result. On success *out_alaw is malloc'd
// (caller frees) and *out_len is its length in bytes — exactly one byte per
// 8kHz sample, so roughly a quarter of the original 16-bit/16kHz size.
esp_err_t audio_codec_encode_alaw(const int16_t *pcm16k, size_t n_samples_16k,
                                   uint8_t **out_alaw, size_t *out_len);

// G711 A-law decodes 8kHz-source data, then upsamples back to 16kHz mono PCM
// via linear interpolation. The upsample exists purely to feed this board's
// fixed-16kHz speaker pipeline for on-device playback of a previously-saved
// clip — the file itself, and anything served to the desktop, stays
// honestly labeled as 8kHz (see session_storage.c / the backend routes).
// On success *out_pcm is malloc'd (caller frees) and *out_n_samples is the
// sample count at 16kHz.
esp_err_t audio_codec_decode_alaw_to_16k(const uint8_t *alaw, size_t alaw_len,
                                          int16_t **out_pcm, size_t *out_n_samples);

#endif
