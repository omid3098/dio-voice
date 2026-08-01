#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <werapi.h>

#include <limits.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>

#include "announcement.h"
#include "app_support.h"
#include "sentence_buffer.h"
#include "tts.h"
#include "ui.h"
#include "vault.h"

#include "dio_voice/agent.h"
#include "dio_voice/audio.h"
#include "dio_voice/voice_core.h"

#define DIO_APP_AGENT_NAME L"Dio Harness"

enum {
    DIO_APP_QUEUE_CAP = 256,
    DIO_APP_TEXT_CAP = 8 * 1024,
    DIO_APP_PCM_CAP = DIO_APP_TEXT_CAP / sizeof(int16_t),
    DIO_APP_SPEECH_CAP = 64,
    DIO_APP_ANNOUNCEMENT_CAP = 64 * 1024 + 1,
    DIO_APP_POLL_MS = 200,
    DIO_AGENT_RESTART_DELAY_MS = 2000,
    DIO_AGENT_RESTART_MAX = 3,
    DIO_VOICE_RESTART_DELAY_MS = 2000,
    DIO_VOICE_RESTART_MAX = 3,
    DIO_AGENT_READY_TIMEOUT_MS = 15000,
    DIO_AGENT_SMOKE_TURN_MS = 45000,
    DIO_CONVERSATION_SMOKE_TIMEOUT_MS = 300000
};

typedef enum DioAppEventKind {
    DIO_APP_EVENT_UI = 0,
    DIO_APP_EVENT_AGENT,
    DIO_APP_EVENT_VOICE
} DioAppEventKind;

typedef struct DioAppAgentEvent {
    uint64_t generation;
    DioAgentEventType type;
    bool discovery;
    bool provider_configured;
    uint64_t turn_id;
    uint64_t request_id;
    unsigned int mcp_configured;
    unsigned int mcp_available;
    unsigned int mcp_tools;
    char server[DIO_AGENT_MCP_NAME_CAP];
    char tool[129];
    size_t text_length;
    unsigned long system_error;
    unsigned int http_status;
    bool cancelled;
    bool truncated;
    union {
        char text[DIO_APP_TEXT_CAP];
        int16_t pcm16[DIO_APP_PCM_CAP];
    };
    size_t sample_count;
    unsigned int sample_rate;
} DioAppAgentEvent;

typedef struct DioAppVoiceEvent {
    uint64_t generation;
    DioVoiceEventType type;
    uint64_t utterance_id;
    size_t text_length;
    float level;
    unsigned int latency_ms;
    uint64_t dropped_frames;
    unsigned long system_error;
    DioVoiceSpeechOutcome speech_outcome;
    bool transcript_is_final;
    bool truncated;
    char text[DIO_APP_TEXT_CAP];
} DioAppVoiceEvent;

typedef struct DioAppEvent {
    DioAppEventKind kind;
    union {
        DioUiCommand ui;
        DioAppAgentEvent agent;
        DioAppVoiceEvent voice;
    } data;
} DioAppEvent;

typedef enum DioAppSpeechKind {
    DIO_APP_SPEECH_AGENT = 0,
    DIO_APP_SPEECH_ANNOUNCEMENT
} DioAppSpeechKind;

typedef struct DioAppSpeech {
    uint64_t id;
    DioAppSpeechKind kind;
} DioAppSpeech;

struct DioApp;

typedef struct DioAppCallbackContext {
    struct DioApp *app;
    uint64_t generation;
    bool discovery;
} DioAppCallbackContext;

typedef struct DioApp {
    DioPaths paths;
    DioSettings settings;
    DioAgentProfile profile;
    wchar_t provider_api_key[DIO_VAULT_VALUE_CAP];
    bool provider_api_key_excluded_from_wer;
    bool mcp_secret_excluded_from_wer[DIO_AGENT_MCP_MAX];
    DioUi *ui;

    CRITICAL_SECTION queue_lock;
    bool queue_lock_ready;
    DioAppEvent *events;
    size_t event_head;
    size_t event_count;
    HANDLE queue_event;
    HANDLE stop_event;
    HANDLE worker;
    volatile LONG accepting_events;
    volatile LONG queue_overflow;

    DioAgent *agent;
    DioAgent *discovery_agent;
    DioVoiceCore *voice;
    DioTtsServer *tts_server;
    DioAppCallbackContext agent_callback;
    DioAppCallbackContext discovery_callback;
    DioAppCallbackContext voice_callback;
    uint64_t agent_generation;
    uint64_t discovery_generation;
    uint64_t discovery_request_id;
    ULONGLONG discovery_deadline;
    wchar_t discovery_endpoint[DIO_AGENT_BASE_URL_CAP];
    wchar_t discovery_api_key[DIO_VAULT_VALUE_CAP];
    uint64_t voice_generation;
    bool agent_ready;
    bool voice_ready;
    bool paused;
    bool turn_active;
    bool turn_output_suppressed;
    bool agent_complete;
    bool follow_up_allowed;
    bool follow_up_listening;
    bool listening_active;
    bool voice_gated;
    bool push_to_talk_pending;
    bool vault_required_emitted;
    ULONGLONG turn_started_at;
    ULONGLONG response_started_at;
    ULONGLONG agent_started_at;
    ULONGLONG model_started_at;
    ULONGLONG agent_restart_at;
    unsigned int agent_restart_attempts;
    ULONGLONG voice_restart_at;
    unsigned int voice_restart_attempts;
    uint64_t last_dropped_frames;

    DioSentenceBuffer sentences;
    DioAppSpeech speech[DIO_APP_SPEECH_CAP];
    size_t speech_count;
    uint64_t next_speech_id;
    bool first_delta_measured;
    uint64_t active_agent_turn_id;
    unsigned int agent_audio_sample_rate;
    uint64_t stream_speech_id;
    bool stream_audio;

    DioAnnouncementInbox inbox;
    bool inbox_open;
    uint64_t announcement_id;
    wchar_t announcement_receipt[MAX_PATH];
    wchar_t *announcement_text;

    const wchar_t *smoke_wavs[2];
    unsigned int smoke_next_wav;
    unsigned int smoke_agent_turns;
    unsigned int smoke_tts_turn;
    ULONGLONG smoke_deadline;
    ULONGLONG smoke_silent_started_at;
    volatile LONG smoke_exit_code;
} DioApp;

typedef struct DioAgentSmoke {
    CRITICAL_SECTION lock;
    HANDLE event;
    bool ready;
    bool finished;
    bool matched;
    bool failed;
    bool reply_overflow;
    char reply[256];
    size_t reply_length;
} DioAgentSmoke;

static bool dio_app_is_conversation_smoke(
    const DioApp *app) {
    return app->smoke_wavs[0] != NULL;
}

static void dio_app_finish_conversation_smoke(
    DioApp *app,
    int exit_code) {
    if (!dio_app_is_conversation_smoke(app) ||
        InterlockedCompareExchange(
            &app->smoke_exit_code,
            (LONG)exit_code,
            STILL_ACTIVE) != STILL_ACTIVE) {
        return;
    }
    (void)SetEvent(app->stop_event);
    dio_ui_request_exit(app->ui);
}

static void dio_app_feed_conversation_smoke(
    DioApp *app,
    const wchar_t *path) {
    DioAudioPcm pcm;
    char error[256];
    int16_t silence[DIO_VOICE_FRAME_SAMPLES] = {0};
    size_t offset = 0u;
    unsigned int index;
    const unsigned int silence_frames =
        app->settings.command_silence_ms / 32u + 2u;

    ZeroMemory(&pcm, sizeof(pcm));
    if (dio_audio_read_wav_mono_16k(
            path,
            &pcm,
            error,
            sizeof(error)) != DIO_AUDIO_OK) {
        dio_app_finish_conversation_smoke(app, 20);
        return;
    }
    while (offset < pcm.sample_count) {
        const size_t count =
            pcm.sample_count - offset < DIO_VOICE_FRAME_SAMPLES
                ? pcm.sample_count - offset
                : DIO_VOICE_FRAME_SAMPLES;
        if (dio_voice_feed_audio(
                app->voice,
                pcm.samples + offset,
                count) != DIO_VOICE_OK) {
            dio_audio_pcm_free(&pcm);
            dio_app_finish_conversation_smoke(app, 20);
            return;
        }
        offset += count;
        Sleep(1u);
    }
    dio_audio_pcm_free(&pcm);
    for (index = 0u; index < silence_frames; ++index) {
        if (dio_voice_feed_audio(
                app->voice,
                silence,
                DIO_VOICE_FRAME_SAMPLES) != DIO_VOICE_OK) {
            dio_app_finish_conversation_smoke(app, 20);
            return;
        }
        Sleep(1u);
    }
    if (dio_voice_dropped_frames(app->voice) != 0u) {
        dio_app_finish_conversation_smoke(app, 20);
    }
}

static const wchar_t *dio_app_localized(
    const DioApp *app,
    const wchar_t *english,
    const wchar_t *persian) {
    return app->settings.persian ? persian : english;
}

static bool dio_app_join(
    wchar_t *output,
    size_t capacity,
    const wchar_t *directory,
    const wchar_t *relative) {
    const int length = swprintf_s(
        output,
        capacity,
        L"%ls\\%ls",
        directory,
        relative);
    return length > 0 && (size_t)length < capacity;
}

static size_t dio_app_utf8_prefix(
    const char *text,
    size_t length,
    size_t capacity) {
    size_t count = length < capacity - 1u ? length : capacity - 1u;
    if (count == length) {
        return count;
    }
    while (count > 0u &&
           ((unsigned char)text[count] & 0xC0u) == 0x80u) {
        --count;
    }
    return count;
}

static void dio_app_copy_text(
    char *target,
    size_t capacity,
    const char *text,
    size_t length,
    size_t *copied,
    bool *truncated) {
    const size_t count =
        text != NULL && length != 0u
            ? dio_app_utf8_prefix(text, length, capacity)
            : 0u;
    if (count != 0u) {
        memcpy(target, text, count);
    }
    target[count] = '\0';
    *copied = count;
    *truncated = count != length;
}

static bool dio_app_is_replaceable(const DioAppEvent *event) {
    return
        (event->kind == DIO_APP_EVENT_VOICE &&
         event->data.voice.type == DIO_VOICE_EVENT_LEVEL) ||
        (event->kind == DIO_APP_EVENT_AGENT &&
         event->data.agent.type == DIO_AGENT_EVENT_PROGRESS);
}

static bool dio_app_same_replaceable(
    const DioAppEvent *left,
    const DioAppEvent *right) {
    if (left->kind != right->kind) {
        return false;
    }
    if (left->kind == DIO_APP_EVENT_VOICE) {
        return
            left->data.voice.type == DIO_VOICE_EVENT_LEVEL &&
            right->data.voice.type == DIO_VOICE_EVENT_LEVEL &&
            left->data.voice.generation ==
                right->data.voice.generation;
    }
    return
        left->kind == DIO_APP_EVENT_AGENT &&
        left->data.agent.type == DIO_AGENT_EVENT_PROGRESS &&
        right->data.agent.type == DIO_AGENT_EVENT_PROGRESS &&
        left->data.agent.generation ==
            right->data.agent.generation;
}

static bool dio_app_enqueue(
    DioApp *app,
    const DioAppEvent *event) {
    size_t offset;
    bool stored = false;

    if (app == NULL ||
        event == NULL ||
        InterlockedCompareExchange(
            &app->accepting_events,
            0,
            0) == 0) {
        return false;
    }
    EnterCriticalSection(&app->queue_lock);
    if (dio_app_is_replaceable(event)) {
        for (offset = app->event_count; offset > 0u; --offset) {
            const size_t index =
                (app->event_head + offset - 1u) %
                DIO_APP_QUEUE_CAP;
            if (dio_app_same_replaceable(
                    &app->events[index],
                    event)) {
                app->events[index] = *event;
                stored = true;
                break;
            }
        }
    }
    if (!stored && app->event_count < DIO_APP_QUEUE_CAP) {
        const size_t tail =
            (app->event_head + app->event_count) %
            DIO_APP_QUEUE_CAP;
        app->events[tail] = *event;
        app->event_count += 1u;
        stored = true;
    }
    LeaveCriticalSection(&app->queue_lock);
    if (stored) {
        (void)SetEvent(app->queue_event);
    } else if (!dio_app_is_replaceable(event)) {
        (void)InterlockedExchange(&app->queue_overflow, 1);
        (void)SetEvent(app->queue_event);
    }
    return stored;
}

static bool dio_app_dequeue(
    DioApp *app,
    DioAppEvent *event) {
    bool available = false;
    EnterCriticalSection(&app->queue_lock);
    if (app->event_count != 0u) {
        *event = app->events[app->event_head];
        SecureZeroMemory(
            &app->events[app->event_head],
            sizeof(app->events[app->event_head]));
        app->event_head =
            (app->event_head + 1u) % DIO_APP_QUEUE_CAP;
        app->event_count -= 1u;
        available = true;
    }
    LeaveCriticalSection(&app->queue_lock);
    return available;
}

static void dio_app_post_state(
    DioApp *app,
    DioUiState state) {
    DioUiEvent event;
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_STATE;
    event.state = state;
    (void)dio_ui_post(app->ui, &event);
}

static void dio_app_post_wide(
    DioApp *app,
    DioUiEventKind kind,
    const wchar_t *text) {
    DioUiEvent event;
    ZeroMemory(&event, sizeof(event));
    event.kind = kind;
    (void)wcsncpy_s(
        event.text,
        _countof(event.text),
        text != NULL ? text : L"",
        _TRUNCATE);
    (void)dio_ui_post(app->ui, &event);
}

