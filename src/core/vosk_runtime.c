#include "vosk_runtime.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

static void dio_vosk_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(destination, capacity, message, _TRUNCATE);
}

static bool dio_vosk_resolve(
    HMODULE module,
    const char *name,
    void *destination,
    size_t destination_size,
    char *error_text,
    size_t error_text_capacity)
{
    FARPROC procedure = GetProcAddress(module, name);

    if (procedure == NULL || destination_size != sizeof(procedure)) {
        dio_vosk_error(error_text, error_text_capacity, "libvosk export is missing");
        return false;
    }
    (void)memcpy(destination, &procedure, destination_size);
    return true;
}

static char *dio_vosk_wide_to_utf8(const wchar_t *text)
{
    int required;
    char *utf8;

    required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        NULL,
        0,
        NULL,
        NULL);
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

static wchar_t *dio_vosk_native_library_path(const wchar_t *path)
{
    DWORD required = GetFullPathNameW(path, 0u, NULL, NULL);
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

bool dio_vosk_runtime_open(
    const wchar_t *library_path,
    const wchar_t *model_directory,
    DioVoskRuntime *runtime,
    char *error_text,
    size_t error_text_capacity)
{
    char *model_utf8 = NULL;
    wchar_t *native_library_path = NULL;

    if (library_path == NULL || model_directory == NULL || runtime == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "invalid Vosk arguments");
        return false;
    }
    (void)memset(runtime, 0, sizeof(*runtime));
    native_library_path = dio_vosk_native_library_path(library_path);
    if (native_library_path == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "could not normalize libvosk path");
        return false;
    }
    runtime->module = LoadLibraryExW(
        native_library_path,
        NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    free(native_library_path);
    if (runtime->module == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "could not load libvosk.dll");
        return false;
    }

#define DIO_VOSK_RESOLVE(field, export_name)                                \
    if (!dio_vosk_resolve(                                                 \
            runtime->module,                                               \
            export_name,                                                   \
            &runtime->field,                                               \
            sizeof(runtime->field),                                        \
            error_text,                                                    \
            error_text_capacity)) {                                        \
        dio_vosk_runtime_close(runtime);                                    \
        return false;                                                       \
    }

    DIO_VOSK_RESOLVE(model_new, "vosk_model_new");
    DIO_VOSK_RESOLVE(model_free, "vosk_model_free");
    DIO_VOSK_RESOLVE(recognizer_new, "vosk_recognizer_new");
    DIO_VOSK_RESOLVE(
        accept_waveform_s,
        "vosk_recognizer_accept_waveform_s");
    DIO_VOSK_RESOLVE(result, "vosk_recognizer_result");
    DIO_VOSK_RESOLVE(partial_result, "vosk_recognizer_partial_result");
    DIO_VOSK_RESOLVE(final_result, "vosk_recognizer_final_result");
    DIO_VOSK_RESOLVE(recognizer_free, "vosk_recognizer_free");
    DIO_VOSK_RESOLVE(set_log_level, "vosk_set_log_level");

#undef DIO_VOSK_RESOLVE

    model_utf8 = dio_vosk_wide_to_utf8(model_directory);
    if (model_utf8 == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "could not encode Vosk model path");
        dio_vosk_runtime_close(runtime);
        return false;
    }
    runtime->set_log_level(-1);
    runtime->model = runtime->model_new(model_utf8);
    free(model_utf8);
    if (runtime->model == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "could not load Vosk model");
        dio_vosk_runtime_close(runtime);
        return false;
    }
    dio_vosk_error(error_text, error_text_capacity, "");
    return true;
}

void dio_vosk_runtime_close(DioVoskRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->model != NULL && runtime->model_free != NULL) {
        runtime->model_free(runtime->model);
    }
    if (runtime->module != NULL) {
        (void)FreeLibrary(runtime->module);
    }
    (void)memset(runtime, 0, sizeof(*runtime));
}

