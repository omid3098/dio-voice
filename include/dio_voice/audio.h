#ifndef DIO_VOICE_AUDIO_H
#define DIO_VOICE_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIO_VOICE_SAMPLE_RATE 16000u
#define DIO_VOICE_FRAME_SAMPLES 512u

typedef enum DioAudioResult {
    DIO_AUDIO_OK = 0,
    DIO_AUDIO_INVALID_ARGUMENT,
    DIO_AUDIO_UNSUPPORTED_FORMAT,
    DIO_AUDIO_OUT_OF_MEMORY,
    DIO_AUDIO_IO_FAILURE
} DioAudioResult;

typedef struct DioAudioPcm {
    int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate;
    uint16_t channel_count;
} DioAudioPcm;

/*
 * Reads an uncompressed RIFF/WAVE file and converts PCM16 input to owned,
 * mono, 16 kHz samples. error_text may be NULL; when supplied it receives a
 * short UTF-8 diagnostic.
 */
DioAudioResult dio_audio_read_wav_mono_16k(
    const wchar_t *path,
    DioAudioPcm *output,
    char *error_text,
    size_t error_text_capacity);

void dio_audio_pcm_free(
    DioAudioPcm *pcm);

#ifdef __cplusplus
}
#endif

#endif
