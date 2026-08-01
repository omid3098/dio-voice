#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "dio_voice/audio.h"
#include "dio_voice/voice_core.h"
#include "audio_capture.h"
#include "earcon.h"
#include "ort_runtime.h"
#include "vosk_runtime.h"
#include "wake_onnx.h"
#include "voice_core_internal.h"
#include "yyjson.h"

static int failures = 0;

static void check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_capture_selector(void)
{
    check(
        dio_audio_capture_name_equal(
            "Desk Microphone",
            L"desk microphone"),
        "capture selector rejected exact case-insensitive name");
    check(
        !dio_audio_capture_name_equal(
            "Desk Microphone (USB)",
            L"Desk Microphone"),
        "capture selector accepted an overlapping name");
    check(
        dio_audio_capture_name_equal(
            "\xd9\x85\xdb\x8c\xda\xa9\xd8\xb1\xd9\x88\xd9\x81\xd9\x88\xd9\x86",
            L"\u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646"),
        "capture selector rejected a Persian UTF-8 name");
}

static wchar_t *join_path(const wchar_t *directory, const wchar_t *name)
{
    const size_t directory_length = wcslen(directory);
    const size_t name_length = wcslen(name);
    const bool separator =
        directory_length != 0u &&
        directory[directory_length - 1u] != L'\\' &&
        directory[directory_length - 1u] != L'/';
    const size_t capacity =
        directory_length + (separator ? 1u : 0u) + name_length + 1u;
    wchar_t *path = (wchar_t *)malloc(capacity * sizeof(*path));

    if (path == NULL) {
        return NULL;
    }
    (void)wcscpy_s(path, capacity, directory);
    if (separator) {
        (void)wcscat_s(path, capacity, L"\\");
    }
    (void)wcscat_s(path, capacity, name);
    return path;
}

static unsigned char *read_file(
    const wchar_t *path,
    size_t *size)
{
    FILE *file = NULL;
    long length;
    unsigned char *bytes = NULL;

    *size = 0u;
    if (_wfopen_s(&file, path, L"rb") != 0 || file == NULL ||
        fseek(file, 0L, SEEK_END) != 0) {
        goto cleanup;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        goto cleanup;
    }
    bytes = (unsigned char *)malloc((size_t)length + 1u);
    if (bytes == NULL) {
        goto cleanup;
    }
    if (fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        bytes = NULL;
        goto cleanup;
    }
    bytes[length] = 0u;
    *size = (size_t)length;

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    return bytes;
}

static yyjson_doc *read_json(const wchar_t *path)
{
    size_t size;
    unsigned char *bytes = read_file(path, &size);
    yyjson_doc *document;

    if (bytes == NULL) {
        return NULL;
    }
    document = yyjson_read((char *)bytes, size, 0u);
    free(bytes);
    return document;
}

static bool compare_float_file(
    const wchar_t *path,
    const float *actual,
    size_t count,
    float tolerance,
    const char *label)
{
    size_t size;
    unsigned char *bytes = read_file(path, &size);
    const float *expected;
    size_t index;
    bool equal = true;

    if (bytes == NULL || size != count * sizeof(*actual)) {
        (void)fprintf(stderr, "FAIL: could not read %s golden\n", label);
        free(bytes);
        ++failures;
        return false;
    }
    expected = (const float *)bytes;
    for (index = 0u; index < count; ++index) {
        if (fabsf(actual[index] - expected[index]) > tolerance) {
            (void)fprintf(
                stderr,
                "FAIL: %s[%zu] expected %.9g, got %.9g\n",
                label,
                index,
                (double)expected[index],
                (double)actual[index]);
            equal = false;
            ++failures;
            break;
        }
    }
    free(bytes);
    return equal;
}

static wchar_t *wake_fixture_path(
    const wchar_t *fixture_directory,
    const char *utf8_name)
{
    wchar_t wide_name[260];

    if (utf8_name == NULL ||
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8_name,
            -1,
            wide_name,
            _countof(wide_name)) == 0) {
        return NULL;
    }
    return join_path(fixture_directory, wide_name);
}

static bool run_wake_file(
    const wchar_t *model_directory,
    float sensitivity,
    const wchar_t *path,
    int64_t expected_trigger_frame)
{
    DioAudioPcm pcm;
    DioWakePorcupine wake;
    char error[512];
    size_t frame_count;
    size_t frame;
    int64_t trigger_frame = -1;
    bool succeeded = true;

    (void)memset(&pcm, 0, sizeof(pcm));
    (void)memset(&wake, 0, sizeof(wake));
    if (!dio_wake_open(
            model_directory,
            sensitivity,
            &wake,
            error,
            sizeof(error))) {
        (void)fprintf(stderr, "FAIL: wake open failed: %s\n", error);
        ++failures;
        return false;
    }
    if (dio_audio_read_wav_mono_16k(
            path,
            &pcm,
            error,
            sizeof(error)) != DIO_AUDIO_OK) {
        (void)fprintf(stderr, "FAIL: WAV read failed: %s\n", error);
        ++failures;
        succeeded = false;
        goto cleanup;
    }

    frame_count = pcm.sample_count / DIO_VOICE_FRAME_SAMPLES;
    for (frame = 0u; frame < frame_count; ++frame) {
        bool triggered;

        if (!dio_wake_feed(
                &wake,
                pcm.samples + (frame * DIO_VOICE_FRAME_SAMPLES),
                &triggered,
                error,
                sizeof(error))) {
            (void)fprintf(stderr, "FAIL: wake inference failed: %s\n", error);
            ++failures;
            succeeded = false;
            break;
        }
        if (triggered && trigger_frame < 0) {
            trigger_frame = (int64_t)frame;
        }
    }
    if (trigger_frame != expected_trigger_frame) {
        (void)fprintf(
            stderr,
            "FAIL: expected wake trigger frame %lld, got %lld\n",
            (long long)expected_trigger_frame,
            (long long)trigger_frame);
        ++failures;
        succeeded = false;
    }

cleanup:
    dio_audio_pcm_free(&pcm);
    dio_wake_close(&wake);
    return succeeded;
}