static void dio_app_post_chip(
    DioApp *app,
    DioChipKind chip,
    const wchar_t *text) {
    DioUiEvent event;
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CHIP;
    event.chip = chip;
    (void)wcsncpy_s(
        event.text,
        _countof(event.text),
        text != NULL ? text : L"",
        _TRUNCATE);
    (void)dio_ui_post(app->ui, &event);
}

static void dio_app_post_level(
    DioApp *app,
    float level) {
    DioUiEvent event;
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_LEVEL;
    event.value = level;
    (void)dio_ui_post(app->ui, &event);
}

static void dio_app_post_utf8(
    DioApp *app,
    DioUiEventKind kind,
    const char *text,
    size_t length) {
    DioUiEvent event;
    int converted;

    ZeroMemory(&event, sizeof(event));
    event.kind = kind;
    if (text != NULL && length != 0u) {
        if (length > (size_t)INT_MAX) {
            return;
        }
        converted = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            (int)length,
            event.text,
            (int)_countof(event.text) - 1);
        if (converted <= 0) {
            return;
        }
        event.text[converted] = L'\0';
    }
    (void)dio_ui_post(app->ui, &event);
}

static void dio_app_ui_callback(
    void *context,
    const DioUiCommand *command) {
    DioApp *app = (DioApp *)context;
    DioAppEvent event;
    if (app == NULL || command == NULL) {
        return;
    }
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_APP_EVENT_UI;
    event.data.ui = *command;
    DioAgentProfile *profile = NULL;
    if (command->kind == DIO_UI_COMMAND_SETTINGS_CHANGED &&
        command->profile != NULL) {
        profile = (DioAgentProfile *)calloc(1u, sizeof(*profile));
        if (profile == NULL) {
            SecureZeroMemory(event.data.ui.api_key, sizeof(event.data.ui.api_key));
            return;
        }
        dio_agent_profile_init(profile);
        if (profile->system_prompt == NULL ||
            !dio_agent_profile_copy(profile, command->profile)) {
            dio_agent_profile_free(profile);
            free(profile);
            SecureZeroMemory(event.data.ui.api_key, sizeof(event.data.ui.api_key));
            return;
        }
        event.data.ui.profile = profile;
    }
    if (!dio_app_enqueue(app, &event) && profile != NULL) {
        dio_agent_profile_free(profile);
        free(profile);
    }
    SecureZeroMemory(event.data.ui.api_key, sizeof(event.data.ui.api_key));
}

static void dio_app_post_discovery_result(
    const DioAppCallbackContext *callback,
    const DioAgentEvent *source) {
    const wchar_t *marker = NULL;
    wchar_t *models = NULL;
    if (source->type == DIO_AGENT_EVENT_MODELS_ERROR) {
        marker = source->text != NULL &&
            ((source->text_length == sizeof("unauthorized") - 1u &&
              memcmp(source->text, "unauthorized", sizeof("unauthorized") - 1u) == 0) ||
             (source->text_length == sizeof("forbidden") - 1u &&
              memcmp(source->text, "forbidden", sizeof("forbidden") - 1u) == 0))
            ? L"!auth"
            : L"!error";
    } else if (source->type == DIO_AGENT_EVENT_MODELS) {
        if (source->text_length > INT_MAX) {
            marker = L"!error";
        } else {
            const int required = source->text_length == 0u
                ? 0
                : MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    source->text,
                    (int)source->text_length,
                    NULL,
                    0);
            if (source->text_length == 0u || required > 0) {
                models = (wchar_t *)calloc(
                    (size_t)required + 1u,
                    sizeof(*models));
            }
            if (models != NULL && required > 0 &&
                MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    source->text,
                    (int)source->text_length,
                    models,
                    required) != required) {
                free(models);
                models = NULL;
            }
            if (models == NULL) {
                marker = L"!error";
            }
        }
    }
    (void)dio_ui_post_models(
        callback->app->ui,
        callback->generation,
        models != NULL ? models : marker != NULL ? marker : L"!error");
    free(models);
}

static void dio_app_agent_callback(
    void *context,
    const DioAgentEvent *source) {
    const DioAppCallbackContext *callback =
        (const DioAppCallbackContext *)context;
    DioAppEvent event;
    size_t offset = 0u;

    if (callback == NULL ||
        callback->app == NULL ||
        source == NULL) {
        return;
    }
    if (callback->discovery &&
        (source->type == DIO_AGENT_EVENT_MODELS ||
         source->type == DIO_AGENT_EVENT_MODELS_ERROR)) {
        dio_app_post_discovery_result(callback, source);
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_APP_EVENT_AGENT;
        event.data.agent.generation = callback->generation;
        event.data.agent.type = source->type;
        event.data.agent.discovery = true;
        event.data.agent.turn_id = source->turn_id;
        (void)dio_app_enqueue(callback->app, &event);
        return;
    }
    if (source->type == DIO_AGENT_EVENT_MCP_STATUS) {
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_APP_EVENT_AGENT;
        event.data.agent.generation = callback->generation;
        event.data.agent.type = source->type;
        event.data.agent.turn_id = source->turn_id;
        event.data.agent.mcp_configured = source->mcp_configured;
        event.data.agent.mcp_available = source->mcp_available;
        event.data.agent.mcp_tools = source->mcp_tools;
        (void)dio_app_enqueue(callback->app, &event);
        return;
    }
    if (source->type == DIO_AGENT_EVENT_TOOL_APPROVAL_REQUIRED) {
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_APP_EVENT_AGENT;
        event.data.agent.generation = callback->generation;
        event.data.agent.type = source->type;
        event.data.agent.turn_id = source->turn_id;
        event.data.agent.request_id = source->request_id;
        (void)strncpy_s(
            event.data.agent.server,
            sizeof(event.data.agent.server),
            source->server_name != NULL ? source->server_name : "",
            _TRUNCATE);
        (void)strncpy_s(
            event.data.agent.tool,
            sizeof(event.data.agent.tool),
            source->tool_name != NULL ? source->tool_name : "",
            _TRUNCATE);
        dio_app_copy_text(
            event.data.agent.text,
            _countof(event.data.agent.text),
            source->arguments_json,
            source->arguments_length,
            &event.data.agent.text_length,
            &event.data.agent.truncated);
        (void)dio_app_enqueue(callback->app, &event);
        return;
    }
    if (source->type == DIO_AGENT_EVENT_AUDIO_PCM16) {
        if (source->pcm16 == NULL ||
            source->sample_count == 0u ||
            source->sample_rate == 0u) {
            return;
        }
        while (offset < source->sample_count) {
            const size_t count =
                source->sample_count - offset < DIO_APP_PCM_CAP
                    ? source->sample_count - offset
                    : DIO_APP_PCM_CAP;
            ZeroMemory(&event, sizeof(event));
            event.kind = DIO_APP_EVENT_AGENT;
            event.data.agent.generation = callback->generation;
            event.data.agent.type = source->type;
            event.data.agent.discovery = callback->discovery;
            event.data.agent.provider_configured = source->provider_configured;
            event.data.agent.turn_id = source->turn_id;
            event.data.agent.sample_rate = source->sample_rate;
            event.data.agent.sample_count = count;
            (void)memcpy(
                event.data.agent.pcm16,
                source->pcm16 + offset,
                count * sizeof(*source->pcm16));
            if (!dio_app_enqueue(callback->app, &event)) {
                return;
            }
            offset += count;
        }
        return;
    }
    do {
        const size_t remaining =
            source->text_length > offset
                ? source->text_length - offset
                : 0u;
        ZeroMemory(&event, sizeof(event));
    event.kind = DIO_APP_EVENT_AGENT;
    event.data.agent.generation = callback->generation;
    event.data.agent.type = source->type;
    event.data.agent.discovery = callback->discovery;
    event.data.agent.provider_configured = source->provider_configured;
        event.data.agent.turn_id = source->turn_id;
        event.data.agent.sample_rate = source->sample_rate;
        event.data.agent.system_error = source->system_error;
        event.data.agent.http_status = source->http_status;
        event.data.agent.cancelled = source->cancelled;
        dio_app_copy_text(
            event.data.agent.text,
            _countof(event.data.agent.text),
            source->text != NULL ? source->text + offset : NULL,
            remaining,
            &event.data.agent.text_length,
            &event.data.agent.truncated);
        if (!dio_app_enqueue(callback->app, &event)) {
            return;
        }
        if (remaining != 0u &&
            event.data.agent.text_length == 0u) {
            return;
        }
        offset += event.data.agent.text_length;
    } while (
        source->type == DIO_AGENT_EVENT_TEXT_DELTA &&
        offset < source->text_length);
}

static void dio_app_voice_callback(
    void *context,
    const DioVoiceEvent *source) {
    const DioAppCallbackContext *callback =
        (const DioAppCallbackContext *)context;
    DioAppEvent event;

    if (callback == NULL ||
        callback->app == NULL ||
        source == NULL) {
        return;
    }
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_APP_EVENT_VOICE;
    event.data.voice.generation = callback->generation;
    event.data.voice.type = source->type;
    event.data.voice.utterance_id = source->utterance_id;
    event.data.voice.level = source->level;
    event.data.voice.latency_ms = source->latency_ms;
    event.data.voice.dropped_frames = source->dropped_frames;
    event.data.voice.system_error = source->system_error;
    event.data.voice.speech_outcome = source->speech_outcome;
    event.data.voice.transcript_is_final =
        source->transcript_is_final;
    dio_app_copy_text(
        event.data.voice.text,
        _countof(event.data.voice.text),
        source->text,
        source->text_length,
        &event.data.voice.text_length,
        &event.data.voice.truncated);
    (void)dio_app_enqueue(callback->app, &event);
}

static bool dio_app_resolve_harness_executable(
    wchar_t *output,
    size_t capacity) {
    wchar_t module[MAX_PATH];
    DWORD attributes;
    const DWORD module_length = GetModuleFileNameW(
        NULL,
        module,
        (DWORD)_countof(module));
    wchar_t *separator =
        module_length != 0u && module_length < _countof(module)
            ? wcsrchr(module, L'\\')
            : NULL;
    if (separator != NULL) {
        *separator = L'\0';
        if (dio_app_join(
                output,
                capacity,
                module,
                L"dio.exe")) {
            attributes = GetFileAttributesW(output);
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) {
                return true;
            }
        }
    }
    const DWORD length = SearchPathW(
        NULL,
        L"dio.exe",
        NULL,
        (DWORD)capacity,
        output,
        NULL);
    return length != 0u && length < capacity;
}

static bool dio_app_agent_installed(void) {
    wchar_t executable[MAX_PATH];
    return dio_app_resolve_harness_executable(
        executable,
        _countof(executable));
}

static bool dio_app_schedule_agent_restart(
    DioApp *app,
    ULONGLONG now);

static void dio_app_require_vault(DioApp *app) {
    if (app->vault_required_emitted) {
        return;
    }
    app->vault_required_emitted = true;
    dio_app_post_wide(
        app,
        DIO_UI_EVENT_VAULT_REQUIRED,
        dio_app_localized(
            app,
            L"Unlock the portable vault before voice and model services can start.",
            L"برای راه‌اندازی صدا و مدل، ابتدا vault قابل‌حمل را باز کنید."));
}

static void dio_app_close_discovery(DioApp *app) {
    DioAgent *agent = app->discovery_agent;
    app->discovery_agent = NULL;
    app->discovery_request_id = 0u;
    app->discovery_deadline = 0u;
    if (agent != NULL) {
        dio_agent_close(agent);
    }
    SecureZeroMemory(
        app->discovery_api_key,
        sizeof(app->discovery_api_key));
}

static void dio_app_open_discovery(DioApp *app) {
    wchar_t executable[MAX_PATH];
    DioAgentConfig config;
    DioAgentProviderConfig provider;
    if (app->discovery_agent != NULL ||
        app->discovery_endpoint[0] == L'\0' ||
        !dio_app_resolve_harness_executable(
            executable,
            _countof(executable))) {
        (void)dio_ui_post_models(
            app->ui,
            app->discovery_generation,
            L"!error");
        app->discovery_deadline = 0u;
        SecureZeroMemory(
            app->discovery_api_key,
            sizeof(app->discovery_api_key));
        return;
    }
    ZeroMemory(&config, sizeof(config));
    ZeroMemory(&provider, sizeof(provider));
    provider.base_url = app->discovery_endpoint;
    provider.api_key = app->discovery_api_key;
    provider.system_prompt = L"";
    app->discovery_callback.app = app;
    app->discovery_callback.generation = app->discovery_generation;
    app->discovery_callback.discovery = true;
    config.executable_path = executable;
    config.working_directory = app->paths.workspace;
    config.provider = &provider;
    config.callback = dio_app_agent_callback;
    config.callback_context = &app->discovery_callback;
    if (dio_agent_open(&config, &app->discovery_agent) != DIO_AGENT_OK) {
        app->discovery_agent = NULL;
        app->discovery_deadline = 0u;
        SecureZeroMemory(
            app->discovery_api_key,
            sizeof(app->discovery_api_key));
        (void)dio_ui_post_models(
            app->ui,
            app->discovery_generation,
            L"!error");
    }
}

static void dio_app_maybe_timeout_discovery(DioApp *app) {
    if (app->discovery_agent == NULL ||
        !dio_model_discovery_deadline_reached(
            app->discovery_deadline,
            GetTickCount64())) {
        return;
    }
    const uint64_t generation = app->discovery_generation;
    dio_app_close_discovery(app);
    (void)dio_ui_post_models(app->ui, generation, L"!timeout");
}