static char *dio_vosk_json_text(
    const char *json,
    const char *field,
    char *error_text,
    size_t error_text_capacity)
{
    yyjson_doc *document;
    yyjson_val *root;
    yyjson_val *value;
    const char *source;
    size_t length;
    char *copy;

    if (json == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "Vosk returned NULL JSON");
        return NULL;
    }
    document = yyjson_read(json, strlen(json), 0u);
    if (document == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "Vosk returned invalid JSON");
        return NULL;
    }
    root = yyjson_doc_get_root(document);
    value = yyjson_is_obj(root) ? yyjson_obj_get(root, field) : NULL;
    source = yyjson_is_str(value) ? yyjson_get_str(value) : "";
    length = strlen(source);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "out of memory copying Vosk text");
    } else {
        (void)memcpy(copy, source, length + 1u);
    }
    yyjson_doc_free(document);
    return copy;
}

static bool dio_vosk_append_finalized(
    DioVoskRecognizer *recognizer,
    const char *text,
    char *error_text,
    size_t error_text_capacity)
{
    const size_t text_length = strlen(text);
    const size_t separator =
        recognizer->finalized_length != 0u && text_length != 0u ? 1u : 0u;
    const size_t new_length =
        recognizer->finalized_length + separator + text_length;
    char *resized;

    if (text_length == 0u) {
        return true;
    }
    if (new_length < recognizer->finalized_length) {
        dio_vosk_error(error_text, error_text_capacity, "Vosk transcript is too large");
        return false;
    }
    resized = (char *)realloc(recognizer->finalized_text, new_length + 1u);
    if (resized == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "out of memory growing transcript");
        return false;
    }
    recognizer->finalized_text = resized;
    if (separator != 0u) {
        resized[recognizer->finalized_length] = ' ';
    }
    (void)memcpy(
        resized + recognizer->finalized_length + separator,
        text,
        text_length + 1u);
    recognizer->finalized_length = new_length;
    return true;
}

static char *dio_vosk_snapshot(
    const DioVoskRecognizer *recognizer,
    const char *partial,
    char *error_text,
    size_t error_text_capacity)
{
    const size_t partial_length = strlen(partial);
    const size_t separator =
        recognizer->finalized_length != 0u && partial_length != 0u ? 1u : 0u;
    const size_t length =
        recognizer->finalized_length + separator + partial_length;
    char *snapshot = (char *)malloc(length + 1u);

    if (snapshot == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "out of memory copying transcript");
        return NULL;
    }
    if (recognizer->finalized_length != 0u) {
        (void)memcpy(
            snapshot,
            recognizer->finalized_text,
            recognizer->finalized_length);
    }
    if (separator != 0u) {
        snapshot[recognizer->finalized_length] = ' ';
    }
    if (partial_length != 0u) {
        (void)memcpy(
            snapshot + recognizer->finalized_length + separator,
            partial,
            partial_length);
    }
    snapshot[length] = '\0';
    return snapshot;
}

bool dio_vosk_recognizer_open(
    DioVoskRuntime *runtime,
    DioVoskRecognizer *recognizer,
    char *error_text,
    size_t error_text_capacity)
{
    if (runtime == NULL || runtime->model == NULL || recognizer == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "invalid Vosk recognizer arguments");
        return false;
    }
    (void)memset(recognizer, 0, sizeof(*recognizer));
    recognizer->runtime = runtime;
    recognizer->handle = runtime->recognizer_new(runtime->model, 16000.0f);
    if (recognizer->handle == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "could not create Vosk recognizer");
        dio_vosk_recognizer_close(recognizer);
        return false;
    }
    return true;
}