static void test_wake(
    const wchar_t *model_directory,
    const wchar_t *fixture_directory)
{
    wchar_t *golden_path = join_path(fixture_directory, L"golden.json");
    yyjson_doc *document = NULL;
    yyjson_val *root;
    yyjson_val *positive_files;
    yyjson_val *negative_files;
    const char *expected_version;
    float sensitivity;
    DioWakePorcupine wake;
    char error[512];
    size_t index;

    (void)memset(&wake, 0, sizeof(wake));
    check(golden_path != NULL, "could not allocate wake golden path");
    if (golden_path == NULL) {
        return;
    }
    document = read_json(golden_path);
    check(document != NULL, "could not parse wake golden JSON");
    if (document == NULL) {
        goto cleanup;
    }
    root = yyjson_doc_get_root(document);
    positive_files = yyjson_obj_get(root, "positive_files");
    negative_files = yyjson_obj_get(root, "negative_files");
    expected_version =
        yyjson_get_str(yyjson_obj_get(root, "engine_version"));
    sensitivity =
        (float)yyjson_get_num(yyjson_obj_get(root, "sensitivity"));

    if (dio_wake_open(
            model_directory,
            sensitivity,
            &wake,
            error,
            sizeof(error))) {
        const char *actual_version = dio_wake_version(&wake);
        check(
            actual_version != NULL &&
                expected_version != NULL &&
                strcmp(actual_version, expected_version) == 0,
            "unexpected Porcupine version");
        dio_wake_close(&wake);
    } else {
        check(false, error);
        goto cleanup;
    }

    for (index = 0u; index < yyjson_arr_size(positive_files); ++index) {
        yyjson_val *entry = yyjson_arr_get(positive_files, index);
        wchar_t *path = wake_fixture_path(
            fixture_directory,
            yyjson_get_str(yyjson_obj_get(entry, "file")));

        check(path != NULL, "invalid positive wake fixture path");
        if (path != NULL) {
            (void)run_wake_file(
                model_directory,
                sensitivity,
                path,
                yyjson_get_sint(
                    yyjson_obj_get(entry, "first_trigger_frame")));
        }
        free(path);
    }

    for (index = 0u; index < yyjson_arr_size(negative_files); ++index) {
        wchar_t *path = wake_fixture_path(
            fixture_directory,
            yyjson_get_str(yyjson_arr_get(negative_files, index)));

        check(path != NULL, "invalid negative wake fixture path");
        if (path != NULL) {
            (void)run_wake_file(
                model_directory,
                sensitivity,
                path,
                -1);
        }
        free(path);
    }

cleanup:
    if (document != NULL) {
        yyjson_doc_free(document);
    }
    free(golden_path);
}

