#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

#include "dio_voice/audio.h"
#include "dio_voice/voice_core.h"

typedef struct ProbeOptions {
    bool mic_mode;
    bool no_tail_silence;
    const wchar_t *wav_path;
    const wchar_t *expected_transcript;
    const wchar_t *root;
    const wchar_t *models;
    const wchar_t *vosk_model;
    const wchar_t *device;
} ProbeOptions;

typedef struct ProbeContext {
    CRITICAL_SECTION lock;
    HANDLE transcript_event;
    char *transcript;
    uint64_t max_dropped_frames;
    bool wake_seen;
} ProbeContext;

static HANDLE probe_stop_event = NULL;
static BOOL WINAPI probe_console_handler(DWORD control_type)
{
    if (control_type == CTRL_C_EVENT ||
        control_type == CTRL_BREAK_EVENT ||
        control_type == CTRL_CLOSE_EVENT) {
        if (probe_stop_event != NULL) {
            (void)SetEvent(probe_stop_event);
        }
        return TRUE;
    }
    return FALSE;
}

static const char *probe_event_name(DioVoiceEventType type)
{
    static const char *const names[] = {
        "READY",
        "WAKE",
        "LISTENING",
        "LEVEL",
        "TRANSCRIPT",
        "SPEAKING",
        "SPEECH_COMPLETE",
        "IDLE",
        "ERROR"};
    return
        (unsigned int)type < _countof(names)
            ? names[type]
            : "UNKNOWN";
}

static void probe_callback(void *context, const DioVoiceEvent *event)
{
    ProbeContext *probe = (ProbeContext *)context;

    EnterCriticalSection(&probe->lock);
    if (event->type == DIO_VOICE_EVENT_WAKE) {
        probe->wake_seen = true;
    }
    if (event->dropped_frames > probe->max_dropped_frames) {
        probe->max_dropped_frames = event->dropped_frames;
    }
    LeaveCriticalSection(&probe->lock);

    if (event->type != DIO_VOICE_EVENT_LEVEL) {
        (void)printf(
            "event=%s id=%llu dropped=%llu",
            probe_event_name(event->type),
            (unsigned long long)event->utterance_id,
            (unsigned long long)event->dropped_frames);
        if (event->type == DIO_VOICE_EVENT_WAKE) {
            (void)printf(" earcon_start_ms=%u", event->latency_ms);
        }
        if (event->type == DIO_VOICE_EVENT_SPEECH_COMPLETE) {
            (void)printf(" outcome=%u", (unsigned int)event->speech_outcome);
        }
        if (event->text != NULL && event->text_length != 0u) {
            (void)printf(
                " text=\"%.*s\"",
                (int)event->text_length,
                event->text);
        }
        (void)printf("\n");
        (void)fflush(stdout);
    }

    if (event->type == DIO_VOICE_EVENT_TRANSCRIPT &&
        event->transcript_is_final &&
        event->text != NULL) {
        char *copy = (char *)malloc(event->text_length + 1u);
        if (copy != NULL) {
            (void)memcpy(copy, event->text, event->text_length);
            copy[event->text_length] = '\0';
            EnterCriticalSection(&probe->lock);
            free(probe->transcript);
            probe->transcript = copy;
            LeaveCriticalSection(&probe->lock);
            (void)SetEvent(probe->transcript_event);
        }
    }
}

static wchar_t *probe_join(
    const wchar_t *directory,
    const wchar_t *relative)
{
    const size_t directory_length = wcslen(directory);
    const size_t relative_length = wcslen(relative);
    const bool separator =
        directory_length != 0u &&
        directory[directory_length - 1u] != L'\\' &&
        directory[directory_length - 1u] != L'/';
    const size_t capacity =
        directory_length + (separator ? 1u : 0u) +
        relative_length + 1u;
    wchar_t *path = (wchar_t *)malloc(capacity * sizeof(*path));

    if (path == NULL) {
        return NULL;
    }
    (void)wcscpy_s(path, capacity, directory);
    if (separator) {
        (void)wcscat_s(path, capacity, L"\\");
    }
    (void)wcscat_s(path, capacity, relative);
    return path;
}

