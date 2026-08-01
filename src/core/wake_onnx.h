#ifndef DIO_VOICE_WAKE_ONNX_H
#define DIO_VOICE_WAKE_ONNX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include "dio_voice/audio.h"
#include "ort_runtime.h"

#define DIO_SILERO_STATE_VALUES 256u

typedef struct DioPorcupineHandle DioPorcupineHandle;

typedef struct DioWakePorcupine {
    HMODULE module;
    DioPorcupineHandle *handle;
    int (__cdecl *sample_rate)(void);
    int (__cdecl *init)(
        const char *model_path,
        int32_t keyword_count,
        const char *const *keyword_paths,
        const float *sensitivities,
        DioPorcupineHandle **handle);
    void (__cdecl *destroy)(DioPorcupineHandle *handle);
    int (__cdecl *process)(
        DioPorcupineHandle *handle,
        const int16_t *samples,
        int32_t *keyword_index);
    const char *(__cdecl *version)(void);
    int32_t (__cdecl *frame_length)(void);
} DioWakePorcupine;

typedef struct DioSileroVad {
    DioOrtRuntime *runtime;
    DioOrtSession session;
    float state[DIO_SILERO_STATE_VALUES];
    float context[64];
} DioSileroVad;

bool dio_wake_open(
    const wchar_t *model_directory,
    float sensitivity,
    DioWakePorcupine *wake,
    char *error_text,
    size_t error_text_capacity);

bool dio_wake_feed(
    DioWakePorcupine *wake,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES],
    bool *triggered,
    char *error_text,
    size_t error_text_capacity);

const char *dio_wake_version(
    const DioWakePorcupine *wake);

void dio_wake_close(
    DioWakePorcupine *wake);

bool dio_silero_open(
    DioOrtRuntime *runtime,
    const wchar_t *model_path,
    DioSileroVad *vad,
    char *error_text,
    size_t error_text_capacity);

void dio_silero_reset(
    DioSileroVad *vad);

bool dio_silero_probability(
    DioSileroVad *vad,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES],
    float *probability,
    char *error_text,
    size_t error_text_capacity);

const float *dio_silero_state_snapshot(
    const DioSileroVad *vad);

void dio_silero_close(
    DioSileroVad *vad);

#endif
