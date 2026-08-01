#ifndef DIO_VOICE_AUDIO_CAPTURE_H
#define DIO_VOICE_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "miniaudio_config.h"

typedef void (*DioCaptureCallback)(
    void *context,
    const int16_t *samples,
    size_t sample_count);

typedef struct DioAudioCapture {
    ma_context context;
    ma_device device;
    DioCaptureCallback callback;
    void *callback_context;
    bool context_ready;
    bool device_ready;
    bool started;
} DioAudioCapture;

/* Exact, case-insensitive display-name match used only for schema migration. */
bool dio_audio_capture_name_equal(
    const char *device_name_utf8,
    const wchar_t *preferred_name);

bool dio_audio_capture_open(
    const wchar_t *preferred_device_name,
    const wchar_t *preferred_device_id,
    DioCaptureCallback callback,
    void *callback_context,
    DioAudioCapture *capture,
    char *error_text,
    size_t error_text_capacity);

bool dio_audio_capture_start(
    DioAudioCapture *capture,
    char *error_text,
    size_t error_text_capacity);

void dio_audio_capture_close(
    DioAudioCapture *capture);

#endif
