#include "dio_voice/voice_core.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

#include "audio_capture.h"
#include "earcon.h"
#include "ort_runtime.h"
#include "pcm_stream.h"
#include "tts.h"
#include "vosk_runtime.h"
#include "wake_onnx.h"
#include "voice_core_internal.h"

#define DIO_AUDIO_QUEUE_FRAMES 256u
#define DIO_RING_FRAMES 125u
#define DIO_SPEECH_QUEUE_LIMIT 64u
#define DIO_VAD_PREROLL_FRAMES 15u
#define DIO_FOLLOW_UP_STT_GRACE_MS 2000u

typedef enum DioVoiceMode {
    DIO_MODE_WAKE = 0,
    DIO_MODE_LISTENING,
    DIO_MODE_FOLLOW_UP,
    DIO_MODE_PAUSED
} DioVoiceMode;

typedef struct DioSpeechItem {
    struct DioSpeechItem *next;
    uint64_t utterance_id;
    size_t text_length;
    DioPcmStream *stream;
    char text[1];
} DioSpeechItem;

struct DioVoiceCore {
    wchar_t *onnxruntime_path;
    wchar_t *wake_model_directory;
    wchar_t *silero_model_path;
    wchar_t *vosk_library_path;
    wchar_t *vosk_model_directory;
    wchar_t *capture_device_name;
    wchar_t *capture_device_id;
    DioTtsServer *tts_server;

    float wake_sensitivity;
    float vad_threshold;
    float vad_hysteresis;
    unsigned int command_silence_ms;
    unsigned int command_start_timeout_ms;
    unsigned int command_max_ms;
    unsigned int follow_up_ms;
    bool external_audio;
    DioVoiceEventCallback callback;
    void *callback_context;

    DioOrtRuntime ort;
    DioWakePorcupine wake;
    DioSileroVad vad;
    DioVoskRuntime vosk;
    DioAudioCapture capture;
    DioEarcon *earcon;

    CRITICAL_SECTION callback_lock;
    CRITICAL_SECTION speech_lock;
    bool locks_ready;
    SRWLOCK audio_lock;
    HANDLE stop_event;
    HANDLE audio_event;
    HANDLE speech_event;
    HANDLE speech_cancel_event;
    HANDLE speech_idle_event;
    HANDLE recognition_thread;
    HANDLE speech_thread;

    volatile LONG started;
    volatile LONG closing;
    volatile LONG paused;
    volatile LONG push_to_talk_requested;
    volatile LONG follow_up_requested;
    volatile LONG cancel_requested;
    volatile LONG speech_active;
    volatile LONG failed;
    volatile LONG64 dropped_frames;

    int16_t *audio_queue;
    size_t audio_queue_read;
    size_t audio_queue_write;
    size_t audio_queue_count;
    int16_t pending_audio[DIO_VOICE_FRAME_SAMPLES];
    size_t pending_audio_count;

    int16_t *ring;
    size_t ring_write;
    size_t ring_count;
    DioVoiceMode mode;
    ULONGLONG follow_up_deadline;
    unsigned int last_earcon_latency_ms;
    bool recognition_was_speaking;
    bool vad_speech_active;

    DioVoskRecognizer command;
    bool command_ready;
    bool command_from_follow_up;
    bool command_speech_started;
    bool command_text_started;
    ULONGLONG command_started_at_ms;
    unsigned int command_elapsed_ms;
    unsigned int command_quiet_ms;
    char *last_partial;

    DioSpeechItem *speech_head;
    DioSpeechItem *speech_tail;
    size_t speech_count;
    DioPcmStream *pcm_stream;
    uint64_t pcm_stream_id;
    bool pcm_stream_active;
};

static wchar_t *dio_voice_wide_copy(const wchar_t *text)
{
    size_t length;
    wchar_t *copy;

    if (text == NULL) {
        return NULL;
    }
    length = wcslen(text);
    if (length == SIZE_MAX ||
        length + 1u > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (wchar_t *)malloc((length + 1u) * sizeof(*copy));
    if (copy != NULL) {
        (void)memcpy(copy, text, (length + 1u) * sizeof(*copy));
    }
    return copy;
}

static void dio_voice_emit(
    DioVoiceCore *voice,
    DioVoiceEventType type,
    uint64_t utterance_id,
    const char *text,
    size_t text_length,
    float level,
    DioVoiceSpeechOutcome speech_outcome,
    bool transcript_is_final,
    unsigned long system_error)
{
    DioVoiceEvent event;

    if (voice == NULL || voice->callback == NULL ||
        InterlockedCompareExchange(&voice->closing, 0, 0) != 0) {
        return;
    }
    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.utterance_id = utterance_id;
    event.text = text;
    event.text_length = text_length;
    event.level = level;
    event.latency_ms =
        type == DIO_VOICE_EVENT_WAKE
            ? voice->last_earcon_latency_ms
            : 0u;
    event.dropped_frames =
        (uint64_t)InterlockedCompareExchange64(
            &voice->dropped_frames,
            0,
            0);
    event.system_error = system_error;
    event.speech_outcome = speech_outcome;
    event.transcript_is_final = transcript_is_final;

    EnterCriticalSection(&voice->callback_lock);
    if (InterlockedCompareExchange(&voice->closing, 0, 0) == 0) {
        voice->callback(voice->callback_context, &event);
    }
    LeaveCriticalSection(&voice->callback_lock);
}

static void dio_voice_emit_error(
    DioVoiceCore *voice,
    DioVoiceErrorCode error_code,
    const char *message)
{
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_ERROR,
        0u,
        message,
        message != NULL ? strlen(message) : 0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        (unsigned long)error_code);
}

static void dio_voice_ring_clear(DioVoiceCore *voice)
{
    voice->ring_write = 0u;
    voice->ring_count = 0u;
}

static void dio_voice_ring_append(
    DioVoiceCore *voice,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES])
{
    (void)memcpy(
        voice->ring + (voice->ring_write * DIO_VOICE_FRAME_SAMPLES),
        samples,
        DIO_VOICE_FRAME_SAMPLES * sizeof(*samples));
    voice->ring_write = (voice->ring_write + 1u) % DIO_RING_FRAMES;
    if (voice->ring_count < DIO_RING_FRAMES) {
        ++voice->ring_count;
    }
}

static const int16_t *dio_voice_ring_frame(
    const DioVoiceCore *voice,
    size_t chronological_index)
{
    const size_t oldest =
        (voice->ring_write + DIO_RING_FRAMES - voice->ring_count) %
        DIO_RING_FRAMES;
    const size_t index =
        (oldest + chronological_index) % DIO_RING_FRAMES;
    return voice->ring + (index * DIO_VOICE_FRAME_SAMPLES);
}