static void test_silero(
    DioOrtRuntime *runtime,
    const wchar_t *model_path,
    const wchar_t *fixture_directory)
{
    wchar_t *golden_path =
        join_path(fixture_directory, L"silero-golden.json");
    wchar_t *wav_path =
        join_path(fixture_directory, L"verify-command-hava.wav");
    wchar_t *state_path =
        join_path(fixture_directory, L"silero-final-state.f32");
    yyjson_doc *document = NULL;
    yyjson_val *root;
    yyjson_val *probabilities;
    float tolerance;
    DioAudioPcm pcm;
    DioSileroVad vad;
    char error[512];
    size_t frame_count;
    size_t index;

    check(
        golden_path != NULL && wav_path != NULL && state_path != NULL,
        "could not allocate Silero fixture paths");
    if (golden_path == NULL || wav_path == NULL || state_path == NULL) {
        goto cleanup;
    }
    document = read_json(golden_path);
    check(document != NULL, "could not parse Silero golden JSON");
    if (document == NULL) {
        goto cleanup;
    }
    root = yyjson_doc_get_root(document);
    probabilities = yyjson_obj_get(root, "probabilities");
    tolerance =
        (float)yyjson_get_num(yyjson_obj_get(root, "tensor_tolerance"));

    check(
        dio_audio_read_wav_mono_16k(
            wav_path,
            &pcm,
            error,
            sizeof(error)) == DIO_AUDIO_OK,
        error);
    if (pcm.samples == NULL) {
        goto cleanup;
    }
    check(
        dio_silero_open(
            runtime,
            model_path,
            &vad,
            error,
            sizeof(error)),
        error);
    if (vad.runtime == NULL) {
        dio_audio_pcm_free(&pcm);
        goto cleanup;
    }

    frame_count = pcm.sample_count / DIO_VOICE_FRAME_SAMPLES;
    check(
        frame_count == yyjson_arr_size(probabilities),
        "Silero frame count differs from golden");
    for (index = 0u;
         index < frame_count && index < yyjson_arr_size(probabilities);
         ++index) {
        float probability;
        const float expected =
            (float)yyjson_get_num(yyjson_arr_get(probabilities, index));
        if (!dio_silero_probability(
                &vad,
                pcm.samples + (index * DIO_VOICE_FRAME_SAMPLES),
                &probability,
                error,
                sizeof(error))) {
            check(false, error);
            break;
        }
        if (fabsf(probability - expected) > tolerance) {
            (void)fprintf(
                stderr,
                "FAIL: Silero probability[%zu] expected %.9g, got %.9g\n",
                index,
                (double)expected,
                (double)probability);
            ++failures;
            break;
        }
    }
    (void)compare_float_file(
        state_path,
        dio_silero_state_snapshot(&vad),
        DIO_SILERO_STATE_VALUES,
        tolerance,
        "silero-final-state");

    dio_silero_close(&vad);
    dio_audio_pcm_free(&pcm);

cleanup:
    if (document != NULL) {
        yyjson_doc_free(document);
    }
    free(state_path);
    free(wav_path);
    free(golden_path);
}

static void test_vosk(
    const wchar_t *library_path,
    const wchar_t *model_directory,
    const wchar_t *fixture_directory)
{
    wchar_t *golden_path =
        join_path(fixture_directory, L"vosk-golden.json");
    yyjson_doc *document = NULL;
    yyjson_val *root;
    yyjson_val *transcripts;
    yyjson_val *key;
    yyjson_val *value;
    size_t index;
    size_t count;
    DioVoskRuntime runtime;
    char error[512];

    check(golden_path != NULL, "could not allocate Vosk golden path");
    if (golden_path == NULL) {
        return;
    }
    document = read_json(golden_path);
    check(document != NULL, "could not parse Vosk golden JSON");
    if (document == NULL) {
        goto cleanup;
    }
    check(
        dio_vosk_runtime_open(
            library_path,
            model_directory,
            &runtime,
            error,
            sizeof(error)),
        error);
    if (runtime.model == NULL) {
        goto cleanup;
    }

    root = yyjson_doc_get_root(document);
    transcripts = yyjson_obj_get(root, "transcripts");
    yyjson_obj_foreach(transcripts, index, count, key, value) {
        const char *utf8_name = yyjson_get_str(key);
        const char *expected = yyjson_get_str(value);
        wchar_t wide_name[260];
        wchar_t *wav_path;
        DioAudioPcm pcm;
        char *actual = NULL;

        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                utf8_name,
                -1,
                wide_name,
                _countof(wide_name)) == 0) {
            check(false, "invalid UTF-8 Vosk fixture name");
            continue;
        }
        wav_path = join_path(fixture_directory, wide_name);
        check(wav_path != NULL, "could not allocate Vosk WAV path");
        if (wav_path == NULL) {
            continue;
        }
        if (dio_audio_read_wav_mono_16k(
                wav_path,
                &pcm,
                error,
                sizeof(error)) != DIO_AUDIO_OK) {
            check(false, error);
            free(wav_path);
            continue;
        }
        if (!dio_vosk_transcribe(
                &runtime,
                pcm.samples,
                pcm.sample_count,
                &actual,
                error,
                sizeof(error))) {
            check(false, error);
        } else if (strcmp(actual, expected) != 0) {
            (void)fprintf(
                stderr,
                "FAIL: Vosk %s expected \"%s\", got \"%s\"\n",
                utf8_name,
                expected,
                actual);
            ++failures;
        }
        dio_vosk_text_free(actual);
        dio_audio_pcm_free(&pcm);
        free(wav_path);
    }
    dio_vosk_runtime_close(&runtime);

cleanup:
    if (document != NULL) {
        yyjson_doc_free(document);
    }
    free(golden_path);
}

typedef struct LifecycleEvents {
    CRITICAL_SECTION lock;
    HANDLE ready;
    HANDLE transcript;
    HANDLE partial;
    HANDLE speech;
    HANDLE listening;
    HANDLE idle;
    char *final_text;
    uint64_t speech_id;
    DioVoiceSpeechOutcome speech_outcome;
    unsigned long error_code;
    unsigned int final_count;
    unsigned int close_speech_counts[8];
} LifecycleEvents;

