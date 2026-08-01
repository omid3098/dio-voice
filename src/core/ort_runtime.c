#include "ort_runtime.h"

#include <string.h>

typedef const OrtApiBase *(ORT_API_CALL *DioOrtGetApiBaseFunction)(void);

static void dio_ort_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(
        destination,
        capacity,
        message != NULL ? message : "ONNX Runtime failure",
        _TRUNCATE);
}

static bool dio_ort_status_ok(
    const OrtApi *api,
    OrtStatus *status,
    char *error_text,
    size_t error_text_capacity)
{
    if (status == NULL) {
        return true;
    }
    dio_ort_error(
        error_text,
        error_text_capacity,
        api->GetErrorMessage(status));
    api->ReleaseStatus(status);
    return false;
}

bool dio_ort_runtime_open(
    const wchar_t *library_path,
    DioOrtRuntime *runtime,
    char *error_text,
    size_t error_text_capacity)
{
    FARPROC procedure;
    DioOrtGetApiBaseFunction get_api_base = NULL;
    const OrtApiBase *base;

    if (library_path == NULL || runtime == NULL) {
        dio_ort_error(error_text, error_text_capacity, "invalid ONNX runtime arguments");
        return false;
    }
    (void)memset(runtime, 0, sizeof(*runtime));

    runtime->module = LoadLibraryExW(
        library_path,
        NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (runtime->module == NULL) {
        dio_ort_error(error_text, error_text_capacity, "could not load onnxruntime.dll");
        return false;
    }

    procedure = GetProcAddress(runtime->module, "OrtGetApiBase");
    if (procedure == NULL) {
        dio_ort_error(error_text, error_text_capacity, "OrtGetApiBase export is missing");
        dio_ort_runtime_close(runtime);
        return false;
    }
    _Static_assert(
        sizeof(get_api_base) == sizeof(procedure),
        "function and data pointers must have equal size on Windows");
    (void)memcpy(&get_api_base, &procedure, sizeof(get_api_base));
    base = get_api_base();
    if (base == NULL) {
        dio_ort_error(error_text, error_text_capacity, "OrtGetApiBase returned NULL");
        dio_ort_runtime_close(runtime);
        return false;
    }
    runtime->api = base->GetApi(ORT_API_VERSION);
    if (runtime->api == NULL) {
        dio_ort_error(error_text, error_text_capacity, "unsupported ONNX Runtime API version");
        dio_ort_runtime_close(runtime);
        return false;
    }

    if (!dio_ort_status_ok(
            runtime->api,
            runtime->api->CreateEnv(
                ORT_LOGGING_LEVEL_ERROR,
                "dio-voice",
                &runtime->environment),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->CreateCpuMemoryInfo(
                OrtArenaAllocator,
                OrtMemTypeDefault,
                &runtime->cpu_memory),
            error_text,
            error_text_capacity)) {
        dio_ort_runtime_close(runtime);
        return false;
    }

    dio_ort_error(error_text, error_text_capacity, "");
    return true;
}

void dio_ort_runtime_close(DioOrtRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->api != NULL) {
        if (runtime->cpu_memory != NULL) {
            runtime->api->ReleaseMemoryInfo(runtime->cpu_memory);
        }
        if (runtime->environment != NULL) {
            runtime->api->ReleaseEnv(runtime->environment);
        }
    }
    if (runtime->module != NULL) {
        (void)FreeLibrary(runtime->module);
    }
    (void)memset(runtime, 0, sizeof(*runtime));
}

bool dio_ort_session_open(
    DioOrtRuntime *runtime,
    const wchar_t *model_path,
    DioOrtSession *session,
    char *error_text,
    size_t error_text_capacity)
{
    OrtSessionOptions *options = NULL;
    bool succeeded = false;

    if (runtime == NULL || runtime->api == NULL ||
        model_path == NULL || session == NULL) {
        dio_ort_error(error_text, error_text_capacity, "invalid ONNX session arguments");
        return false;
    }
    (void)memset(session, 0, sizeof(*session));
    session->runtime = runtime;

    if (!dio_ort_status_ok(
            runtime->api,
            runtime->api->CreateSessionOptions(&options),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->SetSessionExecutionMode(options, ORT_SEQUENTIAL),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->SetIntraOpNumThreads(options, 1),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->SetInterOpNumThreads(options, 1),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->SetSessionGraphOptimizationLevel(
                options,
                ORT_ENABLE_ALL),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->CreateSession(
                runtime->environment,
                model_path,
                options,
                &session->handle),
            error_text,
            error_text_capacity)) {
        goto cleanup;
    }
    succeeded = true;
    dio_ort_error(error_text, error_text_capacity, "");

cleanup:
    if (options != NULL) {
        runtime->api->ReleaseSessionOptions(options);
    }
    if (!succeeded) {
        dio_ort_session_close(session);
    }
    return succeeded;
}