static void dio_app_open_agent(DioApp *app) {
    DioAgentConfig config;
    DioAgentResult result;
    DioAgentProviderConfig provider;
    DioAgentMcpServerConfig mcp_servers[DIO_AGENT_MCP_MAX];
    wchar_t executable[MAX_PATH];

    if (app->agent != NULL ||
        !dio_agent_profile_configured(&app->profile)) {
        return;
    }
    if (!dio_agent_profile_session_secrets_ready(
            &app->profile,
            app->provider_api_key)) {
        dio_app_require_vault(app);
        return;
    }
    const bool installed = dio_app_resolve_harness_executable(
        executable,
        _countof(executable));
    if (!installed) {
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                L"Dio Harness is not installed or is not on PATH.",
                L"Dio Harness \u0646\u0635\u0628 \u0646\u06cc\u0633\u062a "
                L"\u06cc\u0627 \u062f\u0631 PATH \u0646\u06cc\u0633\u062a."));
        dio_metric(
            &app->paths,
            "agent_missing",
            0u,
            ERROR_FILE_NOT_FOUND);
        return;
    }
    ZeroMemory(&config, sizeof(config));
    ZeroMemory(&provider, sizeof(provider));
    ZeroMemory(mcp_servers, sizeof(mcp_servers));
    config.executable_path = executable;
    provider.base_url = app->profile.base_url;
    provider.api_key = app->provider_api_key;
    provider.model = app->profile.model;
    provider.reasoning_effort = app->profile.reasoning_effort;
    provider.service_tier = app->profile.service_tier;
    provider.system_prompt = app->profile.system_prompt;
    config.provider = &provider;
    for (size_t index = 0u;
         index < app->profile.mcp_server_count;
         ++index) {
        const DioMcpServer *source = &app->profile.mcp_servers[index];
        DioAgentMcpServerConfig *target = &mcp_servers[index];
        target->name = source->name;
        target->target = source->target;
        target->arguments = source->arguments;
        target->working_directory = source->working_directory;
        target->secret = source->secret_value;
        target->always_tools = source->always_tools;
        target->stdio = source->stdio;
        target->enabled = source->enabled;
    }
    config.mcp_servers = mcp_servers;
    config.mcp_server_count = app->profile.mcp_server_count;
    app->agent_generation += 1u;
    app->agent_callback.app = app;
    app->agent_callback.generation = app->agent_generation;
    app->agent_callback.discovery = false;
    config.working_directory = app->paths.workspace;
    config.callback = dio_app_agent_callback;
    config.callback_context = &app->agent_callback;
    result = dio_agent_open(&config, &app->agent);
    if (result != DIO_AGENT_OK) {
        app->agent = NULL;
        dio_metric(
            &app->paths,
            "agent_start_error",
            0u,
            (unsigned long)result);
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                DIO_APP_AGENT_NAME L" could not be started.",
                L"\u0631\u0627\u0647\u200c\u0627\u0646\u062f\u0627\u0632\u06cc "
                DIO_APP_AGENT_NAME
                L" \u0645\u0645\u06a9\u0646 \u0646\u0634\u062f."));
        if (!dio_app_schedule_agent_restart(
                app,
                GetTickCount64())) {
            dio_metric(
                &app->paths,
                "agent_restart_exhausted",
                0u,
                ERROR_RETRY);
        }
    } else {
        app->agent_started_at = GetTickCount64();
    }
}

static bool dio_app_schedule_agent_restart(
    DioApp *app,
    ULONGLONG now) {
    if (app->agent_restart_attempts >= DIO_AGENT_RESTART_MAX) {
        app->agent_restart_at = 0u;
        return false;
    }
    app->agent_restart_at = now + DIO_AGENT_RESTART_DELAY_MS;
    return true;
}

static bool dio_app_take_agent_restart(
    DioApp *app,
    ULONGLONG now) {
    if (app->agent_restart_at == 0u ||
        now < app->agent_restart_at) {
        return false;
    }
    app->agent_restart_at = 0u;
    app->agent_restart_attempts += 1u;
    return true;
}

static void dio_app_maybe_restart_agent(DioApp *app) {
    const ULONGLONG now = GetTickCount64();

    if (!dio_app_take_agent_restart(app, now)) {
        return;
    }
    dio_metric(
        &app->paths,
        "agent_restart",
        DIO_AGENT_RESTART_DELAY_MS,
        app->agent_restart_attempts);
    dio_app_post_state(app, DIO_UI_LOADING);
    dio_app_open_agent(app);
}

static bool dio_app_schedule_voice_restart(
    DioApp *app,
    ULONGLONG now) {
    if (app->voice_restart_attempts >= DIO_VOICE_RESTART_MAX) {
        app->voice_restart_at = 0u;
        return false;
    }
    app->voice_restart_at =
        now + DIO_VOICE_RESTART_DELAY_MS;
    return true;
}

static bool dio_app_take_voice_restart(
    DioApp *app,
    ULONGLONG now) {
    if (app->voice_restart_at == 0u ||
        now < app->voice_restart_at) {
        return false;
    }
    app->voice_restart_at = 0u;
    app->voice_restart_attempts += 1u;
    return true;
}

static void dio_app_close_voice(DioApp *app);

static void dio_app_close_agent(DioApp *app) {
    DioAgent *agent = app->agent;
    app->agent = NULL;
    app->agent_ready = false;
    app->turn_active = false;
    app->turn_output_suppressed = false;
    app->agent_complete = false;
    app->follow_up_allowed = false;
    app->voice_gated = false;
    app->turn_started_at = 0u;
    app->agent_started_at = 0u;
    app->agent_generation += 1u;
    if (agent != NULL) {
        dio_agent_close(agent);
    }
}

static bool dio_app_agent_ready_timed_out(
    const DioApp *app,
    ULONGLONG now) {
    return
        app->agent != NULL &&
        !app->agent_ready &&
        app->agent_started_at != 0u &&
        now >= app->agent_started_at &&
        now - app->agent_started_at >= DIO_AGENT_READY_TIMEOUT_MS;
}

static void dio_app_maybe_timeout_agent(DioApp *app) {
    const ULONGLONG now = GetTickCount64();
    ULONGLONG started_at;

    if (!dio_app_agent_ready_timed_out(app, now)) {
        return;
    }
    started_at = app->agent_started_at;
    dio_metric(
        &app->paths,
        "agent_ready_timeout",
        now - started_at,
        WAIT_TIMEOUT);
    dio_app_close_voice(app);
    dio_app_close_agent(app);
    if (!dio_app_schedule_agent_restart(app, now)) {
        dio_metric(
            &app->paths,
            "agent_restart_exhausted",
            0u,
            ERROR_RETRY);
    }
    dio_app_post_wide(
        app,
        DIO_UI_EVENT_ERROR,
        dio_app_localized(
            app,
            DIO_APP_AGENT_NAME L" did not become ready.",
            DIO_APP_AGENT_NAME
            L" \u0622\u0645\u0627\u062f\u0647 \u0646\u0634\u062f."));
}

static void dio_app_clear_speech(DioApp *app) {
    if (app->announcement_receipt[0] != L'\0') {
        dio_announcement_finish(
            app->announcement_receipt,
            false);
    }
    app->announcement_receipt[0] = L'\0';
    app->announcement_id = 0u;
    app->speech_count = 0u;
    app->stream_speech_id = 0u;
    app->stream_audio = false;
    app->active_agent_turn_id = 0u;
    app->agent_audio_sample_rate = 0u;
    dio_sentence_buffer_init(&app->sentences);
}

static void dio_app_close_voice(DioApp *app) {
    DioVoiceCore *voice = app->voice;
    app->voice = NULL;
    app->voice_ready = false;
    app->follow_up_listening = false;
    app->listening_active = false;
    app->last_dropped_frames = 0u;
    app->voice_generation += 1u;
    if (voice != NULL) {
        dio_voice_close(voice);
    }
    dio_app_clear_speech(app);
}

static void dio_app_open_tts_server(DioApp *app) {
    wchar_t bundle[MAX_PATH];
    wchar_t state[MAX_PATH];
    char error[256];
    ULONGLONG started_at;

    if (app->tts_server != NULL ||
        dio_app_is_conversation_smoke(app)) {
        return;
    }
    if (!dio_app_join(
            bundle,
            _countof(bundle),
            app->paths.runtime,
            L"piper") ||
        !dio_app_join(
            state,
            _countof(state),
            app->paths.workspace,
            L"piper")) {
        dio_metric(
            &app->paths,
            "piper_start_error",
            0u,
            ERROR_FILENAME_EXCED_RANGE);
        return;
    }
    started_at = GetTickCount64();
    app->tts_server = dio_tts_server_open(
        bundle,
        state,
        error,
        sizeof(error));
    dio_metric(
        &app->paths,
        app->tts_server != NULL
            ? "piper_started"
            : "piper_start_error",
        GetTickCount64() - started_at,
        app->tts_server != NULL ? ERROR_SUCCESS : ERROR_NOT_READY);
}

static void dio_app_open_voice(DioApp *app) {
    wchar_t onnxruntime[MAX_PATH];
    wchar_t porcupine[MAX_PATH];
    wchar_t silero[MAX_PATH];
    wchar_t vosk_library[MAX_PATH];
    wchar_t vosk_model[MAX_PATH];
    DioVoiceConfig config;
    DioVoiceResult result;

    if (app->voice != NULL || !app->agent_ready) {
        return;
    }
    if (!dio_app_join(
            onnxruntime,
            _countof(onnxruntime),
            app->settings.runtime_dir,
            L"onnxruntime\\onnxruntime.dll") ||
        !dio_app_join(
            porcupine,
            _countof(porcupine),
            app->settings.model_dir,
            L"porcupine") ||
        !dio_app_join(
            silero,
            _countof(silero),
            app->settings.model_dir,
            L"silero_vad.onnx") ||
        !dio_app_join(
            vosk_library,
            _countof(vosk_library),
            app->settings.runtime_dir,
            L"vosk\\libvosk.dll") ||
        !dio_app_join(
            vosk_model,
            _countof(vosk_model),
            app->settings.model_dir,
            L"vosk")) {
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                L"A configured voice path is too long.",
                L"\u0645\u0633\u06cc\u0631 "
                L"\u062a\u0646\u0638\u06cc\u0645\u200c\u0634\u062f\u0647\u0654 "
                L"\u0635\u062f\u0627 \u0628\u06cc\u0634 \u0627\u0632 "
                L"\u062d\u062f \u0637\u0648\u0644\u0627\u0646\u06cc "
                L"\u0627\u0633\u062a."));
        return;
    }

    ZeroMemory(&config, sizeof(config));
    app->voice_generation += 1u;
    app->voice_callback.app = app;
    app->voice_callback.generation = app->voice_generation;
    config.onnxruntime_path = onnxruntime;
    config.wake_model_directory = porcupine;
    config.silero_model_path = silero;
    config.vosk_library_path = vosk_library;
    config.vosk_model_directory = vosk_model;
    config.capture_device_name =
        app->settings.microphone_name[0] != L'\0'
            ? app->settings.microphone_name
            : NULL;
    config.capture_device_id =
        app->settings.microphone_id[0] != L'\0'
            ? app->settings.microphone_id
            : NULL;
    config.tts_server = app->tts_server;
    config.wake_sensitivity = app->settings.wake_sensitivity;
    config.vad_threshold = app->settings.vad_threshold;
    config.vad_hysteresis = app->settings.vad_hysteresis;
    config.command_silence_ms =
        app->settings.command_silence_ms;
    config.command_start_timeout_ms =
        app->settings.command_start_timeout_ms;
    config.command_max_ms = app->settings.command_max_ms;
    config.follow_up_ms = app->settings.follow_up_ms;
    config.external_audio =
        dio_app_is_conversation_smoke(app);
    config.callback = dio_app_voice_callback;
    config.callback_context = &app->voice_callback;

    app->model_started_at = GetTickCount64();
    result = dio_voice_open(&config, &app->voice);
    if (result == DIO_VOICE_OK) {
        result = dio_voice_start(app->voice);
    }
    if (result != DIO_VOICE_OK) {
        dio_metric(
            &app->paths,
            "voice_start_error",
            GetTickCount64() - app->model_started_at,
            (unsigned long)result);
        dio_app_close_voice(app);
        if (!dio_app_schedule_voice_restart(
                app,
                GetTickCount64())) {
            dio_metric(
                &app->paths,
                "voice_restart_exhausted",
                0u,
                ERROR_RETRY);
        }
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                L"The voice engine could not be started. Check the "
                L"deployed runtime and model files.",
                L"\u0645\u0648\u062a\u0648\u0631 \u0635\u062f\u0627 "
                L"\u0631\u0627\u0647\u200c\u0627\u0646\u062f\u0627\u0632\u06cc "
                L"\u0646\u0634\u062f. \u0641\u0627\u06cc\u0644\u200c\u0647\u0627\u06cc "
                L"\u0645\u062f\u0644 \u0648 runtime \u0631\u0627 "
                L"\u0628\u0631\u0631\u0633\u06cc \u06a9\u0646\u06cc\u062f."));
    }
}

static void dio_app_maybe_restart_voice(DioApp *app) {
    const ULONGLONG now = GetTickCount64();
    if (!app->agent_ready ||
        app->voice != NULL ||
        !dio_app_take_voice_restart(app, now)) {
        return;
    }
    dio_metric(
        &app->paths,
        "voice_restart",
        DIO_VOICE_RESTART_DELAY_MS,
        app->voice_restart_attempts);
    dio_app_post_state(app, DIO_UI_LOADING);
    dio_app_open_voice(app);
}

static bool dio_app_track_speech(
    DioApp *app,
    uint64_t id,
    DioAppSpeechKind kind) {
    if (app->speech_count == DIO_APP_SPEECH_CAP) {
        return false;
    }
    app->speech[app->speech_count].id = id;
    app->speech[app->speech_count].kind = kind;
    app->speech_count += 1u;
    return true;
}

static bool dio_app_untrack_speech(
    DioApp *app,
    uint64_t id,
    DioAppSpeechKind *kind) {
    size_t index;
    for (index = 0u; index < app->speech_count; ++index) {
        if (app->speech[index].id == id) {
            *kind = app->speech[index].kind;
            memmove(
                &app->speech[index],
                &app->speech[index + 1u],
                (app->speech_count - index - 1u) *
                    sizeof(app->speech[0]));
            app->speech_count -= 1u;
            return true;
        }
    }
    return false;
}