static void lifecycle_callback(
    void *context,
    const DioVoiceEvent *event)
{
    LifecycleEvents *events = (LifecycleEvents *)context;

    if (event->type == DIO_VOICE_EVENT_READY) {
        (void)SetEvent(events->ready);
    } else if (event->type == DIO_VOICE_EVENT_LISTENING) {
        (void)SetEvent(events->listening);
    } else if (event->type == DIO_VOICE_EVENT_IDLE) {
        (void)SetEvent(events->idle);
    } else if (event->type == DIO_VOICE_EVENT_TRANSCRIPT &&
               !event->transcript_is_final &&
               event->text_length != 0u) {
        (void)SetEvent(events->partial);
    } else if (event->type == DIO_VOICE_EVENT_TRANSCRIPT &&
               event->transcript_is_final &&
               event->text != NULL) {
        char *copy = (char *)malloc(event->text_length + 1u);
        if (copy != NULL) {
            (void)memcpy(copy, event->text, event->text_length);
            copy[event->text_length] = '\0';
            EnterCriticalSection(&events->lock);
            free(events->final_text);
            events->final_text = copy;
            ++events->final_count;
            LeaveCriticalSection(&events->lock);
            (void)SetEvent(events->transcript);
        }
    } else if (event->type == DIO_VOICE_EVENT_SPEECH_COMPLETE) {
        EnterCriticalSection(&events->lock);
        events->speech_id = event->utterance_id;
        events->speech_outcome = event->speech_outcome;
        if (event->utterance_id >= 5000u &&
            event->utterance_id < 5008u) {
            ++events->close_speech_counts[
                (size_t)(event->utterance_id - 5000u)];
        }
        LeaveCriticalSection(&events->lock);
        (void)SetEvent(events->speech);
    } else if (event->type == DIO_VOICE_EVENT_ERROR) {
        EnterCriticalSection(&events->lock);
        events->error_code = event->system_error;
        LeaveCriticalSection(&events->lock);
    }
}

static void lifecycle_feed_silence(
    DioVoiceCore *voice,
    const int16_t silence[DIO_VOICE_FRAME_SAMPLES],
    unsigned int frame_count)
{
    unsigned int index;
    for (index = 0u; index < frame_count; ++index) {
        check(
            dio_voice_feed_audio(
                voice,
                silence,
                DIO_VOICE_FRAME_SAMPLES) == DIO_VOICE_OK,
            "silence feed failed");
    }
}

static void lifecycle_feed_pcm(
    DioVoiceCore *voice,
    const DioAudioPcm *pcm,
    const int16_t silence[DIO_VOICE_FRAME_SAMPLES],
    size_t pause_after_frames,
    unsigned int pause_frames)
{
    size_t offset = 0u;
    size_t frame = 0u;

    while (offset < pcm->sample_count) {
        const size_t count =
            pcm->sample_count - offset < DIO_VOICE_FRAME_SAMPLES
                ? pcm->sample_count - offset
                : DIO_VOICE_FRAME_SAMPLES;
        if (frame == pause_after_frames && pause_frames != 0u) {
            lifecycle_feed_silence(voice, silence, pause_frames);
        }
        check(
            dio_voice_feed_audio(
                voice,
                pcm->samples + offset,
                count) == DIO_VOICE_OK,
            "external audio feed failed");
        offset += count;
        ++frame;
    }
}

static void test_vad_hysteresis(void)
{
    bool active = false;

    check(
        dio_voice_vad_update(0.90f, 0.55f, 0.15f, &active) && active,
        "VAD did not enter speech");
    check(
        !dio_voice_vad_update(0.39f, 0.55f, 0.15f, &active) && !active,
        "VAD did not leave speech below the hysteresis threshold");
    check(
        !dio_voice_vad_update(0.45f, 0.55f, 0.15f, &active) && !active,
        "VAD re-entered speech at the inactive noise level");
}

