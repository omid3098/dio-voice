#ifndef DIO_VOICE_ORT_RUNTIME_H
#define DIO_VOICE_ORT_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <windows.h>

#include "onnxruntime_c_api.h"

typedef struct DioOrtRuntime {
    HMODULE module;
    const OrtApi *api;
    OrtEnv *environment;
    OrtMemoryInfo *cpu_memory;
} DioOrtRuntime;

typedef struct DioOrtSession {
    DioOrtRuntime *runtime;
    OrtSession *handle;
} DioOrtSession;

bool dio_ort_runtime_open(
    const wchar_t *library_path,
    DioOrtRuntime *runtime,
    char *error_text,
    size_t error_text_capacity);

void dio_ort_runtime_close(
    DioOrtRuntime *runtime);

bool dio_ort_session_open(
    DioOrtRuntime *runtime,
    const wchar_t *model_path,
    DioOrtSession *session,
    char *error_text,
    size_t error_text_capacity);

void dio_ort_session_close(
    DioOrtSession *session);

bool dio_ort_tensor_create(
    DioOrtRuntime *runtime,
    void *data,
    size_t data_size,
    const int64_t *shape,
    size_t shape_length,
    ONNXTensorElementDataType type,
    OrtValue **output,
    char *error_text,
    size_t error_text_capacity);

bool dio_ort_run(
    DioOrtSession *session,
    const char *const *input_names,
    const OrtValue *const *inputs,
    size_t input_count,
    const char *const *output_names,
    size_t output_count,
    OrtValue **outputs,
    char *error_text,
    size_t error_text_capacity);

bool dio_ort_tensor_float_data(
    DioOrtRuntime *runtime,
    OrtValue *value,
    float **data,
    size_t *element_count,
    char *error_text,
    size_t error_text_capacity);

void dio_ort_value_release(
    DioOrtRuntime *runtime,
    OrtValue *value);

#endif