bool dio_vosk_recognizer_feed(
    DioVoskRecognizer *recognizer,
    const int16_t *samples,
    size_t sample_count,
    char **text,
    bool *endpoint,
    char *error_text,
    size_t error_text_capacity)
{
    int accept_result;
    const char *json;
    const char *field;
    char *part = NULL;

    if (recognizer == NULL || recognizer->runtime == NULL ||
        recognizer->handle == NULL || samples == NULL ||
        sample_count == 0u || sample_count > INT_MAX ||
        text == NULL || endpoint == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "invalid Vosk feed arguments");
        return false;
    }
    *text = NULL;
    *endpoint = false;
    accept_result = recognizer->runtime->accept_waveform_s(
        recognizer->handle,
        samples,
        (int)sample_count);
    if (accept_result < 0) {
        dio_vosk_error(error_text, error_text_capacity, "Vosk waveform processing failed");
        return false;
    }
    *endpoint = accept_result != 0;
    if (*endpoint) {
        json = recognizer->runtime->result(recognizer->handle);
        field = "text";
    } else {
        json = recognizer->runtime->partial_result(recognizer->handle);
        field = "partial";
    }
    part = dio_vosk_json_text(
        json,
        field,
        error_text,
        error_text_capacity);
    if (part == NULL) {
        return false;
    }
    if (*endpoint &&
        !dio_vosk_append_finalized(
            recognizer,
            part,
            error_text,
            error_text_capacity)) {
        free(part);
        return false;
    }
    *text = dio_vosk_snapshot(
        recognizer,
        *endpoint ? "" : part,
        error_text,
        error_text_capacity);
    free(part);
    return *text != NULL;
}

bool dio_vosk_recognizer_finish(
    DioVoskRecognizer *recognizer,
    char **text,
    char *error_text,
    size_t error_text_capacity)
{
    char *part;

    if (recognizer == NULL || recognizer->runtime == NULL ||
        recognizer->handle == NULL || text == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "invalid Vosk finish arguments");
        return false;
    }
    *text = NULL;
    part = dio_vosk_json_text(
        recognizer->runtime->final_result(recognizer->handle),
        "text",
        error_text,
        error_text_capacity);
    if (part == NULL) {
        return false;
    }
    if (!dio_vosk_append_finalized(
            recognizer,
            part,
            error_text,
            error_text_capacity)) {
        free(part);
        return false;
    }
    free(part);
    *text = dio_vosk_snapshot(recognizer, "", error_text, error_text_capacity);
    return *text != NULL;
}

void dio_vosk_recognizer_close(DioVoskRecognizer *recognizer)
{
    if (recognizer == NULL) {
        return;
    }
    if (recognizer->runtime != NULL &&
        recognizer->runtime->recognizer_free != NULL &&
        recognizer->handle != NULL) {
        recognizer->runtime->recognizer_free(recognizer->handle);
    }
    free(recognizer->finalized_text);
    (void)memset(recognizer, 0, sizeof(*recognizer));
}

bool dio_vosk_transcribe(
    DioVoskRuntime *runtime,
    const int16_t *samples,
    size_t sample_count,
    char **text,
    char *error_text,
    size_t error_text_capacity)
{
    DioVoskRecognizer recognizer;
    size_t offset = 0u;
    bool succeeded = false;

    if (runtime == NULL || samples == NULL || sample_count == 0u ||
        text == NULL) {
        dio_vosk_error(error_text, error_text_capacity, "invalid transcription arguments");
        return false;
    }
    *text = NULL;
    if (!dio_vosk_recognizer_open(
            runtime,
            &recognizer,
            error_text,
            error_text_capacity)) {
        return false;
    }
    while (offset < sample_count) {
        const size_t chunk =
            sample_count - offset < 4000u ? sample_count - offset : 4000u;
        char *snapshot = NULL;
        bool endpoint;

        if (!dio_vosk_recognizer_feed(
                &recognizer,
                samples + offset,
                chunk,
                &snapshot,
                &endpoint,
                error_text,
                error_text_capacity)) {
            goto cleanup;
        }
        free(snapshot);
        offset += chunk;
    }
    if (!dio_vosk_recognizer_finish(
            &recognizer,
            text,
            error_text,
            error_text_capacity)) {
        goto cleanup;
    }
    succeeded = true;

cleanup:
    dio_vosk_recognizer_close(&recognizer);
    return succeeded;
}

void dio_vosk_text_free(char *text)
{
    free(text);
}