void dio_ort_session_close(DioOrtSession *session)
{
    if (session == NULL) {
        return;
    }
    if (session->runtime != NULL &&
        session->runtime->api != NULL &&
        session->handle != NULL) {
        session->runtime->api->ReleaseSession(session->handle);
    }
    (void)memset(session, 0, sizeof(*session));
}

bool dio_ort_tensor_create(
    DioOrtRuntime *runtime,
    void *data,
    size_t data_size,
    const int64_t *shape,
    size_t shape_length,
    ONNXTensorElementDataType type,
    OrtValue **output,
    char *error_text,
    size_t error_text_capacity)
{
    if (runtime == NULL || runtime->api == NULL || data == NULL ||
        data_size == 0u ||
        (shape == NULL && shape_length != 0u) || output == NULL) {
        dio_ort_error(error_text, error_text_capacity, "invalid ONNX tensor arguments");
        return false;
    }
    *output = NULL;
    return dio_ort_status_ok(
        runtime->api,
        runtime->api->CreateTensorWithDataAsOrtValue(
            runtime->cpu_memory,
            data,
            data_size,
            shape,
            shape_length,
            type,
            output),
        error_text,
        error_text_capacity);
}

bool dio_ort_run(
    DioOrtSession *session,
    const char *const *input_names,
    const OrtValue *const *inputs,
    size_t input_count,
    const char *const *output_names,
    size_t output_count,
    OrtValue **outputs,
    char *error_text,
    size_t error_text_capacity)
{
    if (session == NULL || session->runtime == NULL ||
        session->runtime->api == NULL || session->handle == NULL ||
        input_names == NULL || inputs == NULL || input_count == 0u ||
        output_names == NULL || output_count == 0u || outputs == NULL) {
        dio_ort_error(error_text, error_text_capacity, "invalid ONNX run arguments");
        return false;
    }
    return dio_ort_status_ok(
        session->runtime->api,
        session->runtime->api->Run(
            session->handle,
            NULL,
            input_names,
            inputs,
            input_count,
            output_names,
            output_count,
            outputs),
        error_text,
        error_text_capacity);
}

bool dio_ort_tensor_float_data(
    DioOrtRuntime *runtime,
    OrtValue *value,
    float **data,
    size_t *element_count,
    char *error_text,
    size_t error_text_capacity)
{
    OrtTensorTypeAndShapeInfo *shape = NULL;
    void *raw_data = NULL;
    bool succeeded = false;

    if (runtime == NULL || runtime->api == NULL || value == NULL ||
        data == NULL || element_count == NULL) {
        dio_ort_error(error_text, error_text_capacity, "invalid ONNX output arguments");
        return false;
    }
    *data = NULL;
    *element_count = 0u;

    if (!dio_ort_status_ok(
            runtime->api,
            runtime->api->GetTensorTypeAndShape(value, &shape),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->GetTensorShapeElementCount(shape, element_count),
            error_text,
            error_text_capacity) ||
        !dio_ort_status_ok(
            runtime->api,
            runtime->api->GetTensorMutableData(value, &raw_data),
            error_text,
            error_text_capacity)) {
        goto cleanup;
    }

    *data = (float *)raw_data;
    succeeded = true;

cleanup:
    if (shape != NULL) {
        runtime->api->ReleaseTensorTypeAndShapeInfo(shape);
    }
    return succeeded;
}

void dio_ort_value_release(DioOrtRuntime *runtime, OrtValue *value)
{
    if (runtime != NULL && runtime->api != NULL && value != NULL) {
        runtime->api->ReleaseValue(value);
    }
}