static void dio_voice_command_close(DioVoiceCore *voice)
{
    if (voice->command_ready) {
        dio_vosk_recognizer_close(&voice->command);
    }
    voice->command_ready = false;
    voice->command_from_follow_up = false;
    free(voice->last_partial);
    voice->last_partial = NULL;
    voice->command_speech_started = false;
    voice->command_text_started = false;
    voice->command_started_at_ms = 0u;
    voice->command_elapsed_ms = 0u;
    voice->command_quiet_ms = 0u;
}

static void dio_voice_reset_recognition(DioVoiceCore *voice)
{
    dio_voice_command_close(voice);
    dio_silero_reset(&voice->vad);
    dio_voice_ring_clear(voice);
    voice->vad_speech_active = false;
    voice->recognition_was_speaking = false;
}

static void dio_voice_latch_failure(
    DioVoiceCore *voice,
    const char *message)
{
    if (InterlockedCompareExchange(&voice->failed, 1, 0) == 0) {
        dio_voice_emit_error(
            voice,
            DIO_VOICE_ERROR_INFERENCE,
            message);
    }
    dio_voice_command_close(voice);
    voice->mode = DIO_MODE_PAUSED;
}

void dio_voice_test_latch_failure(DioVoiceCore *voice)
{
    if (voice != NULL) {
        if (InterlockedCompareExchange(&voice->failed, 1, 0) == 0) {
            dio_voice_emit_error(
                voice,
                DIO_VOICE_ERROR_INFERENCE,
                "injected voice core failure");
        }
        (void)SetEvent(voice->audio_event);
    }
}

void dio_voice_test_mark_recognition_speaking(DioVoiceCore *voice)
{
    if (voice != NULL) {
        voice->recognition_was_speaking = true;
    }
}

static void dio_voice_copy_last_partial(
    DioVoiceCore *voice,
    const char *text)
{
    const size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);

    if (copy == NULL) {
        return;
    }
    (void)memcpy(copy, text, length + 1u);
    free(voice->last_partial);
    voice->last_partial = copy;
}

static void dio_voice_emit_partial_if_changed(
    DioVoiceCore *voice,
    const char *text)
{
    if (text[0] == '\0' ||
        (voice->last_partial != NULL &&
         strcmp(voice->last_partial, text) == 0)) {
        return;
    }
    dio_voice_copy_last_partial(voice, text);
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_TRANSCRIPT,
        0u,
        text,
        strlen(text),
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
}

static void dio_voice_strip_wake_prefix(char *text);

static bool dio_voice_start_command(
    DioVoiceCore *voice,
    size_t pre_roll_frames,
    bool speech_started,
    char *error_text,
    size_t error_text_capacity)
{
    const bool from_follow_up =
        voice->mode == DIO_MODE_FOLLOW_UP;
    size_t start;
    size_t index;

    dio_voice_command_close(voice);
    if (!dio_vosk_recognizer_open(
            &voice->vosk,
            &voice->command,
            error_text,
            error_text_capacity)) {
        return false;
    }
    voice->command_ready = true;
    voice->command_from_follow_up = from_follow_up;
    voice->command_speech_started = speech_started;
    voice->command_started_at_ms = GetTickCount64();
    voice->command_elapsed_ms = 0u;
    voice->command_quiet_ms = 0u;
    pre_roll_frames =
        pre_roll_frames < voice->ring_count
            ? pre_roll_frames
            : voice->ring_count;
    start = voice->ring_count - pre_roll_frames;

    for (index = start; index < voice->ring_count; ++index) {
        char *text = NULL;
        bool endpoint;
        if (!dio_vosk_recognizer_feed(
                &voice->command,
                dio_voice_ring_frame(voice, index),
                DIO_VOICE_FRAME_SAMPLES,
                &text,
                &endpoint,
                error_text,
                error_text_capacity)) {
            dio_voice_command_close(voice);
            return false;
        }
        dio_voice_emit_partial_if_changed(voice, text);
        dio_voice_strip_wake_prefix(text);
        if (text[0] != '\0') {
            voice->command_speech_started = true;
            voice->command_text_started = true;
        }
        dio_vosk_text_free(text);
    }
    voice->mode = DIO_MODE_LISTENING;
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_LISTENING,
        0u,
        NULL,
        0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
    return true;
}

static void dio_voice_strip_wake_prefix(char *text)
{
    static const char *const prefixes[] = {
        "الکسا",
        "alexa"};
    size_t index;

    while (*text == ' ') {
        (void)memmove(text, text + 1, strlen(text));
    }
    for (index = 0u; index < _countof(prefixes); ++index) {
        const size_t prefix_length = strlen(prefixes[index]);
        if (strlen(text) >= prefix_length &&
            memcmp(text, prefixes[index], prefix_length) == 0 &&
            (text[prefix_length] == '\0' ||
             text[prefix_length] == ' ')) {
            const char *remainder = text + prefix_length;
            while (*remainder == ' ') {
                ++remainder;
            }
            (void)memmove(text, remainder, strlen(remainder) + 1u);
            return;
        }
    }
}

static bool dio_voice_follow_up_stt_grace_expired(
    const DioVoiceCore *voice)
{
    const ULONGLONG now = GetTickCount64();
    return voice->command_from_follow_up &&
        !voice->command_text_started &&
        now >= voice->follow_up_deadline &&
        now - voice->command_started_at_ms >=
            DIO_FOLLOW_UP_STT_GRACE_MS;
}

static void dio_voice_finish_command(
    DioVoiceCore *voice,
    bool timed_out)
{
    char error[512];
    char *text = NULL;
    const bool from_follow_up =
        voice->command_from_follow_up;
    bool recognized = false;

    if (!timed_out && voice->command_ready) {
        if (!dio_vosk_recognizer_finish(
                &voice->command,
                &text,
                error,
                sizeof(error))) {
            dio_voice_latch_failure(voice, error);
            return;
        } else {
            dio_voice_strip_wake_prefix(text);
            if (text[0] != '\0') {
                recognized = true;
                dio_voice_emit(
                    voice,
                    DIO_VOICE_EVENT_TRANSCRIPT,
                    0u,
                    text,
                    strlen(text),
                    0.0f,
                    DIO_VOICE_SPEECH_NONE,
                    true,
                    0u);
            }
        }
    }
    dio_vosk_text_free(text);
    dio_voice_reset_recognition(voice);
    if (from_follow_up &&
        !recognized &&
        GetTickCount64() < voice->follow_up_deadline) {
        voice->mode = DIO_MODE_FOLLOW_UP;
        return;
    }
    voice->mode = DIO_MODE_WAKE;
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_IDLE,
        0u,
        NULL,
        0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
}