static void test_lifecycle(
    const wchar_t *onnxruntime_path,
    const wchar_t *wake_model_directory,
    const wchar_t *silero_path,
    const wchar_t *vosk_library_path,
    const wchar_t *vosk_model_directory,
    const wchar_t *fixture_directory)
{
    wchar_t *wav_path =
        join_path(fixture_directory, L"verify-command-hava.wav");
    wchar_t *follow_up_wav_path =
        join_path(fixture_directory, L"verify-command-bargard.wav");
    wchar_t *empty_candidate_wav_path =
        join_path(fixture_directory, L"sapi-alexa.wav");
    DioAudioPcm pcm;
    DioAudioPcm follow_up_pcm;
    DioAudioPcm empty_candidate_pcm;
    DioVoiceConfig config;
    DioVoiceCore *voice = NULL;
    LifecycleEvents events;
    char error[512];
    int16_t silence[DIO_VOICE_FRAME_SAMPLES] = {0};
    unsigned int index;
    ULONGLONG model_started;
    ULONGLONG model_ready_ms;
    ULONGLONG follow_up_started;
    ULONGLONG follow_up_elapsed;
    const size_t candidate_samples =
        8u * DIO_VOICE_FRAME_SAMPLES;

    (void)memset(&pcm, 0, sizeof(pcm));
    (void)memset(&follow_up_pcm, 0, sizeof(follow_up_pcm));
    (void)memset(
        &empty_candidate_pcm,
        0,
        sizeof(empty_candidate_pcm));
    (void)memset(&events, 0, sizeof(events));
    InitializeCriticalSection(&events.lock);
    events.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    events.transcript = CreateEventW(NULL, TRUE, FALSE, NULL);
    events.partial = CreateEventW(NULL, TRUE, FALSE, NULL);
    events.speech = CreateEventW(NULL, TRUE, FALSE, NULL);
    events.listening = CreateEventW(NULL, TRUE, FALSE, NULL);
    events.idle = CreateEventW(NULL, TRUE, FALSE, NULL);
    check(
        wav_path != NULL && follow_up_wav_path != NULL &&
            empty_candidate_wav_path != NULL &&
            events.ready != NULL &&
            events.transcript != NULL && events.partial != NULL &&
            events.speech != NULL &&
            events.listening != NULL && events.idle != NULL,
        "could not prepare lifecycle test");
    if (wav_path == NULL || follow_up_wav_path == NULL ||
        empty_candidate_wav_path == NULL ||
        events.ready == NULL ||
        events.transcript == NULL || events.partial == NULL ||
        events.speech == NULL ||
        events.listening == NULL || events.idle == NULL) {
        goto cleanup;
    }

    (void)memset(&config, 0, sizeof(config));
    config.onnxruntime_path = onnxruntime_path;
    config.wake_model_directory = wake_model_directory;
    config.wake_sensitivity = 0.5f;
    config.silero_model_path = silero_path;
    config.vosk_library_path = vosk_library_path;
    config.vosk_model_directory = vosk_model_directory;
    config.follow_up_ms = 4000u;
    config.external_audio = true;
    config.callback = lifecycle_callback;
    config.callback_context = &events;
    model_started = GetTickCount64();
    check(
        dio_voice_open(&config, &voice) == DIO_VOICE_OK,
        "voice lifecycle open failed");
    if (voice == NULL) {
        goto cleanup;
    }
    check(
        dio_voice_start(voice) == DIO_VOICE_OK,
        "voice lifecycle start failed");
    check(
        WaitForSingleObject(events.ready, 1000u) == WAIT_OBJECT_0,
        "READY was not emitted");
    check(
        WaitForSingleObject(events.idle, 1000u) == WAIT_OBJECT_0,
        "startup did not enter IDLE");
    (void)ResetEvent(events.idle);
    model_ready_ms = GetTickCount64() - model_started;
    (void)printf(
        "model-ready latency: %llu ms\n",
        (unsigned long long)model_ready_ms);
    check(model_ready_ms <= 8000u, "model-ready exceeded 8 second gate");
    check(
        dio_voice_push_to_talk(voice) == DIO_VOICE_OK,
        "push-to-talk request failed");
    (void)printf("lifecycle: push-to-talk requested\n");

    check(
        dio_audio_read_wav_mono_16k(
            wav_path,
            &pcm,
            error,
            sizeof(error)) == DIO_AUDIO_OK,
        error);
    if (pcm.samples == NULL) {
        goto close_voice;
    }
    check(
        dio_audio_read_wav_mono_16k(
            follow_up_wav_path,
            &follow_up_pcm,
            error,
            sizeof(error)) == DIO_AUDIO_OK,
        error);
    if (follow_up_pcm.samples == NULL) {
        goto close_voice;
    }
    check(
        dio_audio_read_wav_mono_16k(
            empty_candidate_wav_path,
            &empty_candidate_pcm,
            error,
            sizeof(error)) == DIO_AUDIO_OK,
        error);
    if (empty_candidate_pcm.samples == NULL) {
        goto close_voice;
    }
    check(
        empty_candidate_pcm.sample_count >= candidate_samples &&
            follow_up_pcm.sample_count > candidate_samples,
        "STT candidate fixtures are too short");
    if (empty_candidate_pcm.sample_count < candidate_samples ||
        follow_up_pcm.sample_count <= candidate_samples) {
        goto close_voice;
    }
    empty_candidate_pcm.sample_count = candidate_samples;
    check(
        pcm.sample_count >=
            20u * DIO_VOICE_FRAME_SAMPLES,
        "command fixture is too short for limit tests");
    if (pcm.sample_count <
        20u * DIO_VOICE_FRAME_SAMPLES) {
        goto close_voice;
    }
    (void)printf(
        "lifecycle: feeding %zu command samples\n",
        pcm.sample_count);
    lifecycle_feed_pcm(voice, &pcm, silence, 25u, 16u);
    lifecycle_feed_silence(voice, silence, 63u);
    (void)printf("lifecycle: waiting for final transcript\n");
    check(
        WaitForSingleObject(events.transcript, 30000u) ==
            WAIT_OBJECT_0,
        "final lifecycle transcript timed out");
    EnterCriticalSection(&events.lock);
    check(
        events.final_text != NULL &&
            strcmp(events.final_text, "امروز هوا چطور است") == 0,
        "lifecycle transcript differs from golden");
    check(events.final_count == 1u, "initial command final count changed");
    LeaveCriticalSection(&events.lock);
    (void)printf("lifecycle: final transcript received\n");

    check(
        WaitForSingleObject(events.idle, 1000u) ==
            WAIT_OBJECT_0,
        "command completion did not return to IDLE");
    (void)ResetEvent(events.idle);
    check(
        dio_voice_speak(voice, 4242u, "test", 4u) ==
            DIO_VOICE_OK,
        "async speech queue failed");
    check(
        WaitForSingleObject(events.speech, 5000u) ==
            WAIT_OBJECT_0,
        "SPEECH_COMPLETE timed out");
    EnterCriticalSection(&events.lock);
    check(events.speech_id == 4242u, "speech completion ID changed");
    check(
        events.speech_outcome == DIO_VOICE_SPEECH_FAILED,
        "unconfigured TTS did not terminate as failed");
    LeaveCriticalSection(&events.lock);
    check(
        WaitForSingleObject(events.idle, 150u) == WAIT_TIMEOUT,
        "speech worker emitted a stale recognition IDLE");
    (void)printf("lifecycle: initial speech terminal received\n");

    check(
        dio_voice_set_paused(voice, true) == DIO_VOICE_OK,
        "could not pause before atomic follow-up");
    check(
        WaitForSingleObject(events.idle, 1000u) ==
            WAIT_OBJECT_0,
        "pause did not enter IDLE");
    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.transcript);
    dio_voice_test_mark_recognition_speaking(voice);
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_OK,
        "explicit follow-up request failed");
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "follow-up did not enter LISTENING");
    lifecycle_feed_pcm(
        voice,
        &follow_up_pcm,
        silence,
        0u,
        0u);
    lifecycle_feed_silence(voice, silence, 63u);
    check(
        WaitForSingleObject(events.transcript, 30000u) ==
            WAIT_OBJECT_0,
        "spoken follow-up transcript timed out");
    EnterCriticalSection(&events.lock);
    check(
        events.final_text != NULL &&
            strcmp(
                events.final_text,
                "برگرد خب که چی می‌گویی") == 0,
        "spoken follow-up transcript differs from golden");
    check(events.final_count == 2u, "spoken follow-up final count changed");
    LeaveCriticalSection(&events.lock);
    check(
        WaitForSingleObject(events.idle, 1000u) ==
            WAIT_OBJECT_0,
        "spoken follow-up did not return to IDLE");

    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.transcript);
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_OK,
        "repeated follow-up request failed");
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "repeated follow-up did not enter LISTENING");
    (void)ResetEvent(events.listening);
    lifecycle_feed_pcm(
        voice,
        &empty_candidate_pcm,
        silence,
        0u,
        0u);
    lifecycle_feed_silence(voice, silence, 50u);
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "empty STT candidate did not activate VAD");
    Sleep(1000u);
    (void)ResetEvent(events.listening);
    lifecycle_feed_pcm(
        voice,
        &follow_up_pcm,
        silence,
        0u,
        0u);
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "recognized follow-up did not replace empty STT candidate");
    lifecycle_feed_silence(voice, silence, 63u);
    check(
        WaitForSingleObject(events.transcript, 30000u) ==
            WAIT_OBJECT_0,
        "repeated follow-up transcript timed out");
    EnterCriticalSection(&events.lock);
    check(
        events.final_text != NULL &&
            strcmp(
                events.final_text,
                "برگرد خب که چی می‌گویی") == 0,
        "repeated follow-up transcript differs from golden");
    check(events.final_count == 3u, "repeated follow-up final count changed");
    LeaveCriticalSection(&events.lock);
    check(
        WaitForSingleObject(events.idle, 1000u) ==
            WAIT_OBJECT_0,
        "repeated follow-up did not return to IDLE");

    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.transcript);
    follow_up_started = GetTickCount64();
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_OK,
        "silent follow-up request failed");
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "silent follow-up did not enter LISTENING");
    (void)ResetEvent(events.listening);
    Sleep(1800u);
    lifecycle_feed_pcm(
        voice,
        &empty_candidate_pcm,
        silence,
        0u,
        0u);
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "silent empty STT candidate did not activate VAD");
    check(
        WaitForSingleObject(events.idle, 5000u) ==
            WAIT_OBJECT_0,
        "silent follow-up did not return to IDLE");
    follow_up_elapsed = GetTickCount64() - follow_up_started;
    check(
        follow_up_elapsed >= 3900u && follow_up_elapsed <= 5000u,
        "silent follow-up did not honor the 4 second window");
    check(
        WaitForSingleObject(events.transcript, 0u) == WAIT_TIMEOUT,
        "silent follow-up emitted a final transcript");

    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.transcript);
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_OK,
        "late follow-up request failed");
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "late follow-up did not enter LISTENING");
    (void)ResetEvent(events.listening);
    Sleep(3200u);
    {
        DioAudioPcm prefix = follow_up_pcm;
        prefix.sample_count = candidate_samples;
        lifecycle_feed_pcm(
            voice,
            &prefix,
            silence,
            0u,
            0u);
    }
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "late speech prefix did not activate VAD");
    check(
        WaitForSingleObject(events.idle, 1000u) == WAIT_TIMEOUT,
        "late speech was cut at the original deadline");
    {
        DioAudioPcm remainder = follow_up_pcm;
        remainder.samples += candidate_samples;
        remainder.sample_count -= candidate_samples;
        lifecycle_feed_pcm(
            voice,
            &remainder,
            silence,
            0u,
            0u);
    }
    lifecycle_feed_silence(voice, silence, 63u);
    check(
        WaitForSingleObject(events.transcript, 30000u) ==
            WAIT_OBJECT_0,
        "late recognized follow-up transcript timed out");
    EnterCriticalSection(&events.lock);
    check(
        events.final_text != NULL &&
            strcmp(
                events.final_text,
                "برگرد خب که چی می‌گویی") == 0,
        "late recognized follow-up transcript differs from golden");
    check(events.final_count == 4u, "late follow-up final count changed");
    LeaveCriticalSection(&events.lock);
    check(
        WaitForSingleObject(events.idle, 1000u) == WAIT_OBJECT_0,
        "late recognized follow-up did not return to IDLE");

    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.partial);
    (void)ResetEvent(events.transcript);
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_OK,
        "cancelled follow-up request failed");
    check(
        WaitForSingleObject(events.listening, 1000u) ==
            WAIT_OBJECT_0,
        "cancelled follow-up did not enter LISTENING");
    lifecycle_feed_pcm(voice, &pcm, silence, 0u, 0u);
    check(
        WaitForSingleObject(events.partial, 5000u) ==
            WAIT_OBJECT_0,
        "cancelled follow-up never emitted a partial transcript");
    check(
        dio_voice_cancel(voice) == DIO_VOICE_OK,
        "voice cancel failed");
    check(
        WaitForSingleObject(events.idle, 1000u) ==
            WAIT_OBJECT_0,
        "cancelled follow-up did not return to IDLE");
    check(
        WaitForSingleObject(events.transcript, 200u) == WAIT_TIMEOUT,
        "cancelled follow-up emitted a final transcript");

    check(
        dio_voice_set_paused(voice, true) == DIO_VOICE_OK &&
            dio_voice_set_paused(voice, false) == DIO_VOICE_OK,
        "pause lifecycle failed");
    dio_voice_test_latch_failure(voice);
    (void)printf("lifecycle: failure latched\n");
    EnterCriticalSection(&events.lock);
    check(
        events.error_code == DIO_VOICE_ERROR_INFERENCE,
        "ERROR did not carry a stable inference code");
    LeaveCriticalSection(&events.lock);
    check(
        dio_voice_push_to_talk(voice) == DIO_VOICE_NOT_READY,
        "failed latch allowed push-to-talk");
    check(
        dio_voice_follow_up(voice) == DIO_VOICE_NOT_READY,
        "failed latch allowed follow-up");
    check(
        dio_voice_feed_audio(
            voice,
            silence,
            DIO_VOICE_FRAME_SAMPLES) == DIO_VOICE_NOT_READY,
        "failed latch accepted more inference audio");
    for (index = 0u; index < 8u; ++index) {
        check(
            dio_voice_speak(
                voice,
                5000u + index,
                "test",
                4u) == DIO_VOICE_OK,
            "close-contract speech enqueue failed");
    }
    (void)printf("lifecycle: closing with queued speech\n");