static bool dio_app_is_agent_speech(
    const DioApp *app,
    uint64_t id) {
    size_t index;
    for (index = 0u; index < app->speech_count; ++index) {
        if (app->speech[index].id == id) {
            return
                app->speech[index].kind ==
                DIO_APP_SPEECH_AGENT;
        }
    }
    return false;
}

static bool dio_app_speak(
    DioApp *app,
    DioAppSpeechKind kind,
    const char *text,
    size_t length,
    uint64_t *id_output) {
    DioVoiceResult result;
    uint64_t id = app->next_speech_id + 1u;

    if (id == 0u) {
        id = 1u;
    }
    if (app->voice == NULL ||
        length == 0u ||
        !dio_app_track_speech(app, id, kind)) {
        return false;
    }
    app->next_speech_id = id;
    result = dio_voice_speak(app->voice, id, text, length);
    if (result != DIO_VOICE_OK) {
        DioAppSpeechKind ignored;
        (void)dio_app_untrack_speech(app, id, &ignored);
        dio_metric(
            &app->paths,
            "tts_queue_error",
            0u,
            (unsigned long)result);
        return false;
    }
    if (id_output != NULL) {
        *id_output = id;
    }
    return true;
}

static bool dio_app_speak_sentence(
    void *context,
    const char *text,
    size_t length) {
    DioApp *app = (DioApp *)context;
    if (dio_app_speak(
            app,
            DIO_APP_SPEECH_AGENT,
            text,
            length,
            NULL)) {
        return true;
    }
    app->follow_up_allowed = false;
    dio_app_finish_conversation_smoke(app, 22);
    dio_app_post_wide(
        app,
        DIO_UI_EVENT_ERROR,
        dio_app_localized(
            app,
            L"The response could not be queued for speech.",
            L"\u067e\u0627\u0633\u062e \u0628\u0631\u0627\u06cc "
            L"\u062e\u0648\u0627\u0646\u062f\u0646 \u062f\u0631 "
            L"\u0635\u0641 \u0642\u0631\u0627\u0631 \u0646\u06af\u0631\u0641\u062a."));
    return false;
}

static bool dio_app_stream_pcm(
    DioApp *app,
    const DioAppAgentEvent *event) {
    DioVoiceResult result;

    if (app->voice == NULL ||
        !app->turn_active ||
        event->turn_id != app->active_agent_turn_id ||
        event->sample_rate == 0u ||
        event->sample_rate != app->agent_audio_sample_rate ||
        event->sample_count == 0u) {
        return false;
    }
    if (!app->stream_audio) {
        DioAppSpeechKind ignored;
        uint64_t id = app->next_speech_id + 1u;
        if (id == 0u) {
            id = 1u;
        }
        if (!dio_app_track_speech(
                app,
                id,
                DIO_APP_SPEECH_AGENT)) {
            return false;
        }
        result = dio_voice_stream_start(
            app->voice,
            id,
            event->sample_rate,
            event->pcm16,
            event->sample_count);
        if (result != DIO_VOICE_OK) {
            (void)dio_app_untrack_speech(
                app,
                id,
                &ignored);
            return false;
        }
        app->next_speech_id = id;
        app->stream_speech_id = id;
        app->stream_audio = true;
        dio_sentence_buffer_init(&app->sentences);
        return true;
    }
    return
        dio_voice_stream_write(
            app->voice,
            app->stream_speech_id,
            event->pcm16,
            event->sample_count) == DIO_VOICE_OK;
}

static void dio_app_set_voice_paused(
    DioApp *app);

static void dio_app_maybe_follow_up(DioApp *app) {
    DioVoiceResult result;
    bool allowed;

    if (!app->agent_complete || app->speech_count != 0u) {
        return;
    }
    allowed =
        app->follow_up_allowed &&
        app->settings.follow_up_ms != 0u &&
        !app->paused &&
        app->voice != NULL;
    app->agent_complete = false;
    app->follow_up_allowed = false;
    app->voice_gated = false;
    if (!allowed) {
        dio_app_set_voice_paused(app);
        dio_app_post_state(
            app,
            app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
        return;
    }
    result = dio_voice_follow_up(app->voice);
    if (result == DIO_VOICE_OK) {
        app->follow_up_listening = true;
        app->listening_active = true;
        dio_app_post_state(app, DIO_UI_FOLLOW_UP);
    } else {
        dio_app_set_voice_paused(app);
        dio_metric(
            &app->paths,
            "follow_up_error",
            0u,
            (unsigned long)result);
        dio_app_post_state(app, DIO_UI_IDLE);
    }
}

static void dio_app_set_voice_paused(
    DioApp *app) {
    DioVoiceResult result;
    if (app->voice == NULL || !app->voice_ready) {
        return;
    }
    result = dio_voice_set_paused(
        app->voice,
        app->paused || app->voice_gated);
    if (result != DIO_VOICE_OK) {
        dio_metric(
            &app->paths,
            "voice_pause_error",
            0u,
            (unsigned long)result);
    }
}

static void dio_app_cancel(DioApp *app) {
    app->follow_up_allowed = false;
    app->agent_complete = false;
    app->follow_up_listening = false;
    app->listening_active = false;
    app->response_started_at = 0u;
    app->voice_gated = app->turn_active;
    dio_sentence_buffer_init(&app->sentences);
    app->stream_audio = false;
    if (app->turn_active && app->agent != NULL) {
        app->turn_output_suppressed = true;
        (void)dio_agent_cancel(app->agent);
    }
    if (app->voice != NULL) {
        (void)dio_voice_cancel(app->voice);
        if (!app->push_to_talk_pending) {
            dio_app_set_voice_paused(app);
        }
    }
}

static void dio_app_end_failed_turn(DioApp *app) {
    app->turn_active = false;
    app->turn_output_suppressed = false;
    app->agent_complete = false;
    app->follow_up_allowed = false;
    app->follow_up_listening = false;
    app->listening_active = false;
    app->voice_gated = false;
    app->turn_started_at = 0u;
    app->response_started_at = 0u;
    app->active_agent_turn_id = 0u;
    app->agent_audio_sample_rate = 0u;
    dio_sentence_buffer_init(&app->sentences);
    if (app->voice != NULL) {
        (void)dio_voice_cancel(app->voice);
    }
    dio_app_set_voice_paused(app);
}

static bool dio_app_recover_queue_overflow(
    DioApp *app,
    ULONGLONG now) {
    (void)InterlockedExchange(&app->accepting_events, 0);
    dio_app_cancel(app);
    dio_app_close_voice(app);
    dio_app_close_agent(app);
    (void)InterlockedExchange(&app->queue_overflow, 0);
    (void)InterlockedExchange(&app->accepting_events, 1);
    return dio_app_schedule_agent_restart(app, now);
}

static void dio_app_try_push_to_talk(DioApp *app) {
    DioVoiceResult result;
    if (!app->push_to_talk_pending ||
        app->voice == NULL ||
        !app->voice_ready ||
        app->turn_active) {
        return;
    }
    result = dio_voice_push_to_talk(app->voice);
    if (result == DIO_VOICE_OK) {
        app->push_to_talk_pending = false;
        app->follow_up_listening = false;
        app->listening_active = true;
        dio_app_post_state(app, DIO_UI_LISTENING);
    } else if (result != DIO_VOICE_BUSY) {
        app->push_to_talk_pending = false;
        dio_metric(
            &app->paths,
            "push_to_talk_error",
            0u,
            (unsigned long)result);
    }
}

static char *dio_app_wide_to_utf8(
    const wchar_t *text,
    size_t *length) {
    int required;
    char *utf8;

    *length = 0u;
    required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (required <= 1) {
        return NULL;
    }
    utf8 = (char *)malloc((size_t)required);
    if (utf8 == NULL ||
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            utf8,
            required,
            NULL,
            NULL) != required) {
        free(utf8);
        return NULL;
    }
    *length = (size_t)required - 1u;
    return utf8;
}

static void dio_app_poll_announcement(DioApp *app) {
    /*
     * ponytail: 200 ms polling is enough for one personal spool; add a
     * directory change handle only if measured latency or filesystem load
     * makes it necessary.
     */
    wchar_t receipt[MAX_PATH];
    char *utf8;
    size_t length;
    uint64_t id;

    if (!app->inbox_open ||
        !app->voice_ready ||
        app->paused ||
        app->listening_active ||
        app->voice_gated ||
        app->push_to_talk_pending ||
        app->turn_active ||
        app->agent_complete ||
        app->follow_up_listening ||
        app->speech_count != 0u ||
        app->announcement_receipt[0] != L'\0' ||
        !dio_announcement_take(
            &app->inbox,
            app->announcement_text,
            DIO_APP_ANNOUNCEMENT_CAP,
            receipt,
            _countof(receipt))) {
        return;
    }
    dio_app_post_wide(
        app,
        DIO_UI_EVENT_ANNOUNCEMENT,
        app->announcement_text);
    utf8 = dio_app_wide_to_utf8(
        app->announcement_text,
        &length);
    if (utf8 == NULL ||
        !dio_app_speak(
            app,
            DIO_APP_SPEECH_ANNOUNCEMENT,
            utf8,
            length,
            &id)) {
        free(utf8);
        dio_announcement_finish(receipt, false);
        return;
    }
    free(utf8);
    app->announcement_id = id;
    (void)wcsncpy_s(
        app->announcement_receipt,
        _countof(app->announcement_receipt),
        receipt,
        _TRUNCATE);
}

static void dio_app_open_inbox(DioApp *app) {
    if (app->inbox_open) {
        app->inbox_open = false;
    }
    app->inbox_open = dio_announcement_inbox_open(
        &app->inbox,
        app->settings.announcement_dir);
    if (!app->inbox_open) {
        dio_metric(
            &app->paths,
            "announcement_inbox_error",
            0u,
            GetLastError());
    }
}

static void dio_app_handle_settings(
    DioApp *app,
    const DioSettings *settings,
    const DioAgentProfile *profile,
    const wchar_t *api_key) {
    DioAgentProfile *copy = (DioAgentProfile *)calloc(1u, sizeof(*copy));
    if (copy == NULL || profile == NULL) {
        free(copy);
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            L"Could not apply provider settings.");
        return;
    }
    dio_agent_profile_init(copy);
    if (copy->system_prompt == NULL ||
        !dio_agent_profile_copy(copy, profile)) {
        dio_agent_profile_free(copy);
        free(copy);
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            L"Could not apply provider settings.");
        return;
    }
    dio_app_cancel(app);
    dio_app_close_discovery(app);
    dio_app_close_voice(app);
    dio_app_close_agent(app);
    dio_tts_server_close(app->tts_server);
    app->tts_server = NULL;
    app->settings = *settings;
    dio_agent_profile_free(&app->profile);
    app->profile = *copy;
    SecureZeroMemory(copy, sizeof(*copy));
    free(copy);
    SecureZeroMemory(
        app->provider_api_key,
        sizeof(app->provider_api_key));
    if (api_key != NULL) {
        (void)wcsncpy_s(
            app->provider_api_key,
            _countof(app->provider_api_key),
            api_key,
            _TRUNCATE);
    }
    app->agent_restart_at = 0u;
    app->agent_restart_attempts = 0u;
    app->voice_restart_at = 0u;
    app->voice_restart_attempts = 0u;
    app->vault_required_emitted = false;
    dio_app_open_inbox(app);
    if (dio_agent_profile_configured(&app->profile)) {
        dio_app_post_state(app, DIO_UI_LOADING);
        dio_app_open_agent(app);
    } else {
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_PROVIDER_REQUIRED,
            dio_app_localized(
                app,
                L"Model provider is not configured. Open Settings to configure one.",
                L"ارائه‌دهندهٔ مدل تنظیم نشده است؛ از تنظیمات آن را پیکربندی کنید."));
    }
}

static void dio_app_handle_discovery(
    DioApp *app,
    const DioUiCommand *command) {
    dio_app_close_discovery(app);
    app->discovery_generation = command->generation;
    app->discovery_request_id = command->generation;
    app->discovery_deadline =
        GetTickCount64() + DIO_MODEL_DISCOVERY_TIMEOUT_MS;
    (void)wcsncpy_s(
        app->discovery_endpoint,
        _countof(app->discovery_endpoint),
        command->endpoint,
        _TRUNCATE);
    (void)wcsncpy_s(
        app->discovery_api_key,
        _countof(app->discovery_api_key),
        command->api_key,
        _TRUNCATE);
    if (app->discovery_generation != 0u &&
        app->discovery_endpoint[0] != L'\0') {
        dio_app_open_discovery(app);
    }
}