static float dio_voice_level(
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES])
{
    double sum = 0.0;
    size_t index;
    float level;

    for (index = 0u; index < DIO_VOICE_FRAME_SAMPLES; ++index) {
        const double sample = samples[index];
        sum += sample * sample;
    }
    level = (float)(sqrt(sum / DIO_VOICE_FRAME_SAMPLES) / 8192.0);
    return level < 1.0f ? level : 1.0f;
}

bool dio_voice_vad_update(
    float probability,
    float threshold,
    float hysteresis,
    bool *active)
{
    const bool voiced =
        probability >= threshold - (*active ? hysteresis : 0.0f);
    *active = voiced;
    return voiced;
}

static void dio_voice_process_listening(
    DioVoiceCore *voice,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES],
    bool voiced)
{
    char error[512];
    char *text = NULL;
    bool endpoint;
    bool timed_out = false;
    bool captured = false;

    if (!dio_vosk_recognizer_feed(
            &voice->command,
            samples,
            DIO_VOICE_FRAME_SAMPLES,
            &text,
            &endpoint,
                error,
                sizeof(error))) {
        dio_voice_latch_failure(voice, error);
        return;
    }
    dio_voice_emit_partial_if_changed(voice, text);
    dio_voice_strip_wake_prefix(text);
    if (text[0] != '\0') {
        voice->command_text_started = true;
    }
    dio_vosk_text_free(text);

    voice->command_elapsed_ms += 32u;
    if (voiced) {
        voice->command_speech_started = true;
        voice->command_quiet_ms = 0u;
    } else if (voice->command_speech_started) {
        voice->command_quiet_ms += 32u;
    }

    if (dio_voice_follow_up_stt_grace_expired(voice)) {
        captured = true;
    } else if (!voice->command_speech_started &&
        voice->command_elapsed_ms >=
            (voice->command_start_timeout_ms < voice->command_max_ms
                 ? voice->command_start_timeout_ms
                 : voice->command_max_ms)) {
        timed_out = true;
    } else if (voice->command_speech_started &&
               (voice->command_quiet_ms >= voice->command_silence_ms ||
                voice->command_elapsed_ms >= voice->command_max_ms)) {
        captured = true;
    }
    if (timed_out || captured) {
        dio_voice_finish_command(voice, timed_out);
    }
}

static void dio_voice_process_wake_audio(
    DioVoiceCore *voice,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES])
{
    char error[512];
    bool triggered;
    const ULONGLONG detected_at_ms = GetTickCount64();

    if (!dio_wake_feed(
            &voice->wake,
            samples,
            &triggered,
            error,
            sizeof(error))) {
        dio_voice_latch_failure(voice, error);
        return;
    }
    if (!triggered) {
        return;
    }

    voice->last_earcon_latency_ms = 0u;
    if (!voice->external_audio) {
        voice->last_earcon_latency_ms = UINT_MAX;
        if (!dio_earcon_play_listening(
                voice->earcon,
                detected_at_ms,
                &voice->last_earcon_latency_ms,
                error,
                sizeof(error))) {
            dio_voice_emit_error(
                voice,
                DIO_VOICE_ERROR_EARCON,
                error);
        }
    }
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_WAKE,
        0u,
        "alexa",
        5u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
    if (!dio_voice_start_command(
            voice,
            0u,
            false,
            error,
            sizeof(error))) {
        dio_voice_latch_failure(voice, error);
    }
}

static void dio_voice_process_frame(
    DioVoiceCore *voice,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES])
{
    char error[512];
    float probability;
    bool voiced;
    ULONGLONG now;

    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return;
    }
    if (InterlockedCompareExchange(&voice->speech_active, 0, 0) != 0) {
        if (!voice->recognition_was_speaking) {
            dio_voice_reset_recognition(voice);
            voice->recognition_was_speaking = true;
        }
        return;
    }
    if (voice->recognition_was_speaking) {
        dio_voice_reset_recognition(voice);
        voice->mode = DIO_MODE_WAKE;
        voice->recognition_was_speaking = false;
    }

    if (InterlockedCompareExchange(&voice->paused, 0, 0) != 0) {
        if (voice->mode != DIO_MODE_PAUSED) {
            dio_voice_reset_recognition(voice);
            voice->mode = DIO_MODE_PAUSED;
            dio_voice_emit(
                voice,
                DIO_VOICE_EVENT_IDLE,
                0u,
                NULL,
                0u,
                0.0f,
                DIO_VOICE_SPEECH_NONE,
                false,
                0u);
        }
        return;
    }
    if (voice->mode == DIO_MODE_PAUSED) {
        dio_voice_reset_recognition(voice);
        voice->mode = DIO_MODE_WAKE;
        dio_voice_emit(
            voice,
            DIO_VOICE_EVENT_IDLE,
            0u,
            NULL,
            0u,
            0.0f,
            DIO_VOICE_SPEECH_NONE,
            false,
            0u);
    }

    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_LEVEL,
        0u,
        NULL,
        0u,
        dio_voice_level(samples),
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
    dio_voice_ring_append(voice, samples);

    if (voice->mode == DIO_MODE_WAKE) {
        dio_voice_process_wake_audio(voice, samples);
        return;
    }

    if (!dio_silero_probability(
            &voice->vad,
            samples,
            &probability,
            error,
            sizeof(error))) {
        dio_voice_latch_failure(voice, error);
        return;
    }
    voiced = dio_voice_vad_update(
        probability,
        voice->vad_threshold,
        voice->vad_hysteresis,
        &voice->vad_speech_active);

    if (voice->mode == DIO_MODE_LISTENING) {
        dio_voice_process_listening(voice, samples, voiced);
        return;
    }
    if (voice->mode == DIO_MODE_FOLLOW_UP) {
        now = GetTickCount64();
        if (now >= voice->follow_up_deadline) {
            dio_voice_reset_recognition(voice);
            voice->mode = DIO_MODE_WAKE;
            dio_voice_emit(
                voice,
                DIO_VOICE_EVENT_IDLE,
                0u,
                NULL,
                0u,
                0.0f,
                DIO_VOICE_SPEECH_NONE,
                false,
                0u);
        } else if (voiced &&
                   !dio_voice_start_command(
                       voice,
                       DIO_VAD_PREROLL_FRAMES,
                       true,
                       error,
                       sizeof(error))) {
            dio_voice_latch_failure(voice, error);
        }
        return;
    }
}