close_voice:
    dio_voice_close(voice);
    voice = NULL;
    (void)printf("lifecycle: close returned\n");
    EnterCriticalSection(&events.lock);
    for (index = 0u; index < 8u; ++index) {
        check(
            events.close_speech_counts[index] == 1u,
            "accepted speech did not receive exactly one terminal event");
    }
    LeaveCriticalSection(&events.lock);
    (void)ResetEvent(events.ready);
    (void)ResetEvent(events.listening);
    (void)ResetEvent(events.idle);
    (void)ResetEvent(events.transcript);
    config.follow_up_ms = 0u;
    config.command_start_timeout_ms = 320u;
    config.command_max_ms = 640u;
    check(
        dio_voice_open(&config, &voice) == DIO_VOICE_OK &&
            dio_voice_start(voice) == DIO_VOICE_OK,
        "disabled follow-up voice open failed");
    if (voice != NULL) {
        check(
            WaitForSingleObject(events.ready, 1000u) ==
                WAIT_OBJECT_0,
            "disabled follow-up voice was not ready");
        check(
            WaitForSingleObject(events.idle, 1000u) ==
                WAIT_OBJECT_0,
            "short-limit voice did not enter IDLE");
        (void)ResetEvent(events.listening);
        (void)ResetEvent(events.idle);
        check(
            dio_voice_push_to_talk(voice) == DIO_VOICE_OK,
            "start-timeout push-to-talk failed");
        check(
            WaitForSingleObject(events.listening, 1000u) ==
                WAIT_OBJECT_0,
            "start-timeout did not enter LISTENING");
        lifecycle_feed_silence(voice, silence, 10u);
        check(
            WaitForSingleObject(events.idle, 1000u) ==
                WAIT_OBJECT_0,
            "start-timeout did not return to IDLE");
        check(
            WaitForSingleObject(events.transcript, 0u) ==
                WAIT_TIMEOUT,
            "start-timeout emitted a final transcript");
        (void)ResetEvent(events.listening);
        (void)ResetEvent(events.idle);
        (void)ResetEvent(events.transcript);
        check(
            dio_voice_push_to_talk(voice) == DIO_VOICE_OK,
            "max-duration push-to-talk failed");
        check(
            WaitForSingleObject(events.listening, 1000u) ==
                WAIT_OBJECT_0,
            "max-duration did not enter LISTENING");
        {
            DioAudioPcm prefix = pcm;
            prefix.sample_count = 20u * DIO_VOICE_FRAME_SAMPLES;
            lifecycle_feed_pcm(voice, &prefix, silence, 0u, 0u);
        }
        check(
            WaitForSingleObject(events.idle, 1000u) ==
                WAIT_OBJECT_0,
            "max-duration did not return to IDLE");
        check(
            WaitForSingleObject(events.transcript, 1000u) ==
                WAIT_OBJECT_0,
            "max-duration did not finalize captured speech");
        EnterCriticalSection(&events.lock);
        check(events.final_count == 5u, "max-duration final count changed");
        LeaveCriticalSection(&events.lock);
        check(
            dio_voice_follow_up(voice) ==
                DIO_VOICE_NOT_READY,
            "follow-up zero did not disable the API");
        dio_voice_close(voice);
        voice = NULL;
    }
