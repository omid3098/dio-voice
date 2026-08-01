#include "pcm_stream.h"

#include <stdlib.h>
#include <string.h>

#include "miniaudio_config.h"

/* ponytail: bounded per-turn queue; raise only if measured replies exceed it. */
#define DIO_PCM_STREAM_SECONDS 120u
#define DIO_PCM_STREAM_MIN_RATE 8000u
#define DIO_PCM_STREAM_MAX_RATE 48000u

struct DioPcmStream {
    HANDLE terminal_event;
    HANDLE started_event;
    ma_pcm_rb ring;
    bool ring_ready;
    unsigned int sample_rate;
    volatile LONG final;
    volatile LONG cancelled;
    volatile LONG started;
};

static void dio_pcm_stream_error(
    char *output,
    size_t capacity,
    const char *message) {
    if (output == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(output, capacity, message, _TRUNCATE);
}

DioPcmStream *dio_pcm_stream_create(
    unsigned int sample_rate) {
    if (sample_rate < DIO_PCM_STREAM_MIN_RATE ||
        sample_rate > DIO_PCM_STREAM_MAX_RATE ||
        sample_rate > SIZE_MAX / DIO_PCM_STREAM_SECONDS) {
        return NULL;
    }
    DioPcmStream *stream =
        (DioPcmStream *)calloc(1u, sizeof(*stream));
    if (stream == NULL) {
        return NULL;
    }
    stream->terminal_event =
        CreateEventW(NULL, TRUE, FALSE, NULL);
    stream->started_event =
        CreateEventW(NULL, TRUE, FALSE, NULL);
    const size_t capacity =
        (size_t)sample_rate * DIO_PCM_STREAM_SECONDS;
    if (stream->terminal_event != NULL &&
        stream->started_event != NULL &&
        capacity <= UINT32_MAX &&
        ma_pcm_rb_init(
            ma_format_s16,
            1u,
            (ma_uint32)capacity,
            NULL,
            NULL,
            &stream->ring) == MA_SUCCESS) {
        stream->ring_ready = true;
    } else {
        dio_pcm_stream_destroy(stream);
        return NULL;
    }
    stream->sample_rate = sample_rate;
    return stream;
}

bool dio_pcm_stream_write(
    DioPcmStream *stream,
    const int16_t *samples,
    size_t sample_count) {
    if (stream == NULL ||
        samples == NULL ||
        sample_count == 0u ||
        sample_count > UINT32_MAX ||
        InterlockedCompareExchange(
            &stream->final,
            0,
            0) != 0 ||
        InterlockedCompareExchange(
            &stream->cancelled,
            0,
            0) != 0 ||
        sample_count >
            ma_pcm_rb_available_write(&stream->ring)) {
        return false;
    }
    size_t offset = 0u;
    while (offset < sample_count) {
        ma_uint32 count =
            (ma_uint32)(sample_count - offset);
        void *output = NULL;
        if (ma_pcm_rb_acquire_write(
                &stream->ring,
                &count,
                &output) != MA_SUCCESS ||
            count == 0u ||
            output == NULL) {
            return false;
        }
        (void)memcpy(
            output,
            samples + offset,
            (size_t)count * sizeof(*samples));
        if (ma_pcm_rb_commit_write(
                &stream->ring,
                count) != MA_SUCCESS) {
            return false;
        }
        offset += count;
    }
    return true;
}

bool dio_pcm_stream_finish(
    DioPcmStream *stream) {
    if (stream == NULL) {
        return false;
    }
    return
        InterlockedCompareExchange(
            &stream->cancelled,
            0,
            0) == 0 &&
        InterlockedCompareExchange(
            &stream->final,
            1,
            0) == 0;
}

void dio_pcm_stream_cancel(
    DioPcmStream *stream) {
    if (stream == NULL) {
        return;
    }
    (void)InterlockedExchange(&stream->cancelled, 1);
    (void)SetEvent(stream->terminal_event);
}

static void dio_pcm_stream_callback(
    ma_device *device,
    void *output,
    const void *input,
    ma_uint32 frame_count) {
    DioPcmStream *stream =
        (DioPcmStream *)device->pUserData;
    int16_t *frames = (int16_t *)output;

    (void)input;
    (void)memset(
        frames,
        0,
        (size_t)frame_count * sizeof(*frames));
    if (stream == NULL) {
        return;
    }
    if (InterlockedCompareExchange(
            &stream->cancelled,
            0,
            0) == 0) {
        if (InterlockedCompareExchange(
                &stream->final,
                0,
                0) != 0 &&
            ma_pcm_rb_available_read(&stream->ring) == 0u) {
            (void)SetEvent(stream->terminal_event);
            return;
        }
        ma_uint32 copied = 0u;
        while (copied < frame_count) {
            ma_uint32 available = frame_count - copied;
            void *input_frames = NULL;
            if (ma_pcm_rb_acquire_read(
                    &stream->ring,
                    &available,
                    &input_frames) != MA_SUCCESS ||
                available == 0u ||
                input_frames == NULL) {
                break;
            }
            (void)memcpy(
                frames + copied,
                input_frames,
                (size_t)available * sizeof(*frames));
            (void)ma_pcm_rb_commit_read(
                &stream->ring,
                available);
            copied += available;
        }
        if (copied != 0u &&
            InterlockedCompareExchange(
                &stream->cancelled,
                0,
                0) == 0 &&
            InterlockedCompareExchange(
                &stream->started,
                1,
                0) == 0) {
            (void)SetEvent(stream->started_event);
        }
    }
}

DioTtsResult dio_pcm_stream_play(
    DioPcmStream *stream,
    bool null_backend,
    HANDLE cancel_event,
    HANDLE stop_event,
    DioPcmStreamStartedCallback started_callback,
    void *started_context,
    char *error_text,
    size_t error_text_capacity) {
    ma_context context;
    ma_device device;
    ma_device_config config;
    bool context_ready = false;
    bool device_ready = false;
    DioTtsResult result = DIO_TTS_FAILED;

    if (stream == NULL) {
        dio_pcm_stream_error(
            error_text,
            error_text_capacity,
            "PCM stream is not configured");
        return DIO_TTS_FAILED;
    }
    const bool already_cancelled =
        InterlockedCompareExchange(
            &stream->cancelled,
            0,
            0) != 0;
    if (already_cancelled) {
        return DIO_TTS_CANCELLED;
    }

    (void)memset(&context, 0, sizeof(context));
    (void)memset(&device, 0, sizeof(device));
    if (null_backend) {
        static const ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(
                backends,
                (ma_uint32)_countof(backends),
                NULL,
                &context) != MA_SUCCESS) {
            dio_pcm_stream_error(
                error_text,
                error_text_capacity,
                "could not open null PCM playback context");
            goto cleanup;
        }
        context_ready = true;
    }
    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 1u;
    config.sampleRate = stream->sample_rate;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dio_pcm_stream_callback;
    config.pUserData = stream;
    if (ma_device_init(
            context_ready ? &context : NULL,
            &config,
            &device) != MA_SUCCESS) {
        dio_pcm_stream_error(
            error_text,
            error_text_capacity,
            "could not open PCM playback device");
        goto cleanup;
    }
    device_ready = true;
    if (ma_device_start(&device) != MA_SUCCESS) {
        dio_pcm_stream_error(
            error_text,
            error_text_capacity,
            "could not start PCM playback");
        goto cleanup;
    }

    bool started_reported = false;
    for (;;) {
        HANDLE waits[4];
        DWORD count = 0u;
        DWORD terminal_index;
        DWORD cancel_index = UINT32_MAX;
        DWORD stop_index = UINT32_MAX;

        if (!started_reported) {
            waits[count++] = stream->started_event;
        }
        terminal_index = count;
        waits[count++] = stream->terminal_event;
        if (cancel_event != NULL) {
            cancel_index = count;
            waits[count++] = cancel_event;
        }
        if (stop_event != NULL) {
            stop_index = count;
            waits[count++] = stop_event;
        }
        const DWORD wait =
            WaitForMultipleObjects(count, waits, FALSE, INFINITE);
        if (!started_reported && wait == WAIT_OBJECT_0) {
            if (InterlockedCompareExchange(
                    &stream->cancelled,
                    0,
                    0) != 0 ||
                (cancel_event != NULL &&
                 WaitForSingleObject(
                     cancel_event,
                     0u) == WAIT_OBJECT_0) ||
                (stop_event != NULL &&
                 WaitForSingleObject(
                     stop_event,
                     0u) == WAIT_OBJECT_0)) {
                result = DIO_TTS_CANCELLED;
                break;
            }
            started_reported = true;
            if (started_callback != NULL) {
                started_callback(started_context);
            }
            continue;
        }
        const DWORD selected = wait - WAIT_OBJECT_0;
        if (wait >= WAIT_OBJECT_0 &&
            wait < WAIT_OBJECT_0 + count &&
            selected == terminal_index) {
            result =
                InterlockedCompareExchange(
                    &stream->cancelled,
                    0,
                    0) != 0
                    ? DIO_TTS_CANCELLED
                    : DIO_TTS_SUCCEEDED;
        } else if (
            wait >= WAIT_OBJECT_0 &&
            wait < WAIT_OBJECT_0 + count &&
            (selected == cancel_index ||
             selected == stop_index)) {
            result = DIO_TTS_CANCELLED;
        } else {
            dio_pcm_stream_error(
                error_text,
                error_text_capacity,
                "PCM playback wait failed");
        }
        break;
    }

cleanup:
    if (device_ready) {
        if (result == DIO_TTS_SUCCEEDED &&
            ma_device_stop(&device) != MA_SUCCESS) {
            dio_pcm_stream_error(
                error_text,
                error_text_capacity,
                "could not drain PCM playback");
            result = DIO_TTS_FAILED;
        }
        ma_device_uninit(&device);
    }
    if (context_ready) {
        ma_context_uninit(&context);
    }
    return result;
}

void dio_pcm_stream_destroy(
    DioPcmStream *stream) {
    if (stream == NULL) {
        return;
    }
    if (stream->terminal_event != NULL) {
        (void)CloseHandle(stream->terminal_event);
    }
    if (stream->started_event != NULL) {
        (void)CloseHandle(stream->started_event);
    }
    if (stream->ring_ready) {
        ma_pcm_rb_uninit(&stream->ring);
    }
    free(stream);
}
