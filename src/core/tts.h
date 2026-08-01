#ifndef DIO_VOICE_TTS_H
#define DIO_VOICE_TTS_H

#include <stddef.h>

#include <windows.h>

typedef enum DioTtsResult {
    DIO_TTS_SUCCEEDED = 0,
    DIO_TTS_CANCELLED,
    DIO_TTS_FAILED
} DioTtsResult;

typedef struct DioTtsServer DioTtsServer;

typedef void (*DioTtsStartedCallback)(
    void *context);

typedef struct DioTtsConfig {
    DioTtsServer *server;
} DioTtsConfig;

DioTtsServer *dio_tts_server_open(
    const wchar_t *bundle_directory,
    const wchar_t *state_directory,
    char *error_text,
    size_t error_text_capacity);

void dio_tts_server_cancel(
    DioTtsServer *server);

void dio_tts_server_close(
    DioTtsServer *server);

DioTtsResult dio_tts_speak_text(
    const DioTtsConfig *config,
    const char *utf8_text,
    size_t text_length,
    HANDLE cancel_event,
    HANDLE stop_event,
    DioTtsStartedCallback started_callback,
    void *started_context,
    char *error_text,
    size_t error_text_capacity);

#endif
