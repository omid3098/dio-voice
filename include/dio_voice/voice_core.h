#ifndef DIO_VOICE_VOICE_CORE_H
#define DIO_VOICE_VOICE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dio_voice/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DioVoiceCore DioVoiceCore;
typedef struct DioTtsServer DioTtsServer;

typedef enum DioVoiceResult {
    DIO_VOICE_OK = 0,
    DIO_VOICE_INVALID_ARGUMENT,
    DIO_VOICE_NOT_READY,
    DIO_VOICE_BUSY,
    DIO_VOICE_CLOSED,
    DIO_VOICE_OUT_OF_MEMORY,
    DIO_VOICE_MODEL_FAILURE,
    DIO_VOICE_AUDIO_FAILURE,
    DIO_VOICE_PLATFORM_FAILURE
} DioVoiceResult;

typedef enum DioVoiceEventType {
    DIO_VOICE_EVENT_READY = 0,
    DIO_VOICE_EVENT_WAKE,
    DIO_VOICE_EVENT_LISTENING,
    DIO_VOICE_EVENT_LEVEL,
    DIO_VOICE_EVENT_TRANSCRIPT,
    DIO_VOICE_EVENT_SPEAKING,
    DIO_VOICE_EVENT_SPEECH_COMPLETE,
    DIO_VOICE_EVENT_IDLE,
    DIO_VOICE_EVENT_ERROR
} DioVoiceEventType;

typedef enum DioVoiceSpeechOutcome {
    DIO_VOICE_SPEECH_NONE = 0,
    DIO_VOICE_SPEECH_SUCCEEDED,
    DIO_VOICE_SPEECH_CANCELLED,
    DIO_VOICE_SPEECH_FAILED
} DioVoiceSpeechOutcome;

/*
 * Stable voice-domain codes carried by ERROR events. They are deliberately
 * not Win32 last-error values: inference libraries and the earcon backend
 * report text plus one of these portable categories.
 */
typedef enum DioVoiceErrorCode {
    DIO_VOICE_ERROR_NONE = 0,
    DIO_VOICE_ERROR_INFERENCE = 0x1001,
    DIO_VOICE_ERROR_EARCON = 0x1002
} DioVoiceErrorCode;

typedef struct DioVoiceEvent {
    DioVoiceEventType type;
    uint64_t utterance_id;
    const char *text;
    size_t text_length;
    float level;
    /* WAKE: verified-wake to first listening-earcon callback latency. */
    unsigned int latency_ms;
    uint64_t dropped_frames;
    /* One DioVoiceErrorCode value for ERROR, otherwise zero. */
    unsigned long system_error;
    DioVoiceSpeechOutcome speech_outcome;
    bool transcript_is_final;
} DioVoiceEvent;

/*
 * Events are serialized, but may arrive on a worker thread. Event text is
 * UTF-8 and remains valid only for the callback. Return promptly and marshal
 * work to the owner thread; do not call this API from inside the callback.
 */
typedef void (*DioVoiceEventCallback)(
    void *context,
    const DioVoiceEvent *event);

typedef struct DioVoiceConfig {
    const wchar_t *onnxruntime_path;
    const wchar_t *wake_model_directory;
    const wchar_t *silero_model_path;
    const wchar_t *vosk_library_path;
    const wchar_t *vosk_model_directory;
    const wchar_t *capture_device_name;
    const wchar_t *capture_device_id;
    /* Tray-owned local Piper service. */
    DioTtsServer *tts_server;

    float wake_sensitivity;
    float vad_threshold;
    float vad_hysteresis;
    unsigned int command_silence_ms;
    unsigned int command_start_timeout_ms;
    unsigned int command_max_ms;
    /* Zero disables follow-up. */
    unsigned int follow_up_ms;
    /*
     * When external_audio is true no capture device is opened. Tests and
     * embedders feed mono PCM16 at 16 kHz through dio_voice_feed_audio().
     */
    bool external_audio;
    DioVoiceEventCallback callback;
    void *callback_context;
} DioVoiceConfig;

/*
 * open() owns copies of all configuration strings and loads the selected
 * inference runtimes. start() begins recognition and emits READY.
 */
DioVoiceResult dio_voice_open(
    const DioVoiceConfig *config,
    DioVoiceCore **output);

DioVoiceResult dio_voice_start(
    DioVoiceCore *voice);

DioVoiceResult dio_voice_set_paused(
    DioVoiceCore *voice,
    bool paused);

DioVoiceResult dio_voice_push_to_talk(
    DioVoiceCore *voice);

/*
 * Opens one bounded follow-up listen window. The controller calls this only
 * after its agent turn is complete and every queued utterance has emitted
 * SPEECH_COMPLETE; the core never starts follow-up autonomously. A valid
 * request resumes recognition atomically so no intermediate IDLE is emitted.
 */
DioVoiceResult dio_voice_follow_up(
    DioVoiceCore *voice);

/*
 * Plays the processing cue through the core-owned warm output stream.
 * In external-audio mode this is a successful no-op with zero latency.
 */
DioVoiceResult dio_voice_play_processing_earcon(
    DioVoiceCore *voice,
    uint64_t requested_at_ms,
    unsigned int *start_latency_ms);

/*
 * Cancels the active listen/transcription or speech operation. It does not
 * close the engine.
 */
DioVoiceResult dio_voice_cancel(
    DioVoiceCore *voice);

/*
 * Queues UTF-8 speech and returns immediately. utterance_id is copied into
 * SPEAKING and the exactly-one SPEECH_COMPLETE terminal event.
 */
DioVoiceResult dio_voice_speak(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    const char *utf8_text,
    size_t text_length);

/*
 * Streams one mono PCM16 utterance through the existing speech worker.
 * start() includes the first samples so SPEAKING is emitted only when the
 * playback callback actually consumes audio. finish() completes after every
 * accepted sample has reached that callback. dio_voice_cancel() also cancels
 * this path.
 */
DioVoiceResult dio_voice_stream_start(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    unsigned int sample_rate,
    const int16_t *samples,
    size_t sample_count);

DioVoiceResult dio_voice_stream_write(
    DioVoiceCore *voice,
    uint64_t utterance_id,
    const int16_t *samples,
    size_t sample_count);

DioVoiceResult dio_voice_stream_finish(
    DioVoiceCore *voice,
    uint64_t utterance_id);

/*
 * External-audio mode accepts at most one 512-sample frame per call. This
 * bound lets the consumer make progress between producers; callers observe
 * overload through dio_voice_dropped_frames().
 */
DioVoiceResult dio_voice_feed_audio(
    DioVoiceCore *voice,
    const int16_t *samples,
    size_t sample_count);

uint64_t dio_voice_dropped_frames(
    const DioVoiceCore *voice);

/*
 * Stops workers and releases the engine. The owner must serialize close()
 * against every other API call. Accepted speech is cancelled and receives its
 * terminal SPEECH_COMPLETE before shutdown; no callback is made after close()
 * returns.
 */
void dio_voice_close(
    DioVoiceCore *voice);

#ifdef __cplusplus
}
#endif

#endif
