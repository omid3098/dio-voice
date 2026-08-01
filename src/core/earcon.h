#ifndef DIO_VOICE_EARCON_H
#define DIO_VOICE_EARCON_H

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>

typedef struct DioEarcon DioEarcon;

DioEarcon *dio_earcon_open(
    HANDLE stop_event,
    char *error_text,
    size_t error_text_capacity);

bool dio_earcon_play_listening(
    DioEarcon *earcon,
    ULONGLONG requested_at_ms,
    unsigned int *start_latency_ms,
    char *error_text,
    size_t error_text_capacity);
bool dio_earcon_play_processing(
    DioEarcon *earcon,
    ULONGLONG requested_at_ms,
    unsigned int *start_latency_ms,
    char *error_text,
    size_t error_text_capacity);

void dio_earcon_close(
    DioEarcon *earcon);

#endif