static bool dio_voice_dequeue_frame(
    DioVoiceCore *voice,
    int16_t output[DIO_VOICE_FRAME_SAMPLES])
{
    bool available = false;

    AcquireSRWLockExclusive(&voice->audio_lock);
    if (voice->audio_queue_count != 0u) {
        (void)memcpy(
            output,
            voice->audio_queue +
                (voice->audio_queue_read * DIO_VOICE_FRAME_SAMPLES),
            DIO_VOICE_FRAME_SAMPLES * sizeof(*output));
        voice->audio_queue_read =
            (voice->audio_queue_read + 1u) % DIO_AUDIO_QUEUE_FRAMES;
        --voice->audio_queue_count;
        available = true;
    }
    ReleaseSRWLockExclusive(&voice->audio_lock);
    return available;
}

static void dio_voice_handle_control(DioVoiceCore *voice)
{
    char error[512];

    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        if (voice->mode != DIO_MODE_PAUSED) {
            dio_voice_reset_recognition(voice);
            voice->mode = DIO_MODE_PAUSED;
        }
        InterlockedExchange(&voice->cancel_requested, 0);
        InterlockedExchange(&voice->push_to_talk_requested, 0);
        InterlockedExchange(&voice->follow_up_requested, 0);
        return;
    }
    if (InterlockedExchange(&voice->cancel_requested, 0) != 0) {
        dio_voice_reset_recognition(voice);
        voice->mode =
            InterlockedCompareExchange(&voice->paused, 0, 0) != 0
                ? DIO_MODE_PAUSED
                : DIO_MODE_WAKE;
        dio_voice_emit(
            voice,
            DIO_VOICE_EVENT_IDLE,
            0u,
            NULL,
            0u,
            0.0f,
            DIO_VOICE_SPEECH_NONE,
            false,
            0u);
    }
    if (InterlockedCompareExchange(&voice->paused, 0, 0) != 0 &&
        voice->mode != DIO_MODE_PAUSED) {
        dio_voice_reset_recognition(voice);
        voice->mode = DIO_MODE_PAUSED;
        dio_voice_emit(
            voice,
            DIO_VOICE_EVENT_IDLE,
            0u,
            NULL,
            0u,
            0.0f,
            DIO_VOICE_SPEECH_NONE,
            false,
            0u);
    } else if (InterlockedCompareExchange(&voice->paused, 0, 0) == 0 &&
               voice->mode == DIO_MODE_PAUSED) {
        dio_voice_reset_recognition(voice);
        voice->mode = DIO_MODE_WAKE;
        if (InterlockedCompareExchange(
                &voice->push_to_talk_requested,
                0,
                0) == 0 &&
            InterlockedCompareExchange(
                &voice->follow_up_requested,
                0,
                0) == 0) {
            dio_voice_emit(
                voice,
                DIO_VOICE_EVENT_IDLE,
                0u,
                NULL,
                0u,
                0.0f,
                DIO_VOICE_SPEECH_NONE,
                false,
                0u);
        }
    }
    if (InterlockedCompareExchange(&voice->paused, 0, 0) == 0 &&
        InterlockedCompareExchange(&voice->speech_active, 0, 0) == 0 &&
        InterlockedExchange(&voice->push_to_talk_requested, 0) != 0) {
        dio_voice_reset_recognition(voice);
        if (!dio_voice_start_command(
                voice,
                0u,
                false,
                error,
                sizeof(error))) {
            dio_voice_latch_failure(voice, error);
        }
    }
    if (InterlockedCompareExchange(&voice->paused, 0, 0) == 0 &&
        InterlockedCompareExchange(&voice->speech_active, 0, 0) == 0 &&
        InterlockedExchange(&voice->follow_up_requested, 0) != 0) {
        dio_voice_reset_recognition(voice);
        voice->mode = DIO_MODE_FOLLOW_UP;
        voice->follow_up_deadline =
            GetTickCount64() + voice->follow_up_ms;
        dio_voice_emit(
            voice,
            DIO_VOICE_EVENT_LISTENING,
            0u,
            NULL,
            0u,
            0.0f,
            DIO_VOICE_SPEECH_NONE,
            false,
            0u);
    }
}

static DWORD WINAPI dio_voice_recognition_worker(void *context)
{
    DioVoiceCore *voice = (DioVoiceCore *)context;
    HANDLE waits[] = {voice->stop_event, voice->audio_event};
    int16_t frame[DIO_VOICE_FRAME_SAMPLES];

    for (;;) {
        DWORD wait_result;

        dio_voice_handle_control(voice);
        while (dio_voice_dequeue_frame(voice, frame)) {
            dio_voice_handle_control(voice);
            dio_voice_process_frame(voice, frame);
            if (WaitForSingleObject(voice->stop_event, 0u) ==
                WAIT_OBJECT_0) {
                return 0u;
            }
        }
        if (voice->mode == DIO_MODE_LISTENING &&
            dio_voice_follow_up_stt_grace_expired(voice)) {
            dio_voice_finish_command(voice, false);
        } else if (voice->mode == DIO_MODE_FOLLOW_UP &&
            GetTickCount64() >= voice->follow_up_deadline) {
            dio_voice_reset_recognition(voice);
            voice->mode = DIO_MODE_WAKE;
            dio_voice_emit(
                voice,
                DIO_VOICE_EVENT_IDLE,
                0u,
                NULL,
                0u,
                0.0f,
                DIO_VOICE_SPEECH_NONE,
                false,
                0u);
        }
        wait_result = WaitForMultipleObjects(
            _countof(waits),
            waits,
            FALSE,
            32u);
        if (wait_result == WAIT_OBJECT_0) {
            return 0u;
        }
    }
}

static DioSpeechItem *dio_voice_speech_pop(DioVoiceCore *voice)
{
    DioSpeechItem *item;

    EnterCriticalSection(&voice->speech_lock);
    item = voice->speech_head;
    if (item != NULL) {
        voice->speech_head = item->next;
        if (voice->speech_head == NULL) {
            voice->speech_tail = NULL;
        }
        --voice->speech_count;
        (void)ResetEvent(voice->speech_cancel_event);
        InterlockedExchange(&voice->speech_active, 1);
        if (item->stream != NULL) {
            voice->pcm_stream_active = true;
        }
    } else {
        InterlockedExchange(&voice->speech_active, 0);
        (void)SetEvent(voice->speech_idle_event);
        (void)SetEvent(voice->audio_event);
    }
    LeaveCriticalSection(&voice->speech_lock);
    return item;
}

