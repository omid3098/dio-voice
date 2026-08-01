#include "wake_onnx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DIO_SILERO_CONTEXT_SAMPLES 64u
#define DIO_PORCUPINE_SUCCESS 0

static void dio_wake_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(destination, capacity, message, _TRUNCATE);
}

static wchar_t *dio_wake_join_path(
    const wchar_t *directory,
    const wchar_t *name)
{
    const size_t directory_length = wcslen(directory);
    const size_t name_length = wcslen(name);
    const bool needs_separator =
        directory_length != 0u &&
        directory[directory_length - 1u] != L'\\' &&
        directory[directory_length - 1u] != L'/';
    const size_t total =
        directory_length + (needs_separator ? 1u : 0u) + name_length + 1u;
    wchar_t *path;

    if (total > SIZE_MAX / sizeof(*path)) {
        return NULL;
    }
    path = (wchar_t *)malloc(total * sizeof(*path));
    if (path == NULL) {
        return NULL;
    }
    (void)wcscpy_s(path, total, directory);
    if (needs_separator) {
        (void)wcscat_s(path, total, L"\\");
    }
    (void)wcscat_s(path, total, name);
    return path;
}

static wchar_t *dio_wake_native_path(const wchar_t *path)
{
    const DWORD required = GetFullPathNameW(path, 0u, NULL, NULL);
    wchar_t *normalized;
    DWORD length;
    size_t index;

    if (required == 0u) {
        return NULL;
    }
    normalized = (wchar_t *)malloc((size_t)required * sizeof(*normalized));
    if (normalized == NULL) {
        return NULL;
    }
    length = GetFullPathNameW(path, required, normalized, NULL);
    if (length == 0u || length >= required) {
        free(normalized);
        return NULL;
    }
    for (index = 0u; normalized[index] != L'\0'; ++index) {
        if (normalized[index] == L'/') {
            normalized[index] = L'\\';
        }
    }
    return normalized;
}

static char *dio_wake_wide_to_utf8(const wchar_t *text)
{
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    char *utf8;

    if (required == 0) {
        return NULL;
    }
    utf8 = (char *)malloc((size_t)required);
    if (utf8 == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            utf8,
            required,
            NULL,
            NULL) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static bool dio_wake_resolve(
    HMODULE module,
    const char *name,
    void *destination,
    size_t destination_size,
    char *error_text,
    size_t error_text_capacity)
{
    const FARPROC procedure = GetProcAddress(module, name);

    if (procedure == NULL || destination_size != sizeof(procedure)) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "Porcupine export is missing");
        return false;
    }
    (void)memcpy(destination, &procedure, destination_size);
    return true;
}

bool dio_wake_open(
    const wchar_t *model_directory,
    float sensitivity,
    DioWakePorcupine *wake,
    char *error_text,
    size_t error_text_capacity)
{
    wchar_t *library_path = NULL;
    wchar_t *native_library_path = NULL;
    wchar_t *model_path = NULL;
    wchar_t *keyword_path = NULL;
    char *model_utf8 = NULL;
    char *keyword_utf8 = NULL;
    const char *keyword_paths[1];
    int status;
    bool succeeded = false;

    if (model_directory == NULL || wake == NULL ||
        sensitivity < 0.0f || sensitivity > 1.0f) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "invalid Porcupine arguments");
        return false;
    }
    (void)memset(wake, 0, sizeof(*wake));
    library_path = dio_wake_join_path(
        model_directory,
        L"libpv_porcupine.dll");
    model_path = dio_wake_join_path(
        model_directory,
        L"porcupine_params.pv");
    keyword_path = dio_wake_join_path(
        model_directory,
        L"alexa_windows.ppn");
    if (library_path == NULL || model_path == NULL || keyword_path == NULL) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "out of memory building Porcupine paths");
        goto cleanup;
    }
    native_library_path = dio_wake_native_path(library_path);
    model_utf8 = dio_wake_wide_to_utf8(model_path);
    keyword_utf8 = dio_wake_wide_to_utf8(keyword_path);
    if (native_library_path == NULL || model_utf8 == NULL ||
        keyword_utf8 == NULL) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "could not normalize Porcupine paths");
        goto cleanup;
    }

    wake->module = LoadLibraryExW(
        native_library_path,
        NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (wake->module == NULL) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "could not load libpv_porcupine.dll");
        goto cleanup;
    }