static void dio_app_handle_ui(
    DioApp *app,
    const DioUiCommand *command) {
    switch (command->kind) {
    case DIO_UI_COMMAND_SET_PAUSED:
        app->paused = command->enabled;
        app->push_to_talk_pending = false;
        dio_app_set_voice_paused(app);
        dio_app_post_state(
            app,
            command->enabled ? DIO_UI_MUTED : DIO_UI_IDLE);
        break;
    case DIO_UI_COMMAND_PUSH_TO_TALK:
        if (!dio_agent_profile_configured(&app->profile)) {
            dio_app_post_wide(
                app,
                DIO_UI_EVENT_PROVIDER_REQUIRED,
                dio_app_localized(
                    app,
                    L"Model provider is not configured. Open Settings to configure one.",
                    L"\u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647\u0654 \u0645\u062f\u0644 \u062a\u0646\u0638\u06cc\u0645 \u0646\u0634\u062f\u0647 \u0627\u0633\u062a\u061b \u0627\u0632 \u062a\u0646\u0638\u06cc\u0645\u0627\u062a \u0622\u0646 \u0631\u0627 \u067e\u06cc\u06a9\u0631\u0628\u0646\u062f\u06cc \u06a9\u0646\u06cc\u062f."));
            break;
        }
        if (!dio_agent_profile_session_secrets_ready(
                &app->profile,
                app->provider_api_key)) {
            dio_app_require_vault(app);
            break;
        }
        app->paused = false;
        app->push_to_talk_pending = true;
        dio_app_cancel(app);
        dio_app_try_push_to_talk(app);
        break;
    case DIO_UI_COMMAND_CANCEL:
        app->push_to_talk_pending = false;
        dio_app_cancel(app);
        dio_app_post_state(
            app,
            app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
        break;
    case DIO_UI_COMMAND_SETTINGS_CHANGED:
        dio_app_handle_settings(
            app,
            &command->settings,
            command->profile,
            command->api_key);
        break;
    case DIO_UI_COMMAND_DISCOVER_MODELS:
        dio_app_handle_discovery(app, command);
        break;
    case DIO_UI_COMMAND_TOOL_APPROVAL:
        if (app->agent != NULL) {
            (void)dio_agent_approve_tool(
                app->agent,
                command->turn_id,
                command->request_id,
                command->tool_decision == DIO_UI_TOOL_ALWAYS
                    ? DIO_AGENT_TOOL_ALWAYS
                    : command->tool_decision == DIO_UI_TOOL_ONCE
                        ? DIO_AGENT_TOOL_ONCE
                        : DIO_AGENT_TOOL_DENY);
        }
        break;
    case DIO_UI_COMMAND_EXIT:
        (void)SetEvent(app->stop_event);
        break;
    default:
        break;
    }
}

static void dio_app_handle_agent(
    DioApp *app,
    const DioAppAgentEvent *event) {
    if (event->discovery) {
        if (event->generation != app->discovery_generation) {
            return;
        }
        if (event->type == DIO_AGENT_EVENT_READY) {
            if (app->discovery_agent == NULL ||
                dio_agent_list_models(
                    app->discovery_agent,
                    app->discovery_request_id) != DIO_AGENT_OK) {
                (void)dio_ui_post_models(
                    app->ui,
                    app->discovery_generation,
                    L"!error");
                dio_app_close_discovery(app);
            }
        } else if (event->type == DIO_AGENT_EVENT_MODELS ||
                   event->type == DIO_AGENT_EVENT_MODELS_ERROR) {
            dio_app_close_discovery(app);
        } else if (event->type == DIO_AGENT_EVENT_ERROR) {
            (void)dio_ui_post_models(
                app->ui,
                app->discovery_generation,
                L"!error");
            dio_app_close_discovery(app);
        }
        return;
    }
    if (event->generation != app->agent_generation) {
        return;
    }
    switch (event->type) {
    case DIO_AGENT_EVENT_READY:
        app->agent_ready = event->provider_configured;
        app->agent_started_at = 0u;
        if (!event->provider_configured) {
            dio_app_post_wide(
                app,
                DIO_UI_EVENT_PROVIDER_REQUIRED,
                dio_app_localized(
                    app,
                    L"Model provider is not configured. Open Settings to configure one.",
                    L"ارائه‌دهندهٔ مدل تنظیم نشده است؛ از تنظیمات آن را پیکربندی کنید."));
            break;
        }
        dio_app_post_chip(
            app,
            DIO_CHIP_AGENT,
            DIO_APP_AGENT_NAME);
        dio_app_open_tts_server(app);
        dio_app_open_voice(app);
        break;
    case DIO_AGENT_EVENT_MODELS:
    case DIO_AGENT_EVENT_MODELS_ERROR:
        break;
    case DIO_AGENT_EVENT_MCP_STATUS: {
        DioUiEvent status;
        ZeroMemory(&status, sizeof(status));
        status.kind = DIO_UI_EVENT_MCP_STATUS;
        (void)swprintf_s(
            status.text,
            _countof(status.text),
            app->settings.persian
                ? L"MCP: %u \u0627\u0632 %u \u0633\u0631\u0648\u0631\u060c %u \u0627\u0628\u0632\u0627\u0631"
                : L"MCP: %u of %u servers, %u tools",
            event->mcp_available,
            event->mcp_configured,
            event->mcp_tools);
        (void)dio_ui_post(app->ui, &status);
        break;
    }
    case DIO_AGENT_EVENT_TOOL_APPROVAL_REQUIRED: {
        DioUiEvent approval;
        int arguments;
        ZeroMemory(&approval, sizeof(approval));
        approval.kind = DIO_UI_EVENT_TOOL_APPROVAL_REQUIRED;
        approval.turn_id = event->turn_id;
        approval.request_id = event->request_id;
        approval.truncated = event->truncated;
        (void)dio_utf8_to_wide(
            event->server,
            approval.server,
            _countof(approval.server));
        (void)dio_utf8_to_wide(
            event->tool,
            approval.tool,
            _countof(approval.tool));
        arguments = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            event->text,
            (int)event->text_length,
            approval.text,
            (int)_countof(approval.text) - 1);
        if (arguments <= 0) {
            approval.truncated = true;
            (void)wcscpy_s(
                approval.text,
                _countof(approval.text),
                L"Arguments are too large to display safely.");
        } else {
            approval.text[arguments] = L'\0';
        }
        if (!dio_ui_post(app->ui, &approval) && app->agent != NULL) {
            (void)dio_agent_approve_tool(
                app->agent,
                event->turn_id,
                event->request_id,
                DIO_AGENT_TOOL_DENY);
        }
        break;
    }
    case DIO_AGENT_EVENT_ACCEPTED:
        app->active_agent_turn_id = event->turn_id;
        app->agent_audio_sample_rate = event->sample_rate;
        if (app->turn_output_suppressed) {
            break;
        }
        dio_app_post_chip(
            app,
            DIO_CHIP_ACK,
            dio_app_localized(
                app,
                L"Received",
                L"\u062f\u0631\u06cc\u0627\u0641\u062a \u0634\u062f"));
        dio_app_post_state(app, DIO_UI_THINKING);
        break;
    case DIO_AGENT_EVENT_TEXT_DELTA:
        if (!app->turn_output_suppressed &&
            event->text_length != 0u) {
            if (!app->first_delta_measured &&
                app->response_started_at != 0u) {
                app->first_delta_measured = true;
                dio_metric(
                    &app->paths,
                    "agent_first_delta",
                    GetTickCount64() -
                        app->response_started_at,
                    0u);
            }
            dio_app_post_utf8(
                app,
                DIO_UI_EVENT_ASSISTANT_DELTA,
                event->text,
                event->text_length);
            if (app->agent_audio_sample_rate != 0u
                    ? (!app->stream_audio &&
                       !dio_sentence_buffer_store(
                           &app->sentences,
                           event->text,
                           event->text_length))
                    : !dio_sentence_buffer_append(
                          &app->sentences,
                          event->text,
                          event->text_length,
                          dio_app_speak_sentence,
                          app)) {
                app->follow_up_allowed = false;
                if (app->agent != NULL) {
                    (void)dio_agent_cancel(app->agent);
                }
            }
        }
        break;
    case DIO_AGENT_EVENT_AUDIO_PCM16:
        if (!app->turn_output_suppressed &&
            !dio_app_stream_pcm(app, event)) {
            dio_metric(
                &app->paths,
                "pcm_stream_error",
                0u,
                ERROR_BUFFER_OVERFLOW);
            dio_app_cancel(app);
        }
        break;
    case DIO_AGENT_EVENT_PROGRESS:
        if (app->turn_output_suppressed) {
            break;
        }
        dio_app_post_chip(
            app,
            DIO_CHIP_AGENT,
            dio_app_localized(
                app,
                L"Dio Harness is working",
                L"Dio Harness \u062f\u0631 \u062d\u0627\u0644 \u06a9\u0627\u0631"));
        break;
    case DIO_AGENT_EVENT_COMPLETE:
    {
        const bool suppressed = app->turn_output_suppressed;
        if (dio_app_is_conversation_smoke(app)) {
            if (suppressed ||
                event->cancelled ||
                app->smoke_agent_turns >=
                    app->smoke_next_wav) {
                dio_app_finish_conversation_smoke(app, 21);
            } else {
                app->smoke_agent_turns += 1u;
            }
        }
        app->agent_restart_attempts = 0u;
        app->turn_output_suppressed = false;
        app->turn_active = false;
        app->agent_complete = !suppressed;
        app->follow_up_allowed =
            app->follow_up_allowed &&
            !suppressed &&
            !event->cancelled;
        if (suppressed || event->cancelled) {
            dio_sentence_buffer_init(&app->sentences);
            if (app->stream_audio && app->voice != NULL) {
                (void)dio_voice_cancel(app->voice);
            }
        } else if (app->stream_audio) {
            if (dio_voice_stream_finish(
                    app->voice,
                    app->stream_speech_id) != DIO_VOICE_OK) {
                app->follow_up_allowed = false;
                (void)dio_voice_cancel(app->voice);
            }
        } else if (!dio_sentence_buffer_flush(
                       &app->sentences,
                       dio_app_speak_sentence,
                       app)) {
            app->follow_up_allowed = false;
        }
        if (app->speech_count == 0u) {
            app->response_started_at = 0u;
        }
        if (app->turn_started_at != 0u) {
            dio_metric(
                &app->paths,
                "agent_turn",
                GetTickCount64() - app->turn_started_at,
                event->cancelled ? ERROR_CANCELLED : 0u);
            app->turn_started_at = 0u;
        }
        app->active_agent_turn_id = 0u;
        app->agent_audio_sample_rate = 0u;
        if (suppressed) {
            app->voice_gated = false;
            dio_app_set_voice_paused(app);
            dio_app_post_state(
                app,
                app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
            dio_app_try_push_to_talk(app);
        } else {
            dio_app_maybe_follow_up(app);
        }
        break;
    }
    case DIO_AGENT_EVENT_ERROR: {
        const bool cancellation_rejected =
            event->system_error == ERROR_SUCCESS &&
            app->turn_output_suppressed;
        dio_app_finish_conversation_smoke(app, 21);
        dio_metric(
            &app->paths,
            "agent_error",
            app->turn_started_at != 0u
                ? GetTickCount64() - app->turn_started_at
                : 0u,
            event->system_error);
        if (event->system_error == ERROR_SUCCESS) {
            if (!cancellation_rejected) {
                dio_app_end_failed_turn(app);
                dio_app_try_push_to_talk(app);
            }
            if (!cancellation_rejected && event->http_status != 0u &&
                event->text_length != 0u) {
                dio_app_post_utf8(
                    app,
                    DIO_UI_EVENT_ERROR,
                    event->text,
                    event->text_length);
                break;
            }
            dio_app_post_wide(
                app,
                DIO_UI_EVENT_ERROR,
                dio_app_localized(
                    app,
                    cancellation_rejected
                        ? DIO_APP_AGENT_NAME
                          L" could not cancel the current request."
                        : DIO_APP_AGENT_NAME
                          L" could not complete the request.",
                    cancellation_rejected
                        ? DIO_APP_AGENT_NAME
                          L" \u0646\u062a\u0648\u0627\u0646\u0633\u062a "
                          L"\u062f\u0631\u062e\u0648\u0627\u0633\u062a "
                          L"\u0641\u0639\u0644\u06cc \u0631\u0627 "
                          L"\u0644\u063a\u0648 \u06a9\u0646\u062f."
                        : DIO_APP_AGENT_NAME
                          L" \u0646\u062a\u0648\u0627\u0646\u0633\u062a "
                          L"\u062f\u0631\u062e\u0648\u0627\u0633\u062a "
                          L"\u0631\u0627 \u06a9\u0627\u0645\u0644 "
                          L"\u06a9\u0646\u062f."));
            break;
        }
        dio_app_close_voice(app);
        dio_app_close_agent(app);
        if (!dio_app_schedule_agent_restart(
                app,
                GetTickCount64())) {
            dio_metric(
                &app->paths,
                "agent_restart_exhausted",
                0u,
                ERROR_RETRY);
        }
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                DIO_APP_AGENT_NAME
                L" disconnected before the turn completed. The prompt "
                L"was not sent again.",
                L"\u0627\u0631\u062a\u0628\u0627\u0637 "
                DIO_APP_AGENT_NAME
                L" \u067e\u06cc\u0634 "
                L"\u0627\u0632 \u067e\u0627\u06cc\u0627\u0646 \u0646\u0648\u0628\u062a "
                L"\u0642\u0637\u0639 \u0634\u062f. \u062f\u0631\u062e\u0648\u0627\u0633\u062a "
                L"\u062f\u0648\u0628\u0627\u0631\u0647 \u0641\u0631\u0633\u062a\u0627\u062f\u0647 "
                L"\u0646\u0634\u062f."));
        break;
    }
    default:
        break;
    }
}

