#ifndef DIO_VOICE_VOSK_RUNTIME_H
#define DIO_VOICE_VOSK_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <windows.h>

#include "vosk_api.h"

typedef struct DioVoskRuntime {
    HMODULE module;
    VoskModel *model;
    VoskModel *(__cdecl *model_new)(const char *path);
    void(__cdecl *model_free)(VoskModel *model);
    VoskRecognizer *(__cdecl *recognizer_new)(
        VoskModel *model,
        float sample_rate);
    int(__cdecl *accept_waveform_s)(
        VoskRecognizer *recognizer,
        const short *samples,
        int sample_count);
    const char *(__cdecl *result)(VoskRecognizer *recognizer);
    const char *(__cdecl *partial_result)(VoskRecognizer *recognizer);
    const char *(__cdecl *final_result)(VoskRecognizer *recognizer);
    void(__cdecl *recognizer_free)(VoskRecognizer *recognizer);
    void(__cdecl *set_log_level)(int level);
} DioVoskRuntime;

typedef struct DioVoskRecognizer {
    DioVoskRuntime *runtime;
    VoskRecognizer *handle;
    char *finalized_text;
    size_t finalized_length;
} DioVoskRecognizer;

bool dio_vosk_runtime_open(
    const wchar_t *library_path,
    const wchar_t *model_directory,
    DioVoskRuntime *runtime,
    char *error_text,
    size_t error_text_capacity);

void dio_vosk_runtime_close(
    DioVoskRuntime *runtime);

bool dio_vosk_recognizer_open(
    DioVoskRuntime *runtime,
    DioVoskRecognizer *recognizer,
    char *error_text,
    size_t error_text_capacity);

/*
 * Returns an owned snapshot containing finalized parts plus the current
 * partial. endpoint is true when Vosk finalized a part during this call.
 */
bool dio_vosk_recognizer_feed(
    DioVoskRecognizer *recognizer,
    const int16_t *samples,
    size_t sample_count,
    char **text,
    bool *endpoint,
    char *error_text,
    size_t error_text_capacity);

bool dio_vosk_recognizer_finish(
    DioVoskRecognizer *recognizer,
    char **text,
    char *error_text,
    size_t error_text_capacity);

void dio_vosk_recognizer_close(
    DioVoskRecognizer *recognizer);

bool dio_vosk_transcribe(
    DioVoskRuntime *runtime,
    const int16_t *samples,
    size_t sample_count,
    char **text,
    char *error_text,
    size_t error_text_capacity);

void dio_vosk_text_free(
    char *text);

#endif