#define DIO_WAKE_RESOLVE(field, export_name)                               \
    if (!dio_wake_resolve(                                                 \
            wake->module,                                                  \
            export_name,                                                   \
            &wake->field,                                                  \
            sizeof(wake->field),                                           \
            error_text,                                                    \
            error_text_capacity)) {                                        \
        goto cleanup;                                                      \
    }

    DIO_WAKE_RESOLVE(sample_rate, "pv_sample_rate");
    DIO_WAKE_RESOLVE(init, "pv_porcupine_init");
    DIO_WAKE_RESOLVE(destroy, "pv_porcupine_delete");
    DIO_WAKE_RESOLVE(process, "pv_porcupine_process");
    DIO_WAKE_RESOLVE(version, "pv_porcupine_version");
    DIO_WAKE_RESOLVE(frame_length, "pv_porcupine_frame_length");

#undef DIO_WAKE_RESOLVE

    if (wake->sample_rate() != (int)DIO_VOICE_SAMPLE_RATE ||
        wake->frame_length() != (int32_t)DIO_VOICE_FRAME_SAMPLES) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "unsupported Porcupine audio format");
        goto cleanup;
    }

    keyword_paths[0] = keyword_utf8;
    status = wake->init(
        model_utf8,
        1,
        keyword_paths,
        &sensitivity,
        &wake->handle);
    if (status != DIO_PORCUPINE_SUCCESS || wake->handle == NULL) {
        char status_text[96];

        (void)_snprintf_s(
            status_text,
            sizeof(status_text),
            _TRUNCATE,
            "Porcupine initialization failed (%d)",
            status);
        dio_wake_error(error_text, error_text_capacity, status_text);
        goto cleanup;
    }
    dio_wake_error(error_text, error_text_capacity, "");
    succeeded = true;

cleanup:
    free(keyword_utf8);
    free(model_utf8);
    free(keyword_path);
    free(model_path);
    free(native_library_path);
    free(library_path);
    if (!succeeded) {
        dio_wake_close(wake);
    }
    return succeeded;
}

bool dio_wake_feed(
    DioWakePorcupine *wake,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES],
    bool *triggered,
    char *error_text,
    size_t error_text_capacity)
{
    int32_t keyword_index = -1;
    int status;

    if (wake == NULL || wake->handle == NULL || wake->process == NULL ||
        samples == NULL || triggered == NULL) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "invalid Porcupine inference arguments");
        return false;
    }
    status = wake->process(wake->handle, samples, &keyword_index);
    if (status != DIO_PORCUPINE_SUCCESS) {
        dio_wake_error(
            error_text,
            error_text_capacity,
            "Porcupine inference failed");
        return false;
    }
    *triggered = keyword_index >= 0;
    return true;
}

const char *dio_wake_version(const DioWakePorcupine *wake)
{
    return wake != NULL && wake->version != NULL ? wake->version() : NULL;
}

void dio_wake_close(DioWakePorcupine *wake)
{
    if (wake == NULL) {
        return;
    }
    if (wake->handle != NULL && wake->destroy != NULL) {
        wake->destroy(wake->handle);
    }
    if (wake->module != NULL) {
        (void)FreeLibrary(wake->module);
    }
    (void)memset(wake, 0, sizeof(*wake));
}

void dio_silero_reset(DioSileroVad *vad)
{
    if (vad == NULL) {
        return;
    }
    (void)memset(vad->state, 0, sizeof(vad->state));
    (void)memset(vad->context, 0, sizeof(vad->context));
}

bool dio_silero_open(
    DioOrtRuntime *runtime,
    const wchar_t *model_path,
    DioSileroVad *vad,
    char *error_text,
    size_t error_text_capacity)
{
    if (runtime == NULL || model_path == NULL || vad == NULL) {
        dio_wake_error(error_text, error_text_capacity, "invalid Silero arguments");
        return false;
    }
    (void)memset(vad, 0, sizeof(*vad));
    vad->runtime = runtime;
    if (!dio_ort_session_open(
            runtime,
            model_path,
            &vad->session,
            error_text,
            error_text_capacity)) {
        dio_silero_close(vad);
        return false;
    }
    dio_silero_reset(vad);
    return true;
}

