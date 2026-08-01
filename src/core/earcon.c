#include "earcon.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniaudio_config.h"

#define DIO_EARCON_SAMPLE_RATE 24000u
#define DIO_EARCON_DURATION_FRAMES 7200u
#define DIO_EARCON_TIMEOUT_MS 2000u
#define DIO_PI 3.14159265358979323846

typedef enum DioEarconKind {
    DIO_EARCON_LISTENING = 0,
    DIO_EARCON_PROCESSING,
    DIO_EARCON_COUNT
} DioEarconKind;

struct DioEarcon {
    ma_device device;
    HANDLE stop_event;
    HANDLE ready_event;
    HANDLE done_event;
    HANDLE timeout_event;
    CRITICAL_SECTION play_lock;
    CRITICAL_SECTION state_lock;
    bool device_ready;
    bool device_started;
    volatile LONG active;
    size_t cursor;
    DioEarconKind kind;
    ULONGLONG requested_at_ms;
    unsigned int latency_ms;
    int16_t tones[DIO_EARCON_COUNT][DIO_EARCON_DURATION_FRAMES];
};

static void dio_earcon_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination != NULL && capacity != 0u) {
        (void)strncpy_s(destination, capacity, message, _TRUNCATE);
    }
}

static void dio_earcon_generate(
    int16_t output[DIO_EARCON_DURATION_FRAMES],
    bool descending)
{
    size_t index;

    for (index = 0u; index < DIO_EARCON_DURATION_FRAMES; ++index) {
        const double time =
            (double)index / DIO_EARCON_SAMPLE_RATE;
        const double duration =
            (double)DIO_EARCON_DURATION_FRAMES /
            DIO_EARCON_SAMPLE_RATE;
        const double progress =
            (double)index / DIO_EARCON_DURATION_FRAMES;
        const double envelope = sin(DIO_PI * progress) * 0.28;
        const double start_frequency = descending ? 880.0 : 660.0;
        const double end_frequency = descending ? 660.0 : 880.0;
        const double slope =
            (end_frequency - start_frequency) / duration;
        const double phase =
            2.0 * DIO_PI *
            (start_frequency * time +
             0.5 * slope * time * time);
        output[index] =
            (int16_t)(sin(phase) * envelope * INT16_MAX);
    }
}

static void dio_earcon_callback(
    ma_device *device,
    void *output,
    const void *input,
    ma_uint32 frame_count)
{
    DioEarcon *earcon = (DioEarcon *)device->pUserData;
    int16_t *frames = (int16_t *)output;

    (void)input;
    (void)memset(
        frames,
        0,
        (size_t)frame_count * sizeof(*frames));
    if (earcon == NULL) {
        return;
    }
    (void)SetEvent(earcon->ready_event);
    if (InterlockedCompareExchange(
            &earcon->active,
            0,
            0) == 0) {
        return;
    }

    EnterCriticalSection(&earcon->state_lock);
    if (InterlockedCompareExchange(
            &earcon->active,
            0,
            0) != 0) {
        if (earcon->cursor == DIO_EARCON_DURATION_FRAMES) {
            InterlockedExchange(&earcon->active, 0);
            (void)SetEvent(earcon->done_event);
        } else {
            const size_t remaining =
                DIO_EARCON_DURATION_FRAMES - earcon->cursor;
            const size_t copied =
                remaining < frame_count ? remaining : frame_count;

            if (earcon->latency_ms == UINT_MAX) {
                const ULONGLONG elapsed =
                    GetTickCount64() - earcon->requested_at_ms;
                earcon->latency_ms =
                    elapsed > UINT_MAX
                        ? UINT_MAX
                        : (unsigned int)elapsed;
            }
            (void)memcpy(
                frames,
                earcon->tones[earcon->kind] + earcon->cursor,
                copied * sizeof(*frames));
            earcon->cursor += copied;
        }
    }
    LeaveCriticalSection(&earcon->state_lock);
}

DioEarcon *dio_earcon_open(
    HANDLE stop_event,
    char *error_text,
    size_t error_text_capacity)
{
    DioEarcon *earcon =
        (DioEarcon *)calloc(1u, sizeof(*earcon));
    ma_device_config config;

    if (earcon == NULL) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "could not allocate earcon player");
        return NULL;
    }
    InitializeCriticalSection(&earcon->play_lock);
    InitializeCriticalSection(&earcon->state_lock);
    earcon->stop_event = stop_event;
    earcon->ready_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    earcon->done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    earcon->timeout_event =
        CreateWaitableTimerW(NULL, TRUE, NULL);
    if (earcon->ready_event == NULL ||
        earcon->done_event == NULL ||
        earcon->timeout_event == NULL) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "could not prepare earcon player");
        dio_earcon_close(earcon);
        return NULL;
    }

    dio_earcon_generate(
        earcon->tones[DIO_EARCON_LISTENING],
        false);
    dio_earcon_generate(
        earcon->tones[DIO_EARCON_PROCESSING],
        true);
    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 1u;
    config.sampleRate = DIO_EARCON_SAMPLE_RATE;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dio_earcon_callback;
    config.pUserData = earcon;
    if (ma_device_init(NULL, &config, &earcon->device) != MA_SUCCESS) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "could not open earcon playback device");
        dio_earcon_close(earcon);
        return NULL;
    }
    earcon->device_ready = true;
    if (ma_device_start(&earcon->device) != MA_SUCCESS) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "could not start earcon playback device");
        dio_earcon_close(earcon);
        return NULL;
    }
    earcon->device_started = true;
    if (WaitForSingleObject(
            earcon->ready_event,
            DIO_EARCON_TIMEOUT_MS) != WAIT_OBJECT_0) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "earcon playback device did not start");
        dio_earcon_close(earcon);
        return NULL;
    }
    return earcon;
}