static bool probe_parse_options(
    int argument_count,
    wchar_t **arguments,
    ProbeOptions *options)
{
    int index;

    (void)memset(options, 0, sizeof(*options));
    for (index = 1; index < argument_count; ++index) {
        if (wcscmp(arguments[index], L"--wav") == 0 &&
            index + 1 < argument_count) {
            options->wav_path = arguments[++index];
        } else if (wcscmp(arguments[index], L"--expect") == 0 &&
                   index + 1 < argument_count) {
            options->expected_transcript = arguments[++index];
        } else if (wcscmp(arguments[index], L"--mic") == 0) {
            options->mic_mode = true;
        } else if (wcscmp(
                       arguments[index],
                       L"--no-tail-silence") == 0) {
            options->no_tail_silence = true;
        } else if (wcscmp(arguments[index], L"--root") == 0 &&
                   index + 1 < argument_count) {
            options->root = arguments[++index];
        } else if (wcscmp(arguments[index], L"--models") == 0 &&
                   index + 1 < argument_count) {
            options->models = arguments[++index];
        } else if (wcscmp(arguments[index], L"--vosk-model") == 0 &&
                   index + 1 < argument_count) {
            options->vosk_model = arguments[++index];
        } else if (wcscmp(arguments[index], L"--device") == 0 &&
                   index + 1 < argument_count) {
            options->device = arguments[++index];
        } else {
            return false;
        }
    }
    return
        (options->mic_mode != (options->wav_path != NULL)) &&
        (!options->no_tail_silence || options->wav_path != NULL) &&
        (options->expected_transcript == NULL || options->wav_path != NULL);
}

static void probe_usage(const wchar_t *program)
{
    (void)fwprintf(
        stderr,
        L"usage:\n"
        L"  %ls --wav <file.wav> [--root <install-root>] "
        L"[--models <model-root>] [--vosk-model <directory>] "
        L"[--expect <transcript>] "
        L"[--no-tail-silence]\n"
        L"  %ls --mic [--root <version-root>] "
        L"[--models <model-root>] [--vosk-model <directory>] "
        L"[--device <name>]\n"
        L"Defaults: root=.dio\\versions\\current, "
        L"models=.dio\\models\n",
        program,
        program);
}

static bool probe_utf8_equals_wide(
    const char *utf8,
    const wchar_t *wide)
{
    int required;
    wchar_t *converted;
    bool equal;

    if (wide == NULL) {
        return true;
    }
    if (utf8 == NULL) {
        return false;
    }
    required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8,
        -1,
        NULL,
        0);
    if (required <= 0) {
        return false;
    }
    converted = (wchar_t *)malloc((size_t)required * sizeof(*converted));
    if (converted == NULL ||
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8,
            -1,
            converted,
            required) != required) {
        free(converted);
        return false;
    }
    equal = wcscmp(converted, wide) == 0;
    free(converted);
    return equal;
}

static char *probe_take_transcript(ProbeContext *context)
{
    char *text;

    EnterCriticalSection(&context->lock);
    text = context->transcript;
    context->transcript = NULL;
    LeaveCriticalSection(&context->lock);
    return text;
}