static void dio_app_submit_transcript(
    DioApp *app,
    const DioAppVoiceEvent *event) {
    DioAgentResult result;
    ULONGLONG submitted_at;

    if (event->truncated || event->text_length == 0u) {
        dio_metric(
            &app->paths,
            "transcript_rejected",
            0u,
            event->truncated ? ERROR_BUFFER_OVERFLOW : ERROR_NO_DATA);
        return;
    }
    if (app->agent == NULL || !app->agent_ready) {
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                DIO_APP_AGENT_NAME L" is not ready.",
                DIO_APP_AGENT_NAME
                L" \u0622\u0645\u0627\u062f\u0647 \u0646\u06cc\u0633\u062a."));
        return;
    }
    submitted_at = GetTickCount64();
    result = dio_agent_submit(
        app->agent,
        event->text,
        event->text_length);
    if (result != DIO_AGENT_OK) {
        dio_app_finish_conversation_smoke(app, 21);
        dio_metric(
            &app->paths,
            "prompt_submit_error",
            0u,
            (unsigned long)result);
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                L"The spoken request could not be sent to "
                DIO_APP_AGENT_NAME L".",
                L"\u062f\u0631\u062e\u0648\u0627\u0633\u062a "
                L"\u0635\u0648\u062a\u06cc \u0628\u0647 "
                DIO_APP_AGENT_NAME L" "
                L"\u0627\u0631\u0633\u0627\u0644 \u0646\u0634\u062f."));
        return;
    }
    app->turn_active = true;
    app->turn_output_suppressed = false;
    app->agent_complete = false;
    app->follow_up_allowed =
        app->settings.follow_up_ms != 0u;
    app->follow_up_listening = false;
    app->turn_started_at = submitted_at;
    app->response_started_at = app->turn_started_at;
    app->first_delta_measured = false;
    app->active_agent_turn_id = 0u;
    app->agent_audio_sample_rate = 0u;
    app->stream_speech_id = 0u;
    app->stream_audio = false;
    dio_sentence_buffer_init(&app->sentences);
    app->voice_gated = true;
    dio_app_set_voice_paused(app);
    dio_app_post_state(app, DIO_UI_THINKING);
    {
        unsigned int latency_ms = UINT_MAX;
        const DioVoiceResult cue_result =
            dio_voice_play_processing_earcon(
            app->voice,
            submitted_at,
            &latency_ms);
        dio_metric(
            &app->paths,
            "processing_earcon",
            cue_result == DIO_VOICE_OK ? latency_ms : 0u,
            (unsigned long)cue_result);
    }
}

static void dio_app_handle_speech_complete(
    DioApp *app,
    const DioAppVoiceEvent *event) {
    DioAppSpeechKind kind;
    const bool streamed =
        event->utterance_id == app->stream_speech_id;

    if (!dio_app_untrack_speech(
            app,
            event->utterance_id,
            &kind)) {
        return;
    }
    if (dio_app_is_conversation_smoke(app) &&
        kind == DIO_APP_SPEECH_AGENT) {
        if (event->speech_outcome !=
            DIO_VOICE_SPEECH_SUCCEEDED) {
            dio_app_finish_conversation_smoke(app, 22);
        } else {
            app->smoke_tts_turn =
                app->smoke_next_wav;
        }
    }
    if (kind == DIO_APP_SPEECH_ANNOUNCEMENT &&
        event->utterance_id == app->announcement_id &&
        app->announcement_receipt[0] != L'\0') {
        dio_announcement_finish(
            app->announcement_receipt,
            event->speech_outcome ==
                DIO_VOICE_SPEECH_SUCCEEDED);
        app->announcement_receipt[0] = L'\0';
        app->announcement_id = 0u;
    }
    if (event->speech_outcome != DIO_VOICE_SPEECH_SUCCEEDED) {
        app->follow_up_allowed = false;
        dio_metric(
            &app->paths,
            "speech_terminal",
            0u,
            (unsigned long)event->speech_outcome);
    }
    if (event->utterance_id == app->stream_speech_id) {
        app->stream_speech_id = 0u;
        app->stream_audio = false;
    }
    if (streamed &&
        event->speech_outcome !=
            DIO_VOICE_SPEECH_SUCCEEDED &&
        app->turn_active) {
        dio_app_cancel(app);
    }
    dio_app_maybe_follow_up(app);
    dio_app_try_push_to_talk(app);
    if (!app->turn_active &&
        !app->agent_complete &&
        !app->follow_up_listening &&
        !app->listening_active &&
        app->speech_count == 0u) {
        dio_app_post_state(
            app,
            app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
    }
}

static void dio_app_handle_voice(
    DioApp *app,
    const DioAppVoiceEvent *event) {
    if (event->generation != app->voice_generation) {
        return;
    }
    if (event->dropped_frames > app->last_dropped_frames) {
        const uint64_t dropped =
            event->dropped_frames - app->last_dropped_frames;
        app->last_dropped_frames = event->dropped_frames;
        dio_metric(
            &app->paths,
            "capture_drop",
            0u,
            (unsigned long)(
                dropped > ULONG_MAX
                    ? ULONG_MAX
                    : dropped));
        dio_app_finish_conversation_smoke(app, 20);
    }
    if ((app->voice_gated ||
         app->turn_active ||
         app->agent_complete ||
         app->speech_count != 0u) &&
        (event->type == DIO_VOICE_EVENT_WAKE ||
         event->type == DIO_VOICE_EVENT_LISTENING ||
         event->type == DIO_VOICE_EVENT_TRANSCRIPT)) {
        return;
    }
    switch (event->type) {
    case DIO_VOICE_EVENT_READY:
        app->voice_ready = true;
        dio_app_set_voice_paused(app);
        dio_metric(
            &app->paths,
            "model_ready",
            app->model_started_at != 0u
                ? GetTickCount64() - app->model_started_at
                : 0u,
            0u);
        dio_app_post_state(
            app,
            app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
        if (dio_app_is_conversation_smoke(app) &&
            app->smoke_next_wav == 0u) {
            app->smoke_next_wav = 1u;
            dio_app_feed_conversation_smoke(
                app,
                app->smoke_wavs[0]);
        }
        break;
    case DIO_VOICE_EVENT_WAKE: {
        DioUiEvent clear;
        app->voice_restart_attempts = 0u;
        ZeroMemory(&clear, sizeof(clear));
        clear.kind = DIO_UI_EVENT_CLEAR;
        (void)dio_ui_post(app->ui, &clear);
        dio_metric(
            &app->paths,
            "wake_to_earcon",
            event->latency_ms,
            event->latency_ms <= 600u ? 0u : WAIT_TIMEOUT);
        app->follow_up_listening = false;
        app->listening_active = true;
        dio_app_post_state(app, DIO_UI_LISTENING);
        break;
    }
    case DIO_VOICE_EVENT_LISTENING:
        app->listening_active = true;
        dio_app_post_state(
            app,
            app->follow_up_listening
                ? DIO_UI_FOLLOW_UP
                : DIO_UI_LISTENING);
        if (dio_app_is_conversation_smoke(app) &&
            app->follow_up_listening) {
            if (app->smoke_next_wav == 1u &&
                app->smoke_agent_turns == 1u &&
                app->smoke_tts_turn == 1u) {
                app->smoke_next_wav = 2u;
                dio_app_feed_conversation_smoke(
                    app,
                    app->smoke_wavs[1]);
            } else if (
                app->smoke_next_wav == 2u &&
                app->smoke_agent_turns == 2u &&
                app->smoke_tts_turn == 2u &&
                app->smoke_silent_started_at == 0u) {
                app->smoke_silent_started_at =
                    GetTickCount64();
            }
        }
        break;
    case DIO_VOICE_EVENT_LEVEL:
        dio_app_post_level(app, event->level);
        break;
    case DIO_VOICE_EVENT_TRANSCRIPT:
        if (!event->truncated) {
            dio_app_post_utf8(
                app,
                DIO_UI_EVENT_USER_TEXT,
                event->text,
                event->text_length);
        }
        if (event->transcript_is_final) {
            if (app->follow_up_listening) {
                dio_metric(
                    &app->paths,
                    "follow_up_terminal",
                    0u,
                    0u);
            }
            app->listening_active = false;
            dio_app_submit_transcript(app, event);
        }
        break;
    case DIO_VOICE_EVENT_SPEAKING:
        if (event->utterance_id ==
                app->stream_speech_id &&
            !app->stream_audio) {
            break;
        }
        app->listening_active = false;
        if (app->response_started_at != 0u &&
            dio_app_is_agent_speech(
                app,
                event->utterance_id)) {
            const ULONGLONG elapsed =
                GetTickCount64() -
                app->response_started_at;
            if (event->utterance_id ==
                app->stream_speech_id) {
                dio_metric(
                    &app->paths,
                    "agent_first_audio",
                    elapsed,
                    0u);
            } else {
                dio_metric(
                    &app->paths,
                    "agent_first_tts_start",
                    elapsed,
                    0u);
            }
            app->response_started_at = 0u;
        }
        dio_app_post_chip(
            app,
            DIO_CHIP_TTS,
            dio_app_localized(
                app,
                L"Voice",
                L"\u0635\u062f\u0627"));
        dio_app_post_state(app, DIO_UI_SPEAKING);
        break;
    case DIO_VOICE_EVENT_SPEECH_COMPLETE:
        dio_app_handle_speech_complete(app, event);
        break;
    case DIO_VOICE_EVENT_IDLE:
        if (dio_app_is_conversation_smoke(app) &&
            app->follow_up_listening &&
            app->smoke_silent_started_at != 0u) {
            const ULONGLONG elapsed =
                GetTickCount64() -
                app->smoke_silent_started_at;
            const bool passed =
                app->smoke_next_wav == 2u &&
                app->smoke_agent_turns == 2u &&
                app->smoke_tts_turn == 2u &&
                app->last_dropped_frames == 0u &&
                elapsed + DIO_APP_POLL_MS >=
                    app->settings.follow_up_ms &&
                elapsed <=
                    app->settings.follow_up_ms + 1000u;
            dio_app_finish_conversation_smoke(
                app,
                passed ? 0 : 24);
        }
        if (app->follow_up_listening) {
            dio_metric(
                &app->paths,
                "follow_up_idle",
                0u,
                0u);
        }
        app->follow_up_listening = false;
        app->listening_active = false;
        dio_app_try_push_to_talk(app);
        if (!app->turn_active &&
            !app->agent_complete &&
            app->speech_count == 0u &&
            !app->push_to_talk_pending) {
            dio_app_post_state(
                app,
                app->paused ? DIO_UI_MUTED : DIO_UI_IDLE);
        }
        break;
    case DIO_VOICE_EVENT_ERROR:
        app->listening_active = false;
        dio_app_finish_conversation_smoke(app, 23);
        dio_metric(
            &app->paths,
            "voice_error",
            0u,
            event->system_error);
        dio_app_cancel(app);
        dio_app_close_voice(app);
        if (!dio_app_schedule_voice_restart(
                app,
                GetTickCount64())) {
            dio_metric(
                &app->paths,
                "voice_restart_exhausted",
                0u,
                ERROR_RETRY);
        }
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_ERROR,
            dio_app_localized(
                app,
                L"The voice pipeline reported an error.",
                L"\u062f\u0631 \u0645\u0633\u06cc\u0631 "
                L"\u067e\u0631\u062f\u0627\u0632\u0634 \u0635\u062f\u0627 "
                L"\u062e\u0637\u0627 \u0631\u062e \u062f\u0627\u062f."));
        break;
    default:
        break;
    }
}

static void dio_app_handle_event(
    DioApp *app,
    const DioAppEvent *event) {
    switch (event->kind) {
    case DIO_APP_EVENT_UI:
        dio_app_handle_ui(app, &event->data.ui);
        break;
    case DIO_APP_EVENT_AGENT:
        dio_app_handle_agent(app, &event->data.agent);
        break;
    case DIO_APP_EVENT_VOICE:
        dio_app_handle_voice(app, &event->data.voice);
        break;
    default:
        break;
    }
}

static void dio_app_release_event(DioAppEvent *event) {
    if (event == NULL) {
        return;
    }
    if (event->kind == DIO_APP_EVENT_UI) {
        if (event->data.ui.kind == DIO_UI_COMMAND_SETTINGS_CHANGED &&
            event->data.ui.profile != NULL) {
            DioAgentProfile *profile =
                (DioAgentProfile *)event->data.ui.profile;
            dio_agent_profile_free(profile);
            free(profile);
        }
        SecureZeroMemory(
            event->data.ui.api_key,
            sizeof(event->data.ui.api_key));
    }
    SecureZeroMemory(event, sizeof(*event));
}

static DWORD WINAPI dio_app_worker(
    void *context) {
    DioApp *app = (DioApp *)context;
    HANDLE waits[2] = {
        app->stop_event,
        app->queue_event
    };
    bool stopping = false;
    const HRESULT com = CoInitializeEx(
        NULL,
        COINIT_MULTITHREADED);

    dio_sentence_buffer_init(&app->sentences);
    if (!dio_app_is_conversation_smoke(app)) {
        dio_app_open_inbox(app);
    }
    if (dio_agent_profile_configured(&app->profile)) {
        dio_app_open_agent(app);
    } else {
        dio_app_post_wide(
            app,
            DIO_UI_EVENT_PROVIDER_REQUIRED,
            dio_app_localized(
                app,
                L"Model provider is not configured. Open Settings to configure one.",
                L"ارائه‌دهندهٔ مدل تنظیم نشده است؛ از تنظیمات آن را پیکربندی کنید."));
    }
    while (!stopping) {
        const DWORD wait = WaitForMultipleObjects(
            2u,
            waits,
            FALSE,
            DIO_APP_POLL_MS);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (InterlockedExchange(
                &app->queue_overflow,
                0) != 0) {
            dio_app_finish_conversation_smoke(app, 26);
            dio_metric(
                &app->paths,
                "controller_queue_overflow",
                0u,
                ERROR_BUFFER_OVERFLOW);
            if (!dio_app_recover_queue_overflow(
                    app,
                    GetTickCount64())) {
                dio_metric(
                    &app->paths,
                    "agent_restart_exhausted",
                    0u,
                    ERROR_RETRY);
            }
            dio_app_post_wide(
                app,
                DIO_UI_EVENT_ERROR,
                dio_app_localized(
                    app,
                    L"The controller event queue overflowed.",
                    L"\u0635\u0641 \u0631\u0648\u06cc\u062f\u0627\u062f\u0647\u0627\u06cc "
                    L"\u06a9\u0646\u062a\u0631\u0644\u0631 \u067e\u0631 \u0634\u062f."));
        }
        for (;;) {
            DioAppEvent event;
            if (!dio_app_dequeue(app, &event)) {
                break;
            }
            dio_app_handle_event(app, &event);
            dio_app_release_event(&event);
            if (WaitForSingleObject(
                    app->stop_event,
                    0u) == WAIT_OBJECT_0) {
                stopping = true;
                break;
            }
        }
        if (!stopping) {
            if (dio_app_is_conversation_smoke(app) &&
                GetTickCount64() >= app->smoke_deadline) {
                dio_app_finish_conversation_smoke(app, 25);
            }
            dio_app_maybe_timeout_agent(app);
            dio_app_maybe_timeout_discovery(app);
            dio_app_maybe_restart_agent(app);
            dio_app_maybe_restart_voice(app);
            if (!dio_app_is_conversation_smoke(app)) {
                dio_app_poll_announcement(app);
            }
        }
    }

    (void)InterlockedExchange(&app->accepting_events, 0);
    if (app->inbox_open) {
        app->inbox_open = false;
    }
    dio_app_close_discovery(app);
    dio_app_close_agent(app);
    dio_app_close_voice(app);
    dio_tts_server_close(app->tts_server);
    app->tts_server = NULL;
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0u;
}

static bool dio_app_initialize(
    DioApp *app,
    const DioPaths *paths,
    const DioSettings *settings,
    const DioAgentProfile *profile) {
    ZeroMemory(app, sizeof(*app));
    if (profile == NULL) {
        return false;
    }
    app->paths = *paths;
    app->settings = *settings;
    dio_agent_profile_init(&app->profile);
    if (app->profile.system_prompt == NULL ||
        !dio_agent_profile_copy(&app->profile, profile)) {
        dio_agent_profile_free(&app->profile);
        return false;
    }
    app->events = (DioAppEvent *)calloc(
        DIO_APP_QUEUE_CAP,
        sizeof(app->events[0]));
    app->announcement_text = (wchar_t *)calloc(
        DIO_APP_ANNOUNCEMENT_CAP,
        sizeof(app->announcement_text[0]));
    if (app->events == NULL ||
        app->announcement_text == NULL) {
        free(app->announcement_text);
        app->announcement_text = NULL;
        free(app->events);
        app->events = NULL;
        dio_agent_profile_free(&app->profile);
        return false;
    }
    if (!InitializeCriticalSectionEx(
            &app->queue_lock,
            4000u,
            0u)) {
        free(app->events);
        app->events = NULL;
        free(app->announcement_text);
        app->announcement_text = NULL;
        dio_agent_profile_free(&app->profile);
        return false;
    }
    app->queue_lock_ready = true;
    app->queue_event = CreateEventW(
        NULL,
        FALSE,
        FALSE,
        NULL);
    app->stop_event = CreateEventW(
        NULL,
        TRUE,
        FALSE,
        NULL);
    if (app->queue_event == NULL || app->stop_event == NULL) {
        if (app->queue_event != NULL) {
            CloseHandle(app->queue_event);
            app->queue_event = NULL;
        }
        if (app->stop_event != NULL) {
            CloseHandle(app->stop_event);
            app->stop_event = NULL;
        }
        DeleteCriticalSection(&app->queue_lock);
        app->queue_lock_ready = false;
        free(app->events);
        app->events = NULL;
        free(app->announcement_text);
        app->announcement_text = NULL;
        dio_agent_profile_free(&app->profile);
        return false;
    }
    app->provider_api_key_excluded_from_wer = SUCCEEDED(
        WerRegisterExcludedMemoryBlock(
            app->provider_api_key,
            (DWORD)sizeof(app->provider_api_key)));
    for (size_t index = 0u; index < DIO_AGENT_MCP_MAX; ++index) {
        app->mcp_secret_excluded_from_wer[index] = SUCCEEDED(
            WerRegisterExcludedMemoryBlock(
                app->profile.mcp_servers[index].secret_value,
                (DWORD)sizeof(
                    app->profile.mcp_servers[index].secret_value)));
    }
    (void)InterlockedExchange(&app->accepting_events, 1);
    return true;
}

static void dio_app_release(DioApp *app) {
    if (app->worker != NULL) {
        (void)SetEvent(app->stop_event);
        (void)WaitForSingleObject(app->worker, INFINITE);
        CloseHandle(app->worker);
        app->worker = NULL;
    }
    if (app->queue_event != NULL) {
        CloseHandle(app->queue_event);
    }
    if (app->stop_event != NULL) {
        CloseHandle(app->stop_event);
    }
    if (app->queue_lock_ready) {
        for (;;) {
            DioAppEvent event;
            if (!dio_app_dequeue(app, &event)) {
                break;
            }
            dio_app_release_event(&event);
        }
        DeleteCriticalSection(&app->queue_lock);
    }
    for (size_t index = 0u; index < DIO_AGENT_MCP_MAX; ++index) {
        if (app->mcp_secret_excluded_from_wer[index]) {
            (void)WerUnregisterExcludedMemoryBlock(
                app->profile.mcp_servers[index].secret_value);
        }
    }
    if (app->provider_api_key_excluded_from_wer) {
        (void)WerUnregisterExcludedMemoryBlock(app->provider_api_key);
    }
    dio_agent_profile_free(&app->profile);
    SecureZeroMemory(
        app->provider_api_key,
        sizeof(app->provider_api_key));
    SecureZeroMemory(
        app->discovery_api_key,
        sizeof(app->discovery_api_key));
    free(app->events);
    app->events = NULL;
    free(app->announcement_text);
    app->announcement_text = NULL;
}

static bool dio_agent_smoke_reply_matches(
    const char *reply,
    size_t length) {
    static const char expected[] = "DIO_AGENT_SMOKE_OK";
    size_t begin = 0u;
    size_t end = length;

    while (begin < end &&
           (reply[begin] == ' ' ||
            reply[begin] == '\t' ||
            reply[begin] == '\r' ||
            reply[begin] == '\n')) {
        ++begin;
    }
    while (end > begin &&
           (reply[end - 1u] == ' ' ||
            reply[end - 1u] == '\t' ||
            reply[end - 1u] == '\r' ||
            reply[end - 1u] == '\n')) {
        --end;
    }
    return
        end - begin == sizeof(expected) - 1u &&
        memcmp(
            reply + begin,
            expected,
            sizeof(expected) - 1u) == 0;
}

static void dio_agent_smoke_callback(
    void *context,
    const DioAgentEvent *event) {
    DioAgentSmoke *smoke = (DioAgentSmoke *)context;
    if (smoke == NULL || event == NULL) {
        return;
    }
    EnterCriticalSection(&smoke->lock);
    if (event->type == DIO_AGENT_EVENT_READY) {
        smoke->ready = true;
    } else if (event->type == DIO_AGENT_EVENT_TEXT_DELTA) {
        if (event->text != NULL &&
            event->text_length <=
                sizeof(smoke->reply) -
                smoke->reply_length) {
            memcpy(
                smoke->reply + smoke->reply_length,
                event->text,
                event->text_length);
            smoke->reply_length += event->text_length;
        } else {
            smoke->reply_overflow = true;
        }
    } else if (event->type == DIO_AGENT_EVENT_COMPLETE) {
        if (event->text != NULL &&
            event->text_length <= sizeof(smoke->reply)) {
            memcpy(
                smoke->reply,
                event->text,
                event->text_length);
            smoke->reply_length = event->text_length;
            smoke->reply_overflow = false;
        }
        smoke->finished = true;
        smoke->matched =
            !event->cancelled &&
            !smoke->reply_overflow &&
            dio_agent_smoke_reply_matches(
                smoke->reply,
                smoke->reply_length);
    } else if (event->type == DIO_AGENT_EVENT_ERROR) {
        smoke->failed = true;
    }
    LeaveCriticalSection(&smoke->lock);
    (void)SetEvent(smoke->event);
}

static bool dio_agent_smoke_wait(
    DioAgentSmoke *smoke,
    bool wait_for_complete,
    DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        bool done;
        bool succeeded;
        ULONGLONG now;
        DWORD remaining;
        EnterCriticalSection(&smoke->lock);
        done =
            smoke->failed ||
            (wait_for_complete
                ? smoke->finished
                : smoke->ready);
        succeeded =
            !smoke->failed &&
            (!wait_for_complete || smoke->matched);
        LeaveCriticalSection(&smoke->lock);
        if (done) {
            return succeeded;
        }
        now = GetTickCount64();
        if (now >= deadline) {
            return false;
        }
        remaining = (DWORD)(deadline - now);
        (void)WaitForSingleObject(smoke->event, remaining);
    }
}