cleanup:
    dio_audio_pcm_free(&empty_candidate_pcm);
    dio_audio_pcm_free(&follow_up_pcm);
    dio_audio_pcm_free(&pcm);
    free(events.final_text);
    if (events.idle != NULL) {
        (void)CloseHandle(events.idle);
    }
    if (events.listening != NULL) {
        (void)CloseHandle(events.listening);
    }
    if (events.speech != NULL) {
        (void)CloseHandle(events.speech);
    }
    if (events.partial != NULL) {
        (void)CloseHandle(events.partial);
    }
    if (events.transcript != NULL) {
        (void)CloseHandle(events.transcript);
    }
    if (events.ready != NULL) {
        (void)CloseHandle(events.ready);
    }
    DeleteCriticalSection(&events.lock);
    free(empty_candidate_wav_path);
    free(follow_up_wav_path);
    free(wav_path);
}

static void test_earcon_metric_if_enabled(void)
{
    wchar_t enabled[2];
    DioEarcon *earcon;
    char error[512];
    unsigned int latency = UINT_MAX;

    if (GetEnvironmentVariableW(
            L"DIO_TEST_AUDIO",
            enabled,
            _countof(enabled)) == 0u) {
        (void)printf(
            "earcon hardware metric: SKIP "
            "(set DIO_TEST_AUDIO=1 to enable)\n");
        return;
    }
    earcon = dio_earcon_open(NULL, error, sizeof(error));
    check(earcon != NULL, error);
    if (earcon == NULL) {
        return;
    }
    {
        const bool played = dio_earcon_play_listening(
            earcon,
            GetTickCount64(),
            &latency,
            error,
            sizeof(error));
        check(played, error);
        if (played) {
            (void)printf("earcon callback start latency: %u ms\n", latency);
            check(latency <= 600u, "earcon start exceeded 600 ms gate");
        }
    }
    {
        latency = UINT_MAX;
        const bool played = dio_earcon_play_processing(
            earcon,
            GetTickCount64(),
            &latency,
            error,
            sizeof(error));
        check(played, error);
        if (played) {
            (void)printf(
                "processing earcon callback start latency: %u ms\n",
                latency);
            check(
                latency <= 600u,
                "processing earcon start exceeded 600 ms gate");
        }
    }
    dio_earcon_close(earcon);
}