bool dio_silero_probability(
    DioSileroVad *vad,
    const int16_t samples[DIO_VOICE_FRAME_SAMPLES],
    float *probability,
    char *error_text,
    size_t error_text_capacity)
{
    float input[DIO_SILERO_CONTEXT_SAMPLES + DIO_VOICE_FRAME_SAMPLES];
    int64_t sample_rate = DIO_VOICE_SAMPLE_RATE;
    int64_t input_shape[] = {
        1,
        DIO_SILERO_CONTEXT_SAMPLES + DIO_VOICE_FRAME_SAMPLES};
    int64_t state_shape[] = {2, 1, 128};
    OrtValue *input_values[3] = {NULL, NULL, NULL};
    const OrtValue *const_inputs[3];
    OrtValue *outputs[2] = {NULL, NULL};
    const char *input_names[] = {"input", "state", "sr"};
    const char *output_names[] = {"output", "stateN"};
    float *probability_data = NULL;
    float *state_data = NULL;
    size_t probability_count = 0u;
    size_t state_count = 0u;
    size_t index;
    bool succeeded = false;

    if (vad == NULL || vad->runtime == NULL || samples == NULL ||
        probability == NULL) {
        dio_wake_error(error_text, error_text_capacity, "invalid Silero inference arguments");
        return false;
    }

    for (index = 0u; index < DIO_SILERO_CONTEXT_SAMPLES; ++index) {
        input[index] = vad->context[index];
    }
    for (index = 0u; index < DIO_VOICE_FRAME_SAMPLES; ++index) {
        input[DIO_SILERO_CONTEXT_SAMPLES + index] =
            (float)samples[index] / 32768.0f;
    }

    if (!dio_ort_tensor_create(
            vad->runtime,
            input,
            sizeof(input),
            input_shape,
            _countof(input_shape),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &input_values[0],
            error_text,
            error_text_capacity) ||
        !dio_ort_tensor_create(
            vad->runtime,
            vad->state,
            sizeof(vad->state),
            state_shape,
            _countof(state_shape),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            &input_values[1],
            error_text,
            error_text_capacity) ||
        !dio_ort_tensor_create(
            vad->runtime,
            &sample_rate,
            sizeof(sample_rate),
            NULL,
            0u,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
            &input_values[2],
            error_text,
            error_text_capacity)) {
        goto cleanup;
    }
    for (index = 0u; index < _countof(input_values); ++index) {
        const_inputs[index] = input_values[index];
    }
    if (!dio_ort_run(
            &vad->session,
            input_names,
            const_inputs,
            _countof(const_inputs),
            output_names,
            _countof(output_names),
            outputs,
            error_text,
            error_text_capacity) ||
        !dio_ort_tensor_float_data(
            vad->runtime,
            outputs[0],
            &probability_data,
            &probability_count,
            error_text,
            error_text_capacity) ||
        !dio_ort_tensor_float_data(
            vad->runtime,
            outputs[1],
            &state_data,
            &state_count,
            error_text,
            error_text_capacity) ||
        probability_count != 1u ||
        state_count != DIO_SILERO_STATE_VALUES) {
        if ((probability_count != 0u && probability_count != 1u) ||
            (state_count != 0u && state_count != DIO_SILERO_STATE_VALUES)) {
            dio_wake_error(error_text, error_text_capacity, "unexpected Silero output shape");
        }
        goto cleanup;
    }

    *probability = probability_data[0];
    (void)memcpy(vad->state, state_data, sizeof(vad->state));
    (void)memcpy(
        vad->context,
        input + DIO_VOICE_FRAME_SAMPLES,
        sizeof(vad->context));
    succeeded = true;

cleanup:
    for (index = 0u; index < _countof(outputs); ++index) {
        dio_ort_value_release(vad->runtime, outputs[index]);
    }
    for (index = 0u; index < _countof(input_values); ++index) {
        dio_ort_value_release(vad->runtime, input_values[index]);
    }
    return succeeded;
}

const float *dio_silero_state_snapshot(const DioSileroVad *vad)
{
    return vad != NULL ? vad->state : NULL;
}

void dio_silero_close(DioSileroVad *vad)
{
    if (vad == NULL) {
        return;
    }
    dio_ort_session_close(&vad->session);
    (void)memset(vad, 0, sizeof(*vad));
}