static int probe_run_wav(
    DioVoiceCore *voice,
    ProbeContext *context,
    const wchar_t *path,
    const wchar_t *expected_transcript,
    bool no_tail_silence)
{
    DioAudioPcm pcm;
    char error[512];
    size_t offset = 0u;
    int16_t silence[DIO_VOICE_FRAME_SAMPLES] = {0};
    unsigned int index;

    if (dio_audio_read_wav_mono_16k(
            path,
            &pcm,
            error,
            sizeof(error)) != DIO_AUDIO_OK) {
        (void)fprintf(stderr, "WAV error: %s\n", error);
        return 1;
    }
    while (offset < pcm.sample_count) {
        const size_t count =
            pcm.sample_count - offset < DIO_VOICE_FRAME_SAMPLES
                ? pcm.sample_count - offset
                : DIO_VOICE_FRAME_SAMPLES;
        if (dio_voice_feed_audio(
                voice,
                pcm.samples + offset,
                count) != DIO_VOICE_OK) {
            (void)fprintf(stderr, "audio feed failed\n");
            dio_audio_pcm_free(&pcm);
            return 1;
        }
        offset += count;
        Sleep(1u);
    }
    if (!no_tail_silence) {
        for (index = 0u; index < 63u; ++index) {
            (void)dio_voice_feed_audio(
                voice,
                silence,
                DIO_VOICE_FRAME_SAMPLES);
            Sleep(1u);
        }
    }
    dio_audio_pcm_free(&pcm);

    if (WaitForSingleObject(context->transcript_event, 30000u) !=
        WAIT_OBJECT_0) {
        (void)fprintf(
            stderr,
            "no final transcript within 30 seconds "
            "(WAV mode never opened a playback device)\n");
        return 1;
    }
    EnterCriticalSection(&context->lock);
    {
        const bool transcript_matches =
            context->transcript != NULL &&
            context->transcript[0] != '\0' &&
            probe_utf8_equals_wide(
                context->transcript,
                expected_transcript);
        const uint64_t core_dropped =
            dio_voice_dropped_frames(voice);
        const uint64_t dropped =
            context->max_dropped_frames > core_dropped
                ? context->max_dropped_frames
                : core_dropped;
        const bool valid =
            context->wake_seen &&
            dropped == 0u &&
            transcript_matches;
        const bool wake_seen = context->wake_seen;
        LeaveCriticalSection(&context->lock);
        if (!valid) {
            (void)fprintf(
                stderr,
                "WAV verification failed: wake=%u dropped=%llu "
                "transcript_match=%u\n",
                wake_seen ? 1u : 0u,
                (unsigned long long)dropped,
                transcript_matches ? 1u : 0u);
            return 1;
        }
    }
    return 0;
}

static int probe_run_mic(
    DioVoiceCore *voice,
    ProbeContext *context)
{
    HANDLE waits[] = {
        probe_stop_event,
        context->transcript_event};

    (void)printf("microphone probe running; press Ctrl+C to stop\n");
    for (;;) {
        DWORD wait_result = WaitForMultipleObjects(
            _countof(waits),
            waits,
            FALSE,
            INFINITE);
        if (wait_result == WAIT_OBJECT_0) {
            uint64_t dropped;
            EnterCriticalSection(&context->lock);
            dropped = context->max_dropped_frames;
            LeaveCriticalSection(&context->lock);
            {
                const uint64_t core_dropped =
                    dio_voice_dropped_frames(voice);
                if (core_dropped > dropped) {
                    dropped = core_dropped;
                }
            }
            (void)printf(
                "microphone probe stopped; dropped=%llu\n",
                (unsigned long long)dropped);
            return dropped == 0u ? 0 : 1;
        }
        if (wait_result == WAIT_OBJECT_0 + 1u) {
            char *text = probe_take_transcript(context);
            free(text);
        } else if (wait_result == WAIT_FAILED) {
            (void)fprintf(stderr, "probe wait failed\n");
            return 1;
        }
    }
}