int wmain(int argument_count, wchar_t **arguments)
{
    DioOrtRuntime runtime;
    char error[512];

    (void)setvbuf(stdout, NULL, _IONBF, 0u);
    (void)setvbuf(stderr, NULL, _IONBF, 0u);
    if (argument_count != 7) {
        (void)fwprintf(
            stderr,
            L"usage: %ls <onnxruntime.dll> <porcupine-dir> "
            L"<silero.onnx> <libvosk.dll> <vosk-model-dir> "
            L"<fixture-dir>\n",
            arguments[0]);
        return 2;
    }

    test_lifecycle(
        arguments[1],
        arguments[2],
        arguments[3],
        arguments[4],
        arguments[5],
        arguments[6]);
    test_capture_selector();

    if (!dio_ort_runtime_open(
            arguments[1],
            &runtime,
            error,
            sizeof(error))) {
        (void)fprintf(stderr, "FAIL: %s\n", error);
        return 1;
    }

    test_wake(arguments[2], arguments[6]);
    test_silero(&runtime, arguments[3], arguments[6]);
    dio_ort_runtime_close(&runtime);
    test_vosk(arguments[4], arguments[5], arguments[6]);
    test_vad_hysteresis();
    test_earcon_metric_if_enabled();

    if (failures != 0) {
        (void)fprintf(stderr, "%d voice core test(s) failed\n", failures);
        return 1;
    }
    (void)printf("voice core deterministic inference: PASS\n");
    return 0;
}