static bool dio_earcon_play(
    DioEarcon *earcon,
    ULONGLONG requested_at_ms,
    unsigned int *start_latency_ms,
    char *error_text,
    size_t error_text_capacity,
    DioEarconKind kind)
{
    HANDLE waits[3];
    DWORD wait_count = 2u;
    DWORD wait;
    LARGE_INTEGER timeout;
    bool played = false;

    if (earcon == NULL ||
        start_latency_ms == NULL ||
        kind >= DIO_EARCON_COUNT) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "invalid earcon arguments");
        return false;
    }
    *start_latency_ms = UINT_MAX;
    EnterCriticalSection(&earcon->play_lock);
    (void)ResetEvent(earcon->done_event);
    (void)CancelWaitableTimer(earcon->timeout_event);
    timeout.QuadPart = -20000000LL;
    if (!SetWaitableTimer(
            earcon->timeout_event,
            &timeout,
            0,
            NULL,
            NULL,
            FALSE)) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "could not prepare earcon playback");
        goto done;
    }

    EnterCriticalSection(&earcon->state_lock);
    earcon->cursor = 0u;
    earcon->kind = kind;
    earcon->requested_at_ms = requested_at_ms;
    earcon->latency_ms = UINT_MAX;
    InterlockedExchange(&earcon->active, 1);
    LeaveCriticalSection(&earcon->state_lock);

    waits[0] = earcon->done_event;
    waits[1] = earcon->timeout_event;
    if (earcon->stop_event != NULL) {
        waits[wait_count++] = earcon->stop_event;
    }
    wait = WaitForMultipleObjects(
        wait_count,
        waits,
        FALSE,
        INFINITE);
    if (wait == WAIT_OBJECT_0) {
        EnterCriticalSection(&earcon->state_lock);
        *start_latency_ms = earcon->latency_ms;
        played = earcon->latency_ms != UINT_MAX;
        LeaveCriticalSection(&earcon->state_lock);
    } else {
        EnterCriticalSection(&earcon->state_lock);
        InterlockedExchange(&earcon->active, 0);
        LeaveCriticalSection(&earcon->state_lock);
    }

    if (wait == WAIT_OBJECT_0 + 1u) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "earcon playback timed out");
    } else if (wait_count == 3u &&
               wait == WAIT_OBJECT_0 + 2u) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "earcon cancelled");
    } else if (wait != WAIT_OBJECT_0) {
        dio_earcon_error(
            error_text,
            error_text_capacity,
            "earcon playback wait failed");
    }

done:
    (void)CancelWaitableTimer(earcon->timeout_event);
    LeaveCriticalSection(&earcon->play_lock);
    return played;
}

bool dio_earcon_play_listening(
    DioEarcon *earcon,
    ULONGLONG requested_at_ms,
    unsigned int *start_latency_ms,
    char *error_text,
    size_t error_text_capacity)
{
    return dio_earcon_play(
        earcon,
        requested_at_ms,
        start_latency_ms,
        error_text,
        error_text_capacity,
        DIO_EARCON_LISTENING);
}

bool dio_earcon_play_processing(
    DioEarcon *earcon,
    ULONGLONG requested_at_ms,
    unsigned int *start_latency_ms,
    char *error_text,
    size_t error_text_capacity)
{
    return dio_earcon_play(
        earcon,
        requested_at_ms,
        start_latency_ms,
        error_text,
        error_text_capacity,
        DIO_EARCON_PROCESSING);
}

void dio_earcon_close(
    DioEarcon *earcon)
{
    if (earcon == NULL) {
        return;
    }
    EnterCriticalSection(&earcon->play_lock);
    EnterCriticalSection(&earcon->state_lock);
    InterlockedExchange(&earcon->active, 0);
    LeaveCriticalSection(&earcon->state_lock);
    if (earcon->device_ready) {
        if (earcon->device_started) {
            (void)ma_device_stop(&earcon->device);
        }
        ma_device_uninit(&earcon->device);
    }
    if (earcon->timeout_event != NULL) {
        (void)CloseHandle(earcon->timeout_event);
    }
    if (earcon->done_event != NULL) {
        (void)CloseHandle(earcon->done_event);
    }
    if (earcon->ready_event != NULL) {
        (void)CloseHandle(earcon->ready_event);
    }
    LeaveCriticalSection(&earcon->play_lock);
    DeleteCriticalSection(&earcon->state_lock);
    DeleteCriticalSection(&earcon->play_lock);
    free(earcon);
}