static int dio_run_agent_smoke(
    const DioPaths *paths) {
    static const char prompt[] =
        "Reply with exactly DIO_AGENT_SMOKE_OK and nothing else.";
    DioAgentSmoke smoke;
    DioAgentConfig config;
    DioAgent *agent = NULL;
    DioAgentResult result;
    int exit_code = 1;

    if (!dio_app_agent_installed()) {
        return 2;
    }
    ZeroMemory(&smoke, sizeof(smoke));
    InitializeCriticalSection(&smoke.lock);
    smoke.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (smoke.event == NULL) {
        DeleteCriticalSection(&smoke.lock);
        return 3;
    }
    ZeroMemory(&config, sizeof(config));
    config.working_directory = paths->workspace;
    config.callback = dio_agent_smoke_callback;
    config.callback_context = &smoke;
    result = dio_agent_open(&config, &agent);
    if (result == DIO_AGENT_OK &&
        dio_agent_smoke_wait(
            &smoke,
            false,
            DIO_AGENT_READY_TIMEOUT_MS) &&
        dio_agent_submit(
            agent,
            prompt,
            sizeof(prompt) - 1u) == DIO_AGENT_OK &&
        dio_agent_smoke_wait(
            &smoke,
            true,
            DIO_AGENT_SMOKE_TURN_MS)) {
        exit_code = 0;
    }
    dio_agent_close(agent);
    CloseHandle(smoke.event);
    DeleteCriticalSection(&smoke.lock);
    return exit_code;
}

