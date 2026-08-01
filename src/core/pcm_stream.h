#ifndef DIO_VOICE_PCM_STREAM_H
#define DIO_VOICE_PCM_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <windows.h>

#include "tts.h"

typedef struct DioPcmStream DioPcmStream;

typedef void (*DioPcmStreamStartedCallback)(
    void *context);

DioPcmStream *dio_pcm_stream_create(
    unsigned int sample_rate);

bool dio_pcm_stream_write(
    DioPcmStream *stream,
    const int16_t *samples,
    size_t sample_count);

bool dio_pcm_stream_finish(
    DioPcmStream *stream);

void dio_pcm_stream_cancel(
    DioPcmStream *stream);

DioTtsResult dio_pcm_stream_play(
    DioPcmStream *stream,
    bool null_backend,
    HANDLE cancel_event,
    HANDLE stop_event,
    DioPcmStreamStartedCallback started_callback,
    void *started_context,
    char *error_text,
    size_t error_text_capacity);

void dio_pcm_stream_destroy(
    DioPcmStream *stream);

#endif