int wmain(int argument_count, wchar_t **arguments)
{
    ProbeOptions options;
    ProbeContext context;
    DioVoiceConfig config;
    DioVoiceCore *voice = NULL;
    wchar_t *onnxruntime = NULL;
    wchar_t *wake_model = NULL;
    wchar_t *silero = NULL;
    wchar_t *vosk_library = NULL;
    wchar_t *vosk_model = NULL;
    wchar_t default_root[MAX_PATH];
    wchar_t *default_models = NULL;
    DioVoiceResult result;
    int exit_code = 1;

    (void)SetConsoleOutputCP(CP_UTF8);
    if (!probe_parse_options(
            argument_count,
            arguments,
            &options)) {
        probe_usage(arguments[0]);
        return 2;
    }
    if (options.root == NULL) {
        const DWORD length = GetFullPathNameW(
            L".dio\\versions\\current",
            (DWORD)_countof(default_root),
            default_root,
            NULL);
        if (length == 0u || length >= _countof(default_root)) {
            (void)fprintf(stderr, "could not resolve portable root\n");
            return 2;
        }
        options.root = default_root;
    }
    if (options.models == NULL) {
        default_models = probe_join(L".dio", L"models");
        if (default_models == NULL) {
            (void)fprintf(stderr, "out of memory building default models\n");
            return 1;
        }
        options.models = default_models;
    }

    onnxruntime =
        probe_join(options.root, L"runtime\\onnxruntime\\onnxruntime.dll");
    wake_model = probe_join(options.models, L"porcupine");
    silero = probe_join(options.models, L"silero_vad.onnx");
    vosk_library =
        probe_join(options.root, L"runtime\\vosk\\libvosk.dll");
    vosk_model =
        options.vosk_model == NULL
            ? probe_join(options.models, L"vosk")
            : _wcsdup(options.vosk_model);
    if (onnxruntime == NULL || wake_model == NULL || silero == NULL ||
        vosk_library == NULL || vosk_model == NULL) {
        (void)fprintf(stderr, "out of memory building probe paths\n");
        goto cleanup_paths;
    }

    (void)memset(&context, 0, sizeof(context));
    InitializeCriticalSection(&context.lock);
    context.transcript_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    probe_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (context.transcript_event == NULL ||
        probe_stop_event == NULL) {
        (void)fprintf(stderr, "could not create probe events\n");
        goto cleanup_context;
    }
    (void)SetConsoleCtrlHandler(probe_console_handler, TRUE);

    (void)memset(&config, 0, sizeof(config));
    config.onnxruntime_path = onnxruntime;
    config.wake_model_directory = wake_model;
    config.wake_sensitivity = 0.5f;
    config.silero_model_path = silero;
    config.vosk_library_path = vosk_library;
    config.vosk_model_directory = vosk_model;
    config.capture_device_name = options.device;
    config.follow_up_ms = 4000u;
    config.external_audio = !options.mic_mode;
    config.callback = probe_callback;
    config.callback_context = &context;

    result = dio_voice_open(&config, &voice);
    if (result != DIO_VOICE_OK) {
        (void)fprintf(stderr, "voice open failed: %u\n", (unsigned int)result);
        goto cleanup_context;
    }
    result = dio_voice_start(voice);
    if (result != DIO_VOICE_OK) {
        (void)fprintf(stderr, "voice start failed: %u\n", (unsigned int)result);
        goto cleanup_voice;
    }

    exit_code =
        options.mic_mode
            ? probe_run_mic(voice, &context)
            : probe_run_wav(
                voice,
                &context,
                options.wav_path,
                options.expected_transcript,
                options.no_tail_silence);

cleanup_voice:
    dio_voice_close(voice);
cleanup_context:
    (void)SetConsoleCtrlHandler(probe_console_handler, FALSE);
    free(context.transcript);
    if (probe_stop_event != NULL) {
        (void)CloseHandle(probe_stop_event);
        probe_stop_event = NULL;
    }
    if (context.transcript_event != NULL) {
        (void)CloseHandle(context.transcript_event);
    }
    DeleteCriticalSection(&context.lock);
cleanup_paths:
    free(vosk_model);
    free(vosk_library);
    free(silero);
    free(wake_model);
    free(onnxruntime);
    free(default_models);
    return exit_code;
}
