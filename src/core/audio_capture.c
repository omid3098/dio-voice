#include "audio_capture.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

static void dio_capture_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(destination, capacity, message, _TRUNCATE);
}

static wchar_t *dio_capture_utf8_to_wide(const char *text)
{
    int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0);
    wchar_t *wide;

    if (required == 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)required * sizeof(*wide));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            -1,
            wide,
            required) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

bool dio_audio_capture_name_equal(
    const char *device_name,
    const wchar_t *preferred_name)
{
    wchar_t *wide_device;
    bool matches = false;

    if (preferred_name[0] == L'\0') {
        return true;
    }
    wide_device = dio_capture_utf8_to_wide(device_name);
    if (wide_device == NULL) {
        return false;
    }
    matches = CompareStringOrdinal(
        wide_device,
        -1,
        preferred_name,
        -1,
        TRUE) == CSTR_EQUAL;
    free(wide_device);
    return matches;
}

static void dio_capture_data_callback(
    ma_device *device,
    void *output,
    const void *input,
    ma_uint32 frame_count)
{
    DioAudioCapture *capture = (DioAudioCapture *)device->pUserData;

    (void)output;
    if (capture != NULL && capture->callback != NULL && input != NULL) {
        capture->callback(
            capture->callback_context,
            (const int16_t *)input,
            (size_t)frame_count);
    }
}

bool dio_audio_capture_open(
    const wchar_t *preferred_device_name,
    const wchar_t *preferred_device_id,
    DioCaptureCallback callback,
    void *callback_context,
    DioAudioCapture *capture,
    char *error_text,
    size_t error_text_capacity)
{
    ma_backend backend = ma_backend_wasapi;
    ma_device_info *capture_infos = NULL;
    ma_uint32 capture_count = 0u;
    const ma_device_id *selected_id = NULL;
    ma_device_config config;
    ma_uint32 index;
    ma_result result;

    if (callback == NULL || capture == NULL) {
        dio_capture_error(error_text, error_text_capacity, "invalid capture arguments");
        return false;
    }
    (void)memset(capture, 0, sizeof(*capture));
    capture->callback = callback;
    capture->callback_context = callback_context;

    result = ma_context_init(&backend, 1u, NULL, &capture->context);
    if (result != MA_SUCCESS) {
        dio_capture_error(error_text, error_text_capacity, "could not initialize WASAPI");
        return false;
    }
    capture->context_ready = true;

    if ((preferred_device_id != NULL &&
         preferred_device_id[0] != L'\0') ||
        (preferred_device_name != NULL &&
         preferred_device_name[0] != L'\0')) {
        result = ma_context_get_devices(
            &capture->context,
            NULL,
            NULL,
            &capture_infos,
            &capture_count);
        if (result != MA_SUCCESS) {
            dio_capture_error(error_text, error_text_capacity, "could not enumerate microphones");
            dio_audio_capture_close(capture);
            return false;
        }
        for (index = 0u; index < capture_count; ++index) {
            const bool matches =
                preferred_device_id != NULL &&
                    preferred_device_id[0] != L'\0'
                ? wcscmp(
                      capture_infos[index].id.wasapi,
                      preferred_device_id) == 0
                : dio_audio_capture_name_equal(
                      capture_infos[index].name,
                      preferred_device_name);
            if (matches) {
                selected_id = &capture_infos[index].id;
                break;
            }
        }
        if (selected_id == NULL) {
            dio_capture_error(error_text, error_text_capacity, "configured microphone was not found");
            dio_audio_capture_close(capture);
            return false;
        }
    }

    config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = selected_id;
    config.capture.format = ma_format_s16;
    config.capture.channels = 1u;
    config.sampleRate = 16000u;
    config.dataCallback = dio_capture_data_callback;
    config.pUserData = capture;
    config.noPreSilencedOutputBuffer = MA_TRUE;

    result = ma_device_init(&capture->context, &config, &capture->device);
    if (result != MA_SUCCESS) {
        dio_capture_error(error_text, error_text_capacity, "could not open microphone");
        dio_audio_capture_close(capture);
        return false;
    }
    capture->device_ready = true;
    return true;
}

bool dio_audio_capture_start(
    DioAudioCapture *capture,
    char *error_text,
    size_t error_text_capacity)
{
    if (capture == NULL || !capture->device_ready) {
        dio_capture_error(error_text, error_text_capacity, "microphone is not open");
        return false;
    }
    if (ma_device_start(&capture->device) != MA_SUCCESS) {
        dio_capture_error(error_text, error_text_capacity, "could not start microphone");
        return false;
    }
    capture->started = true;
    return true;
}

void dio_audio_capture_close(DioAudioCapture *capture)
{
    if (capture == NULL) {
        return;
    }
    if (capture->device_ready) {
        if (capture->started) {
            (void)ma_device_stop(&capture->device);
        }
        ma_device_uninit(&capture->device);
    }
    if (capture->context_ready) {
        ma_context_uninit(&capture->context);
    }
    (void)memset(capture, 0, sizeof(*capture));
}