static int dio_run_recovery_self_test(void) {
    DioApp app;
    DioPaths paths;
    wchar_t temporary_root[MAX_PATH];
    wchar_t temporary_directory[MAX_PATH];
    DWORD length;
    bool previous_unclean = false;
    bool marker_exists = false;
    unsigned int expected_attempt;
    int result = 1;

    ZeroMemory(&app, sizeof(app));
    for (expected_attempt = 1u;
         expected_attempt <= DIO_AGENT_RESTART_MAX;
         ++expected_attempt) {
        const ULONGLONG now =
            1000u * (ULONGLONG)expected_attempt;
        if (!dio_app_schedule_agent_restart(&app, now) ||
            dio_app_take_agent_restart(
                &app,
                now + DIO_AGENT_RESTART_DELAY_MS - 1u) ||
            !dio_app_take_agent_restart(
                &app,
                now + DIO_AGENT_RESTART_DELAY_MS) ||
            app.agent_restart_attempts != expected_attempt) {
            return 2;
        }
    }
    if (dio_app_schedule_agent_restart(&app, 10000u)) {
        return 3;
    }
    for (expected_attempt = 1u;
         expected_attempt <= DIO_VOICE_RESTART_MAX;
         ++expected_attempt) {
        const ULONGLONG now =
            20000u + 1000u * (ULONGLONG)expected_attempt;
        if (!dio_app_schedule_voice_restart(&app, now) ||
            dio_app_take_voice_restart(
                &app,
                now + DIO_VOICE_RESTART_DELAY_MS - 1u) ||
            !dio_app_take_voice_restart(
                &app,
                now + DIO_VOICE_RESTART_DELAY_MS) ||
            app.voice_restart_attempts != expected_attempt) {
            return 8;
        }
    }
    if (dio_app_schedule_voice_restart(&app, 30000u)) {
        return 9;
    }

    ZeroMemory(&app, sizeof(app));
    app.agent = (DioAgent *)(uintptr_t)1u;
    app.agent_started_at = 1000u;
    if (dio_app_agent_ready_timed_out(
            &app,
            1000u + DIO_AGENT_READY_TIMEOUT_MS - 1u) ||
        !dio_app_agent_ready_timed_out(
            &app,
            1000u + DIO_AGENT_READY_TIMEOUT_MS)) {
        return 10;
    }
    app.agent_ready = true;
    if (dio_app_agent_ready_timed_out(
            &app,
            1000u + DIO_AGENT_READY_TIMEOUT_MS)) {
        return 11;
    }

    ZeroMemory(&app, sizeof(app));
    app.turn_active = true;
    app.turn_output_suppressed = false;
    app.agent_complete = true;
    app.follow_up_allowed = true;
    app.follow_up_listening = true;
    app.listening_active = true;
    app.voice_gated = true;
    app.turn_started_at = 100u;
    app.agent_generation = 4u;
    app.voice_generation = 7u;
    dio_app_end_failed_turn(&app);
    if (app.turn_active ||
        app.turn_output_suppressed ||
        app.agent_complete ||
        app.follow_up_allowed ||
        app.follow_up_listening ||
        app.listening_active ||
        app.voice_gated ||
        app.turn_started_at != 0u ||
        app.agent_generation != 4u ||
        app.voice_generation != 7u) {
        return 12;
    }

    ZeroMemory(&app, sizeof(app));
    ZeroMemory(&paths, sizeof(paths));
    {
        DioSettings settings;
        DioAgentProfile profile;
        DioAppEvent queued;
        unsigned int index;
        ZeroMemory(&settings, sizeof(settings));
        ZeroMemory(&profile, sizeof(profile));
        dio_agent_profile_init(&profile);
        ZeroMemory(&queued, sizeof(queued));
        queued.kind = DIO_APP_EVENT_UI;
        if (profile.system_prompt == NULL ||
            !dio_app_initialize(&app, &paths, &settings, &profile)) {
            dio_agent_profile_free(&profile);
            return 13;
        }
        dio_agent_profile_free(&profile);
        {
            const DioAgentEvent accepted = {
                .type = DIO_AGENT_EVENT_ACCEPTED,
                .turn_id = 42u,
                .sample_rate = 24000u};
            app.agent_callback.app = &app;
            app.agent_callback.generation = 9u;
            dio_app_agent_callback(
                &app.agent_callback,
                &accepted);
            if (!dio_app_dequeue(&app, &queued) ||
                queued.kind != DIO_APP_EVENT_AGENT ||
                queued.data.agent.generation != 9u ||
                queued.data.agent.turn_id != 42u ||
                queued.data.agent.sample_rate != 24000u) {
                dio_app_release(&app);
                return 16;
            }
        }
        {
            int16_t *samples = (int16_t *)calloc(
                DIO_APP_PCM_CAP + 1u,
                sizeof(*samples));
            if (samples == NULL) {
                dio_app_release(&app);
                return 17;
            }
            samples[0] = 11;
            samples[DIO_APP_PCM_CAP] = 22;
            const DioAgentEvent audio = {
                .type = DIO_AGENT_EVENT_AUDIO_PCM16,
                .turn_id = 43u,
                .pcm16 = samples,
                .sample_count = DIO_APP_PCM_CAP + 1u,
                .sample_rate = 24000u};
            dio_app_agent_callback(
                &app.agent_callback,
                &audio);
            ZeroMemory(
                samples,
                (DIO_APP_PCM_CAP + 1u) *
                    sizeof(*samples));
            free(samples);
            if (!dio_app_dequeue(&app, &queued) ||
                queued.data.agent.sample_count !=
                    DIO_APP_PCM_CAP ||
                queued.data.agent.pcm16[0] != 11 ||
                !dio_app_dequeue(&app, &queued) ||
                queued.data.agent.sample_count != 1u ||
                queued.data.agent.pcm16[0] != 22) {
                dio_app_release(&app);
                return 18;
            }
        }
        for (index = 0u; index < DIO_APP_QUEUE_CAP; ++index) {
            if (!dio_app_enqueue(&app, &queued)) {
                dio_app_release(&app);
                return 14;
            }
        }
        queued.kind = DIO_APP_EVENT_AGENT;
        queued.data.agent.type = DIO_AGENT_EVENT_COMPLETE;
        if (dio_app_enqueue(&app, &queued) ||
            InterlockedCompareExchange(
                &app.queue_overflow,
                0,
                0) == 0 ||
            !dio_app_recover_queue_overflow(&app, 5000u) ||
            app.agent_generation != 1u ||
            app.voice_generation != 1u ||
            app.agent_restart_at !=
                5000u + DIO_AGENT_RESTART_DELAY_MS) {
            dio_app_release(&app);
            return 15;
        }
        dio_app_release(&app);
    }

    ZeroMemory(&paths, sizeof(paths));
    length = GetTempPathW(
        (DWORD)_countof(temporary_root),
        temporary_root);
    if (length == 0u ||
        length >= _countof(temporary_root) ||
        GetTempFileNameW(
            temporary_root,
            L"DIO",
            0u,
            temporary_directory) == 0u ||
        !DeleteFileW(temporary_directory) ||
        !CreateDirectoryW(temporary_directory, NULL) ||
        wcscpy_s(
            paths.logs,
            _countof(paths.logs),
            temporary_directory) != 0) {
        return 4;
    }
    if (!dio_run_marker_start(&paths, &previous_unclean) ||
        previous_unclean) {
        result = 5;
        goto cleanup;
    }
    marker_exists = true;
    if (!dio_run_marker_start(&paths, &previous_unclean) ||
        !previous_unclean ||
        !dio_run_marker_clean(&paths)) {
        result = 6;
        goto cleanup;
    }
    marker_exists = false;
    if (!dio_run_marker_start(&paths, &previous_unclean) ||
        previous_unclean) {
        result = 7;
        goto cleanup;
    }
    marker_exists = true;
    result = 0;

cleanup:
    if (marker_exists) {
        (void)dio_run_marker_clean(&paths);
    }
    (void)RemoveDirectoryW(temporary_directory);
    return result;
}

static bool dio_has_argument(
    int argument_count,
    wchar_t **arguments,
    const wchar_t *expected) {
    int index;
    for (index = 1; index < argument_count; ++index) {
        if (_wcsicmp(arguments[index], expected) == 0) {
            return true;
        }
    }
    return false;
}

static int dio_run_schedule_reminder(
    const DioPaths *paths,
    int argument_count,
    wchar_t **arguments) {
    uint64_t seconds = 0u;
    const wchar_t *cursor;

    if (paths == NULL ||
        arguments == NULL ||
        argument_count != 4 ||
        _wcsicmp(arguments[1], L"--schedule-reminder") != 0 ||
        arguments[2][0] == L'\0') {
        return 4;
    }
    for (cursor = arguments[2]; *cursor != L'\0'; ++cursor) {
        if (*cursor < L'0' || *cursor > L'9') {
            return 4;
        }
        const unsigned int digit =
            (unsigned int)(*cursor - L'0');
        if (seconds > (UINT_MAX - digit) / 10u) {
            return 4;
        }
        seconds = seconds * 10u + digit;
    }
    return dio_announcement_schedule(
               paths->announce,
               (unsigned int)seconds,
               arguments[3])
               ? 0
               : 5;
}

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE previous_instance,
    _In_ PWSTR command_line,
    _In_ int show_command) {
    HANDLE singleton = NULL;
    DioPaths paths;
    DioSettings settings;
    DioAgentProfile profile;
    wchar_t settings_error[512] = L"";
    wchar_t **arguments;
    int argument_count = 0;
    bool ui_smoke;
    bool ui_smoke_english;
    bool settings_smoke;
    bool settings_smoke_english;
    bool agent_smoke;
    bool conversation_smoke;
    bool schedule_reminder;
    bool recovery_self_test;
    bool production_run;
    bool paths_ready = false;
    bool run_marker_started = false;
    bool previous_unclean_exit = false;
    bool settings_loaded;
    bool profile_ready = false;
    HRESULT com;
    int exit_code = 1;

    (void)previous_instance;
    (void)command_line;
    (void)show_command;
#ifdef WER_FAULT_REPORTING_FLAG_NOHEAP
    (void)WerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP);
#endif
    ZeroMemory(&profile, sizeof(profile));

    arguments = CommandLineToArgvW(
        GetCommandLineW(),
        &argument_count);
    ui_smoke =
        arguments != NULL &&
        (dio_has_argument(
             argument_count,
             arguments,
             L"--ui-smoke") ||
         dio_has_argument(
             argument_count,
             arguments,
             L"--ui-smoke-fa") ||
         dio_has_argument(
             argument_count,
             arguments,
             L"--ui-smoke-en"));
    ui_smoke_english =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--ui-smoke-en");
    settings_smoke =
        arguments != NULL &&
        (dio_has_argument(
             argument_count,
             arguments,
             L"--settings-smoke") ||
         dio_has_argument(
             argument_count,
             arguments,
             L"--settings-smoke-fa") ||
         dio_has_argument(
             argument_count,
             arguments,
             L"--settings-smoke-en"));
    settings_smoke_english =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--settings-smoke-en");
    agent_smoke =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--agent-smoke");
    conversation_smoke =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--conversation-smoke");
    schedule_reminder =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--schedule-reminder");
    recovery_self_test =
        arguments != NULL &&
        dio_has_argument(
            argument_count,
            arguments,
            L"--recovery-self-test");
    if (recovery_self_test) {
        const int result = dio_run_recovery_self_test();
        LocalFree(arguments);
        return result;
    }
    production_run =
        !ui_smoke && !settings_smoke && !agent_smoke &&
        !conversation_smoke && !schedule_reminder;

    if (production_run) {
        singleton = CreateMutexW(
            NULL,
            FALSE,
            L"Local\\DIOVoice.Singleton.v1");
        if (singleton == NULL) {
            LocalFree(arguments);
            return 2;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(singleton);
            LocalFree(arguments);
            return 0;
        }
    }
    if (!dio_paths_initialize(
            &paths,
            ui_smoke || settings_smoke)) {
        (void)MessageBoxW(
            NULL,
            L"DIO Voice could not initialize its local data directory.",
            L"DIO Voice",
            MB_OK | MB_ICONERROR);
        goto cleanup_singleton;
    }
    paths_ready = true;
    if (schedule_reminder) {
        exit_code = dio_run_schedule_reminder(
            &paths,
            argument_count,
            arguments);
        goto cleanup_singleton;
    }
    if (production_run) {
        run_marker_started = dio_run_marker_start(
            &paths,
            &previous_unclean_exit);
        if (!run_marker_started) {
            dio_metric(
                &paths,
                "run_marker_error",
                0u,
                GetLastError());
        }
        dio_metric(&paths, "app_start", 0u, 0u);
        if (previous_unclean_exit) {
            dio_metric(
                &paths,
                "previous_unclean_exit",
                0u,
                ERROR_PROCESS_ABORTED);
        }
    }
    {
        const bool settings_reset =
            !(ui_smoke || settings_smoke) ||
            DeleteFileW(paths.settings) ||
            GetLastError() == ERROR_FILE_NOT_FOUND;
        settings_loaded = dio_settings_load_all(
            &paths,
            &settings,
            &profile,
            settings_error,
            _countof(settings_error)) &&
            settings_reset;
        if (!settings_loaded && profile.system_prompt == NULL) {
            dio_agent_profile_init(&profile);
        }
        profile_ready = profile.system_prompt != NULL;
    }
    if (settings_smoke) {
        settings.persian = !settings_smoke_english;
    } else if (ui_smoke) {
        settings.persian = !ui_smoke_english;
    }
    if (agent_smoke) {
        exit_code = settings_loaded
            ? dio_run_agent_smoke(&paths)
            : 4;
        goto cleanup_singleton;
    }
    if (conversation_smoke &&
        (argument_count != 4 ||
         _wcsicmp(
             arguments[1],
             L"--conversation-smoke") != 0 ||
         !settings_loaded ||
         settings.follow_up_ms != 4000u ||
         !dio_app_agent_installed())) {
        exit_code = 4;
        goto cleanup_singleton;
    }

    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        goto cleanup_singleton;
    }
    {
        DioUiOptions options;
        DioUi *ui = NULL;
        ZeroMemory(&options, sizeof(options));
        options.instance = instance;
        options.paths = &paths;
        options.settings = &settings;
        options.profile = &profile;

        if (ui_smoke || settings_smoke) {
            if (settings_loaded && profile_ready &&
                dio_ui_create(&options, &ui)) {
                exit_code = dio_ui_run(
                    ui,
                    ui_smoke,
                    settings_smoke);
                dio_ui_destroy(ui);
            }
        } else {
            DioApp *app = (DioApp *)malloc(sizeof(*app));
            if (app == NULL ||
                !dio_app_initialize(
                    app,
                    &paths,
                    &settings,
                    &profile)) {
                (void)MessageBoxW(
                    NULL,
                    L"DIO Voice could not initialize its controller.",
                    L"DIO Voice",
                    MB_OK | MB_ICONERROR);
                free(app);
            } else {
                options.command = dio_app_ui_callback;
                options.command_context = app;
                if (conversation_smoke) {
                    app->smoke_wavs[0] = arguments[2];
                    app->smoke_wavs[1] = arguments[3];
                    app->smoke_deadline =
                        GetTickCount64() +
                        DIO_CONVERSATION_SMOKE_TIMEOUT_MS;
                    (void)InterlockedExchange(
                        &app->smoke_exit_code,
                        STILL_ACTIVE);
                }
                if (dio_ui_create(&options, &ui)) {
                    app->ui = ui;
                    if (!settings_loaded) {
                        dio_app_post_wide(
                            app,
                            DIO_UI_EVENT_ERROR,
                            settings_error);
                    }
                    app->worker = CreateThread(
                        NULL,
                        0u,
                        dio_app_worker,
                        app,
                        0u,
                        NULL);
                    if (app->worker == NULL) {
                        dio_app_post_wide(
                            app,
                            DIO_UI_EVENT_ERROR,
                            dio_app_localized(
                                app,
                                L"The controller worker could not be started.",
                                L"\u0627\u06cc\u062c\u0627\u062f worker "
                                L"\u06a9\u0646\u062a\u0631\u0644\u0631 "
                                L"\u0645\u0645\u06a9\u0646 \u0646\u0634\u062f."));
                        dio_app_finish_conversation_smoke(
                            app,
                            28);
                    }
                    {
                        const int ui_exit_code =
                            dio_ui_run(
                                ui,
                                false,
                                false);
                        const LONG smoke_exit_code =
                            InterlockedCompareExchange(
                                &app->smoke_exit_code,
                                0,
                                0);
                        exit_code =
                            conversation_smoke
                                ? (smoke_exit_code ==
                                       STILL_ACTIVE
                                    ? 27
                                    : (int)smoke_exit_code)
                                : ui_exit_code;
                    }
                    (void)InterlockedExchange(
                        &app->accepting_events,
                        0);
                    (void)SetEvent(app->stop_event);
                    dio_app_release(app);
                    dio_ui_destroy(ui);
                } else {
                    dio_app_release(app);
                }
                free(app);
            }
        }
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }

cleanup_singleton:
    if (profile_ready) {
        dio_agent_profile_free(&profile);
    }
    if (paths_ready && production_run) {
        if (run_marker_started &&
            !dio_run_marker_clean(&paths)) {
            dio_metric(
                &paths,
                "run_marker_error",
                0u,
                GetLastError());
        }
        dio_metric(
            &paths,
            "app_exit",
            0u,
            exit_code == 0
                ? 0u
                : (unsigned long)exit_code);
    }
    if (singleton != NULL) {
        CloseHandle(singleton);
    }
    LocalFree(arguments);
    return exit_code;
}