typedef struct DioPcmStartedContext {
    DioVoiceCore *voice;
    uint64_t utterance_id;
} DioPcmStartedContext;

static void dio_voice_pcm_started(void *context)
{
    const DioPcmStartedContext *started =
        (const DioPcmStartedContext *)context;
    dio_voice_emit(
        started->voice,
        DIO_VOICE_EVENT_SPEAKING,
        started->utterance_id,
        NULL,
        0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
}

static DWORD WINAPI dio_voice_speech_worker(void *context)
{
    DioVoiceCore *voice = (DioVoiceCore *)context;
    HANDLE waits[] = {voice->stop_event, voice->speech_event};
    DioTtsConfig config;

    ZeroMemory(&config, sizeof(config));
    config.server = voice->tts_server;

    for (;;) {
        DioSpeechItem *item;

        if (WaitForMultipleObjects(
                _countof(waits),
                waits,
                FALSE,
                INFINITE) == WAIT_OBJECT_0) {
            return 0u;
        }
        while ((item = dio_voice_speech_pop(voice)) != NULL) {
            char error[512];
            DioTtsResult tts_result;
            DioVoiceSpeechOutcome outcome;
            bool queue_empty;

            (void)SetEvent(voice->audio_event);
            if (item->stream != NULL) {
                DioPcmStartedContext started = {
                    .voice = voice,
                    .utterance_id = item->utterance_id};
                tts_result = dio_pcm_stream_play(
                    item->stream,
                    false,
                    voice->speech_cancel_event,
                    voice->stop_event,
                    dio_voice_pcm_started,
                    &started,
                    error,
                    sizeof(error));
            } else {
                DioPcmStartedContext started = {
                    .voice = voice,
                    .utterance_id = item->utterance_id};
                tts_result = dio_tts_speak_text(
                    &config,
                    item->text,
                    item->text_length,
                    voice->speech_cancel_event,
                    voice->stop_event,
                    dio_voice_pcm_started,
                    &started,
                    error,
                    sizeof(error));
            }
            outcome =
                tts_result == DIO_TTS_SUCCEEDED
                    ? DIO_VOICE_SPEECH_SUCCEEDED
                    : (tts_result == DIO_TTS_CANCELLED
                           ? DIO_VOICE_SPEECH_CANCELLED
                           : DIO_VOICE_SPEECH_FAILED);
            EnterCriticalSection(&voice->speech_lock);
            if (voice->pcm_stream == item->stream &&
                item->stream != NULL) {
                voice->pcm_stream = NULL;
                voice->pcm_stream_id = 0u;
                voice->pcm_stream_active = false;
            }
            queue_empty = voice->speech_head == NULL;
            if (queue_empty) {
                InterlockedExchange(&voice->speech_active, 0);
            }
            LeaveCriticalSection(&voice->speech_lock);
            if (queue_empty) {
                (void)SetEvent(voice->audio_event);
            }
            dio_voice_emit(
                voice,
                DIO_VOICE_EVENT_SPEECH_COMPLETE,
                item->utterance_id,
                tts_result == DIO_TTS_FAILED ? error : NULL,
                tts_result == DIO_TTS_FAILED ? strlen(error) : 0u,
                0.0f,
                outcome,
                false,
                0u);
            dio_pcm_stream_destroy(item->stream);
            free(item);
            if (queue_empty) {
                EnterCriticalSection(&voice->speech_lock);
                if (voice->speech_head == NULL &&
                    InterlockedCompareExchange(
                        &voice->speech_active,
                        0,
                        0) == 0) {
                    (void)SetEvent(voice->speech_idle_event);
                } else {
                    (void)ResetEvent(voice->speech_idle_event);
                }
                LeaveCriticalSection(&voice->speech_lock);
                break;
            }

            if (WaitForSingleObject(voice->stop_event, 0u) ==
                WAIT_OBJECT_0) {
                return 0u;
            }
        }
    }
}

static void dio_voice_enqueue_samples(
    DioVoiceCore *voice,
    const int16_t *samples,
    size_t sample_count)
{
    size_t offset = 0u;
    bool queued = false;

    AcquireSRWLockExclusive(&voice->audio_lock);
    while (offset < sample_count) {
        const size_t needed =
            DIO_VOICE_FRAME_SAMPLES - voice->pending_audio_count;
        const size_t copy_count =
            sample_count - offset < needed
                ? sample_count - offset
                : needed;
        (void)memcpy(
            voice->pending_audio + voice->pending_audio_count,
            samples + offset,
            copy_count * sizeof(*samples));
        voice->pending_audio_count += copy_count;
        offset += copy_count;

        if (voice->pending_audio_count == DIO_VOICE_FRAME_SAMPLES) {
            if (voice->audio_queue_count == DIO_AUDIO_QUEUE_FRAMES) {
                (void)InterlockedIncrement64(&voice->dropped_frames);
            } else {
                (void)memcpy(
                    voice->audio_queue +
                        (voice->audio_queue_write *
                         DIO_VOICE_FRAME_SAMPLES),
                    voice->pending_audio,
                    sizeof(voice->pending_audio));
                voice->audio_queue_write =
                    (voice->audio_queue_write + 1u) %
                    DIO_AUDIO_QUEUE_FRAMES;
                ++voice->audio_queue_count;
                queued = true;
            }
            voice->pending_audio_count = 0u;
        }
    }
    ReleaseSRWLockExclusive(&voice->audio_lock);
    if (queued) {
        (void)SetEvent(voice->audio_event);
    }
}

static void dio_voice_capture_callback(
    void *context,
    const int16_t *samples,
    size_t sample_count)
{
    DioVoiceCore *voice = (DioVoiceCore *)context;

    if (voice != NULL &&
        InterlockedCompareExchange(&voice->started, 0, 0) != 0 &&
        InterlockedCompareExchange(&voice->closing, 0, 0) == 0) {
        size_t offset = 0u;
        while (offset < sample_count) {
            const size_t count =
                sample_count - offset < DIO_VOICE_FRAME_SAMPLES
                    ? sample_count - offset
                    : DIO_VOICE_FRAME_SAMPLES;
            dio_voice_enqueue_samples(
                voice,
                samples + offset,
                count);
            offset += count;
        }
    }
}

static bool dio_voice_copy_config(
    DioVoiceCore *voice,
    const DioVoiceConfig *config)
{
#define DIO_COPY_REQUIRED(field)                                             \
    voice->field = dio_voice_wide_copy(config->field);                       \
    if (voice->field == NULL) {                                              \
        return false;                                                        \
    }
#define DIO_COPY_OPTIONAL(field)                                             \
    if (config->field != NULL) {                                             \
        voice->field = dio_voice_wide_copy(config->field);                   \
        if (voice->field == NULL) {                                          \
            return false;                                                    \
        }                                                                    \
    }

    DIO_COPY_REQUIRED(onnxruntime_path);
    DIO_COPY_REQUIRED(wake_model_directory);
    DIO_COPY_REQUIRED(silero_model_path);
    DIO_COPY_REQUIRED(vosk_library_path);
    DIO_COPY_REQUIRED(vosk_model_directory);
    DIO_COPY_OPTIONAL(capture_device_name);
    DIO_COPY_OPTIONAL(capture_device_id);
    voice->tts_server = config->tts_server;

#undef DIO_COPY_OPTIONAL
#undef DIO_COPY_REQUIRED
    return true;
}

static void dio_voice_free_config(DioVoiceCore *voice)
{
    free(voice->capture_device_id);
    free(voice->capture_device_name);
    free(voice->vosk_model_directory);
    free(voice->vosk_library_path);
    free(voice->silero_model_path);
    free(voice->wake_model_directory);
    free(voice->onnxruntime_path);
}

DioVoiceResult dio_voice_open(
    const DioVoiceConfig *config,
    DioVoiceCore **output)
{
    DioVoiceCore *voice;
    char error[512];

    if (config == NULL || output == NULL ||
        config->onnxruntime_path == NULL ||
        config->wake_model_directory == NULL ||
        config->silero_model_path == NULL ||
        config->vosk_library_path == NULL ||
        config->vosk_model_directory == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    *output = NULL;
    voice = (DioVoiceCore *)calloc(1u, sizeof(*voice));
    if (voice == NULL) {
        return DIO_VOICE_OUT_OF_MEMORY;
    }
    InitializeCriticalSection(&voice->callback_lock);
    InitializeCriticalSection(&voice->speech_lock);
    voice->locks_ready = true;
    InitializeSRWLock(&voice->audio_lock);

    voice->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    voice->audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    voice->speech_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    voice->speech_cancel_event =
        CreateEventW(NULL, TRUE, FALSE, NULL);
    voice->speech_idle_event =
        CreateEventW(NULL, TRUE, TRUE, NULL);
    voice->audio_queue = (int16_t *)malloc(
        DIO_AUDIO_QUEUE_FRAMES *
        DIO_VOICE_FRAME_SAMPLES *
        sizeof(*voice->audio_queue));
    voice->ring = (int16_t *)malloc(
        DIO_RING_FRAMES *
        DIO_VOICE_FRAME_SAMPLES *
        sizeof(*voice->ring));
    if (voice->stop_event == NULL ||
        voice->audio_event == NULL ||
        voice->speech_event == NULL ||
        voice->speech_cancel_event == NULL ||
        voice->speech_idle_event == NULL ||
        voice->audio_queue == NULL ||
        voice->ring == NULL ||
        !dio_voice_copy_config(voice, config)) {
        dio_voice_close(voice);
        return DIO_VOICE_OUT_OF_MEMORY;
    }

    voice->wake_sensitivity =
        config->wake_sensitivity > 0.0f &&
        config->wake_sensitivity <= 1.0f
            ? config->wake_sensitivity
            : 0.5f;
    voice->vad_threshold =
        config->vad_threshold > 0.0f
            ? config->vad_threshold
            : 0.55f;
    voice->vad_hysteresis =
        config->vad_hysteresis > 0.0f
            ? config->vad_hysteresis
            : 0.15f;
    voice->command_silence_ms =
        config->command_silence_ms != 0u
            ? config->command_silence_ms
            : 1100u;
    voice->command_start_timeout_ms =
        config->command_start_timeout_ms != 0u
            ? config->command_start_timeout_ms
            : 6000u;
    voice->command_max_ms =
        config->command_max_ms != 0u
            ? config->command_max_ms
            : 15000u;
    voice->follow_up_ms = config->follow_up_ms;
    voice->external_audio = config->external_audio;
    voice->callback = config->callback;
    voice->callback_context = config->callback_context;
    voice->mode = DIO_MODE_WAKE;

    if (!dio_ort_runtime_open(
            voice->onnxruntime_path,
            &voice->ort,
            error,
            sizeof(error)) ||
        !dio_wake_open(
            voice->wake_model_directory,
            voice->wake_sensitivity,
            &voice->wake,
            error,
            sizeof(error)) ||
        !dio_silero_open(
            &voice->ort,
            voice->silero_model_path,
            &voice->vad,
            error,
            sizeof(error)) ||
        !dio_vosk_runtime_open(
            voice->vosk_library_path,
            voice->vosk_model_directory,
            &voice->vosk,
            error,
            sizeof(error))) {
        dio_voice_close(voice);
        return DIO_VOICE_MODEL_FAILURE;
    }

    *output = voice;
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_start(DioVoiceCore *voice)
{
    char error[512];

    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->closing, 0, 0) != 0) {
        return DIO_VOICE_CLOSED;
    }
    if (InterlockedCompareExchange(&voice->started, 1, 0) != 0) {
        return DIO_VOICE_BUSY;
    }

    if (!voice->external_audio &&
        !dio_audio_capture_open(
            voice->capture_device_name,
            voice->capture_device_id,
            dio_voice_capture_callback,
            voice,
            &voice->capture,
            error,
            sizeof(error))) {
        InterlockedExchange(&voice->started, 0);
        return DIO_VOICE_AUDIO_FAILURE;
    }
    if (!voice->external_audio) {
        voice->earcon = dio_earcon_open(
            voice->stop_event,
            error,
            sizeof(error));
        if (voice->earcon == NULL) {
            dio_audio_capture_close(&voice->capture);
            InterlockedExchange(&voice->started, 0);
            return DIO_VOICE_AUDIO_FAILURE;
        }
    }
    voice->recognition_thread = CreateThread(
        NULL,
        0u,
        dio_voice_recognition_worker,
        voice,
        0u,
        NULL);
    voice->speech_thread = CreateThread(
        NULL,
        0u,
        dio_voice_speech_worker,
        voice,
        0u,
        NULL);
    if (voice->recognition_thread == NULL ||
        voice->speech_thread == NULL) {
        (void)SetEvent(voice->stop_event);
        if (voice->recognition_thread != NULL) {
            (void)WaitForSingleObject(voice->recognition_thread, INFINITE);
            (void)CloseHandle(voice->recognition_thread);
            voice->recognition_thread = NULL;
        }
        if (voice->speech_thread != NULL) {
            (void)WaitForSingleObject(voice->speech_thread, INFINITE);
            (void)CloseHandle(voice->speech_thread);
            voice->speech_thread = NULL;
        }
        dio_audio_capture_close(&voice->capture);
        dio_earcon_close(voice->earcon);
        voice->earcon = NULL;
        (void)ResetEvent(voice->stop_event);
        InterlockedExchange(&voice->started, 0);
        return DIO_VOICE_PLATFORM_FAILURE;
    }
    if (!voice->external_audio &&
        !dio_audio_capture_start(
            &voice->capture,
            error,
            sizeof(error))) {
        (void)SetEvent(voice->stop_event);
        (void)WaitForSingleObject(voice->recognition_thread, INFINITE);
        (void)WaitForSingleObject(voice->speech_thread, INFINITE);
        (void)CloseHandle(voice->recognition_thread);
        (void)CloseHandle(voice->speech_thread);
        voice->recognition_thread = NULL;
        voice->speech_thread = NULL;
        dio_audio_capture_close(&voice->capture);
        dio_earcon_close(voice->earcon);
        voice->earcon = NULL;
        (void)ResetEvent(voice->stop_event);
        InterlockedExchange(&voice->started, 0);
        return DIO_VOICE_AUDIO_FAILURE;
    }

    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_READY,
        0u,
        NULL,
        0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
    dio_voice_emit(
        voice,
        DIO_VOICE_EVENT_IDLE,
        0u,
        NULL,
        0u,
        0.0f,
        DIO_VOICE_SPEECH_NONE,
        false,
        0u);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_play_processing_earcon(
    DioVoiceCore *voice,
    uint64_t requested_at_ms,
    unsigned int *start_latency_ms)
{
    char error[256];
    bool played;

    if (voice == NULL || start_latency_ms == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->closing, 0, 0) != 0) {
        return DIO_VOICE_CLOSED;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0 ||
        InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (voice->external_audio) {
        *start_latency_ms = 0u;
        return DIO_VOICE_OK;
    }
    played = dio_earcon_play_processing(
        voice->earcon,
        (ULONGLONG)requested_at_ms,
        start_latency_ms,
        error,
        sizeof(error));
    if (!played) {
        dio_voice_emit_error(
            voice,
            DIO_VOICE_ERROR_EARCON,
            error);
    }
    return played ? DIO_VOICE_OK : DIO_VOICE_AUDIO_FAILURE;
}

DioVoiceResult dio_voice_set_paused(
    DioVoiceCore *voice,
    bool paused)
{
    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    InterlockedExchange(&voice->paused, paused ? 1 : 0);
    (void)SetEvent(voice->audio_event);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_push_to_talk(DioVoiceCore *voice)
{
    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(
            &voice->speech_active,
            0,
            0) != 0) {
        return DIO_VOICE_BUSY;
    }
    InterlockedExchange(&voice->push_to_talk_requested, 1);
    InterlockedExchange(&voice->paused, 0);
    (void)SetEvent(voice->audio_event);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_follow_up(DioVoiceCore *voice)
{
    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (voice->follow_up_ms == 0u) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(
            &voice->speech_active,
            0,
            0) != 0) {
        return DIO_VOICE_BUSY;
    }
    InterlockedExchange(&voice->follow_up_requested, 1);
    InterlockedExchange(&voice->paused, 0);
    (void)SetEvent(voice->audio_event);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_cancel(DioVoiceCore *voice)
{
    DioSpeechItem *cancelled;

    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }

    InterlockedExchange(&voice->cancel_requested, 1);
    (void)SetEvent(voice->audio_event);
    (void)SetEvent(voice->speech_cancel_event);
    if (InterlockedCompareExchange(
            &voice->speech_active,
            0,
            0) != 0) {
        dio_tts_server_cancel(voice->tts_server);
    }

    EnterCriticalSection(&voice->speech_lock);
    cancelled = voice->speech_head;
    voice->speech_head = NULL;
    voice->speech_tail = NULL;
    voice->speech_count = 0u;
    if (voice->pcm_stream != NULL) {
        dio_pcm_stream_cancel(voice->pcm_stream);
        if (!voice->pcm_stream_active) {
            voice->pcm_stream = NULL;
            voice->pcm_stream_id = 0u;
        }
    }
    if (InterlockedCompareExchange(&voice->speech_active, 0, 0) == 0) {
        (void)SetEvent(voice->speech_idle_event);
    } else {
        (void)ResetEvent(voice->speech_idle_event);
    }
    LeaveCriticalSection(&voice->speech_lock);
    (void)SetEvent(voice->speech_cancel_event);

    while (cancelled != NULL) {
        DioSpeechItem *next = cancelled->next;
        dio_voice_emit(
            voice,
            DIO_VOICE_EVENT_SPEECH_COMPLETE,
            cancelled->utterance_id,
            NULL,
            0u,
            0.0f,
            DIO_VOICE_SPEECH_CANCELLED,
            false,
            0u);
        dio_pcm_stream_destroy(cancelled->stream);
        free(cancelled);
        cancelled = next;
    }
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_speak(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    const char *utf8_text,
    size_t text_length)
{
    DioSpeechItem *item;

    if (voice == NULL || utf8_text == NULL || text_length == 0u ||
        text_length >
            SIZE_MAX - offsetof(DioSpeechItem, text) - 1u) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    item = (DioSpeechItem *)malloc(
        offsetof(DioSpeechItem, text) + text_length + 1u);
    if (item == NULL) {
        return DIO_VOICE_OUT_OF_MEMORY;
    }
    item->next = NULL;
    item->utterance_id = utterance_id;
    item->text_length = text_length;
    item->stream = NULL;
    (void)memcpy(item->text, utf8_text, text_length);
    item->text[text_length] = '\0';

    EnterCriticalSection(&voice->speech_lock);
    if (voice->speech_count >= DIO_SPEECH_QUEUE_LIMIT) {
        LeaveCriticalSection(&voice->speech_lock);
        free(item);
        return DIO_VOICE_BUSY;
    }
    if (voice->speech_tail == NULL) {
        voice->speech_head = item;
    } else {
        voice->speech_tail->next = item;
    }
    voice->speech_tail = item;
    ++voice->speech_count;
    (void)ResetEvent(voice->speech_idle_event);
    LeaveCriticalSection(&voice->speech_lock);
    (void)SetEvent(voice->speech_event);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_stream_start(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    unsigned int sample_rate,
    const int16_t *samples,
    size_t sample_count)
{
    DioSpeechItem *item;
    DioPcmStream *stream;

    if (voice == NULL ||
        samples == NULL ||
        sample_count == 0u ||
        sample_rate < 8000u ||
        sample_rate > 48000u) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    stream = dio_pcm_stream_create(sample_rate);
    item = (DioSpeechItem *)calloc(1u, sizeof(*item));
    if (stream == NULL || item == NULL) {
        dio_pcm_stream_destroy(stream);
        free(item);
        return DIO_VOICE_OUT_OF_MEMORY;
    }
    if (!dio_pcm_stream_write(stream, samples, sample_count)) {
        dio_pcm_stream_destroy(stream);
        free(item);
        return DIO_VOICE_BUSY;
    }
    item->utterance_id = utterance_id;
    item->stream = stream;

    EnterCriticalSection(&voice->speech_lock);
    if (voice->speech_count >= DIO_SPEECH_QUEUE_LIMIT ||
        voice->pcm_stream != NULL) {
        LeaveCriticalSection(&voice->speech_lock);
        dio_pcm_stream_destroy(stream);
        free(item);
        return DIO_VOICE_BUSY;
    }
    if (voice->speech_tail == NULL) {
        voice->speech_head = item;
    } else {
        voice->speech_tail->next = item;
    }
    voice->speech_tail = item;
    ++voice->speech_count;
    voice->pcm_stream = stream;
    voice->pcm_stream_id = utterance_id;
    voice->pcm_stream_active = false;
    (void)ResetEvent(voice->speech_idle_event);
    LeaveCriticalSection(&voice->speech_lock);
    (void)SetEvent(voice->speech_event);
    return DIO_VOICE_OK;
}

DioVoiceResult dio_voice_stream_write(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    const int16_t *samples,
    size_t sample_count)
{
    DioVoiceResult result;

    if (voice == NULL ||
        samples == NULL ||
        sample_count == 0u) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    EnterCriticalSection(&voice->speech_lock);
    if (voice->pcm_stream == NULL ||
        voice->pcm_stream_id != utterance_id) {
        result = DIO_VOICE_NOT_READY;
    } else {
        result =
            dio_pcm_stream_write(
                voice->pcm_stream,
                samples,
                sample_count)
                ? DIO_VOICE_OK
                : DIO_VOICE_BUSY;
    }
    LeaveCriticalSection(&voice->speech_lock);
    return result;
}

DioVoiceResult dio_voice_stream_finish(
    DioVoiceCore *voice,
    uint64_t utterance_id)
{
    DioVoiceResult result;

    if (voice == NULL) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    EnterCriticalSection(&voice->speech_lock);
    if (voice->pcm_stream == NULL ||
        voice->pcm_stream_id != utterance_id) {
        result = DIO_VOICE_NOT_READY;
    } else {
        result =
            dio_pcm_stream_finish(voice->pcm_stream)
                ? DIO_VOICE_OK
                : DIO_VOICE_BUSY;
    }
    LeaveCriticalSection(&voice->speech_lock);
    return result;
}

DioVoiceResult dio_voice_feed_audio(
    DioVoiceCore *voice,
    const int16_t *samples,
    size_t sample_count)
{
    if (voice == NULL || samples == NULL || sample_count == 0u ||
        sample_count > DIO_VOICE_FRAME_SAMPLES) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (!voice->external_audio) {
        return DIO_VOICE_INVALID_ARGUMENT;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) == 0) {
        return DIO_VOICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&voice->failed, 0, 0) != 0) {
        return DIO_VOICE_NOT_READY;
    }
    dio_voice_enqueue_samples(voice, samples, sample_count);
    return DIO_VOICE_OK;
}

uint64_t dio_voice_dropped_frames(const DioVoiceCore *voice)
{
    if (voice == NULL) {
        return 0u;
    }
    return (uint64_t)InterlockedCompareExchange64(
        (volatile LONG64 *)&voice->dropped_frames,
        0,
        0);
}

void dio_voice_close(DioVoiceCore *voice)
{
    DioSpeechItem *item;

    if (voice == NULL) {
        return;
    }
    if (InterlockedCompareExchange(&voice->started, 0, 0) != 0) {
        (void)dio_voice_cancel(voice);
        (void)WaitForSingleObject(voice->speech_idle_event, INFINITE);
    }
    InterlockedExchange(&voice->closing, 1);
    (void)SetEvent(voice->stop_event);
    (void)SetEvent(voice->audio_event);
    (void)SetEvent(voice->speech_event);
    (void)SetEvent(voice->speech_cancel_event);
    dio_audio_capture_close(&voice->capture);

    if (voice->recognition_thread != NULL) {
        (void)WaitForSingleObject(voice->recognition_thread, INFINITE);
        (void)CloseHandle(voice->recognition_thread);
    }
    if (voice->speech_thread != NULL) {
        (void)WaitForSingleObject(voice->speech_thread, INFINITE);
        (void)CloseHandle(voice->speech_thread);
    }
    dio_earcon_close(voice->earcon);
    voice->earcon = NULL;

    dio_voice_command_close(voice);
    dio_vosk_runtime_close(&voice->vosk);
    dio_silero_close(&voice->vad);
    dio_wake_close(&voice->wake);
    dio_ort_runtime_close(&voice->ort);

    item = voice->speech_head;
    while (item != NULL) {
        DioSpeechItem *next = item->next;
        dio_pcm_stream_destroy(item->stream);
        free(item);
        item = next;
    }
    if (voice->speech_cancel_event != NULL) {
        (void)CloseHandle(voice->speech_cancel_event);
    }
    if (voice->speech_idle_event != NULL) {
        (void)CloseHandle(voice->speech_idle_event);
    }
    if (voice->speech_event != NULL) {
        (void)CloseHandle(voice->speech_event);
    }
    if (voice->audio_event != NULL) {
        (void)CloseHandle(voice->audio_event);
    }
    if (voice->stop_event != NULL) {
        (void)CloseHandle(voice->stop_event);
    }
    if (voice->locks_ready) {
        DeleteCriticalSection(&voice->speech_lock);
        DeleteCriticalSection(&voice->callback_lock);
    }
    dio_voice_free_config(voice);
    free(voice->ring);
    free(voice->audio_queue);
    free(voice);
}
