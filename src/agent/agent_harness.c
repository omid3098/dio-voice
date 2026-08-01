#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <wchar.h>

#include <dio_voice/agent.h>

#include "yyjson.h"

#define DIO_HARNESS_HEADER_SIZE 36u
#define DIO_HARNESS_MAX_PAYLOAD (1024u * 1024u)
#define DIO_HARNESS_PATH_CAP 32768u
#define DIO_HARNESS_WRITE_TIMEOUT_MS 5000u
#define DIO_HARNESS_CONTROL_FRAME 1u
#define DIO_HARNESS_OUTPUT_PCM16_FRAME 3u
#define DIO_HARNESS_OUTPUT_SAMPLE_RATE 24000u

typedef enum DioAgentState {
    DIO_AGENT_STATE_STARTING = 0,
    DIO_AGENT_STATE_READY,
    DIO_AGENT_STATE_ACTIVE,
    DIO_AGENT_STATE_FAILED,
    DIO_AGENT_STATE_CLOSING
} DioAgentState;

struct DioAgent {
    CRITICAL_SECTION state_lock;
    CRITICAL_SECTION callback_lock;
    CRITICAL_SECTION io_lock;
    HANDLE pipe;
    HANDLE process;
    HANDLE job;
    HANDLE reader_thread;
    DWORD process_id;
    DioAgentEventCallback callback;
    void *callback_context;
    DioAgentState state;
    volatile LONG closing;
    bool transport_error_emitted;
    bool accepted;
    bool cancel_requested;
    uint64_t generation;
    uint64_t next_turn_id;
    uint64_t active_turn_id;
    unsigned int active_audio_sample_rate;
    uint32_t next_sequence;
    uint32_t last_remote_sequence;
    char *hello_json;
    size_t hello_json_length;
};

static bool dio_is_closing(const DioAgent *agent) {
    return agent == NULL ||
        InterlockedCompareExchange(
            (volatile LONG *)&agent->closing,
            0,
            0) != 0;
}

static bool dio_pipe_name(
    wchar_t *pipe_name,
    size_t pipe_name_capacity) {
    static const wchar_t digits[] = L"0123456789abcdef";
    unsigned char random[16];
    wchar_t suffix[33];
    if (BCryptGenRandom(
            NULL,
            random,
            (ULONG)sizeof(random),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return false;
    }
    for (size_t index = 0u; index < sizeof(random); ++index) {
        suffix[index * 2u] = digits[random[index] >> 4u];
        suffix[index * 2u + 1u] = digits[random[index] & 0x0fu];
    }
    suffix[_countof(suffix) - 1u] = L'\0';
    SecureZeroMemory(random, sizeof(random));
    return SUCCEEDED(StringCchPrintfW(
        pipe_name,
        pipe_name_capacity,
        L"\\\\.\\pipe\\dio-voice-%ls",
        suffix));
}

static bool dio_pipe_security(
    SECURITY_ATTRIBUTES *attributes,
    PSECURITY_DESCRIPTOR *descriptor) {
    HANDLE token = NULL;
    TOKEN_GROUPS *groups = NULL;
    LPWSTR sid = NULL;
    DWORD required = 0u;
    wchar_t sddl[256];
    bool valid = false;

    *descriptor = NULL;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token)) {
        goto cleanup;
    }
    (void)GetTokenInformation(
        token,
        TokenGroups,
        NULL,
        0u,
        &required);
    if (required == 0u) {
        goto cleanup;
    }
    groups = (TOKEN_GROUPS *)malloc(required);
    if (groups == NULL ||
        !GetTokenInformation(
            token,
            TokenGroups,
            groups,
            required,
            &required)) {
        goto cleanup;
    }
    PSID logon_sid = NULL;
    for (DWORD index = 0u; index < groups->GroupCount; ++index) {
        if ((groups->Groups[index].Attributes & SE_GROUP_LOGON_ID) ==
            SE_GROUP_LOGON_ID) {
            logon_sid = groups->Groups[index].Sid;
            break;
        }
    }
    if (logon_sid == NULL ||
        !ConvertSidToStringSidW(logon_sid, &sid) ||
        FAILED(StringCchPrintfW(
            sddl,
            _countof(sddl),
            L"D:P(A;;GA;;;%ls)",
            sid)) ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl,
            SDDL_REVISION_1,
            descriptor,
            NULL)) {
        goto cleanup;
    }
    attributes->nLength = sizeof(*attributes);
    attributes->lpSecurityDescriptor = *descriptor;
    attributes->bInheritHandle = FALSE;
    valid = true;

cleanup:
    if (sid != NULL) {
        (void)LocalFree(sid);
    }
    free(groups);
    if (token != NULL) {
        (void)CloseHandle(token);
    }
    return valid;
}

static void dio_emit(
    DioAgent *agent,
    DioAgentEventType type,
    uint64_t turn_id,
    const char *text,
    size_t text_length,
    DWORD system_error,
    bool cancelled) {
    if (agent == NULL || agent->callback == NULL || dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = type,
        .turn_id = turn_id,
        .text = text,
        .text_length = text != NULL ? text_length : 0u,
        .system_error = system_error,
        .cancelled = cancelled};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_emit_provider_error(
    DioAgent *agent,
    uint64_t turn_id,
    unsigned int http_status,
    const char *message) {
    char display[8192];
    const char *detail = message != NULL && message[0] != '\0'
        ? message
        : "Provider request failed.";
    const int length = _snprintf_s(
        display,
        sizeof(display),
        _TRUNCATE,
        "HTTP %u: %s",
        http_status,
        detail);
    if (length <= 0 || agent == NULL || agent->callback == NULL ||
        dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_ERROR,
        .turn_id = turn_id,
        .text = display,
        .text_length = (size_t)length,
        .http_status = http_status};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
    SecureZeroMemory(display, sizeof(display));
}

static void dio_emit_ready(
    DioAgent *agent,
    bool provider_configured) {
    if (agent == NULL || agent->callback == NULL || dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_READY,
        .provider_configured = provider_configured};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_emit_mcp_status(
    DioAgent *agent,
    uint64_t turn_id,
    unsigned int configured,
    unsigned int available,
    unsigned int tools) {
    if (agent == NULL || agent->callback == NULL || dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_MCP_STATUS,
        .turn_id = turn_id,
        .mcp_configured = configured,
        .mcp_available = available,
        .mcp_tools = tools};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_emit_tool_approval(
    DioAgent *agent,
    uint64_t turn_id,
    uint64_t request_id,
    const char *server_name,
    const char *tool_name,
    const char *arguments_json,
    size_t arguments_length) {
    if (agent == NULL || agent->callback == NULL || dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_TOOL_APPROVAL_REQUIRED,
        .turn_id = turn_id,
        .request_id = request_id,
        .server_name = server_name,
        .tool_name = tool_name,
        .arguments_json = arguments_json,
        .arguments_length = arguments_length};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_emit_pcm16(
    DioAgent *agent,
    uint64_t turn_id,
    const int16_t *samples,
    size_t sample_count) {
    if (agent == NULL ||
        agent->callback == NULL ||
        samples == NULL ||
        sample_count == 0u ||
        dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_AUDIO_PCM16,
        .turn_id = turn_id,
        .pcm16 = samples,
        .sample_count = sample_count,
        .sample_rate = DIO_HARNESS_OUTPUT_SAMPLE_RATE};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_emit_accepted(
    DioAgent *agent,
    uint64_t turn_id,
    unsigned int sample_rate) {
    if (agent == NULL ||
        agent->callback == NULL ||
        dio_is_closing(agent)) {
        return;
    }
    const DioAgentEvent event = {
        .type = DIO_AGENT_EVENT_ACCEPTED,
        .turn_id = turn_id,
        .sample_rate = sample_rate};
    EnterCriticalSection(&agent->callback_lock);
    if (!dio_is_closing(agent)) {
        agent->callback(agent->callback_context, &event);
    }
    LeaveCriticalSection(&agent->callback_lock);
}

static void dio_transport_failure(
    DioAgent *agent,
    DWORD system_error) {
    uint64_t turn_id = 0u;
    bool emit = false;

    if (agent == NULL || dio_is_closing(agent)) {
        return;
    }
    EnterCriticalSection(&agent->state_lock);
    if (agent->state != DIO_AGENT_STATE_FAILED &&
        agent->state != DIO_AGENT_STATE_CLOSING) {
        turn_id = agent->active_turn_id;
        agent->state = DIO_AGENT_STATE_FAILED;
        if (!agent->transport_error_emitted) {
            agent->transport_error_emitted = true;
            emit = true;
        }
    }
    LeaveCriticalSection(&agent->state_lock);
    if (agent->job != NULL) {
        (void)TerminateJobObject(
            agent->job,
            system_error != ERROR_SUCCESS
                ? system_error
                : ERROR_INVALID_DATA);
    }
    if (emit) {
        static const char message[] = "Dio Harness transport failed.";
        dio_emit(
            agent,
            DIO_AGENT_EVENT_ERROR,
            turn_id,
            message,
            sizeof(message) - 1u,
            system_error != ERROR_SUCCESS
                ? system_error
                : ERROR_INVALID_DATA,
            false);
    }
}

static bool dio_valid_utf8(
    const char *text,
    size_t length) {
    return
        text != NULL &&
        length != 0u &&
        length <= INT_MAX &&
        memchr(text, '\0', length) == NULL &&
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            (int)length,
            NULL,
            0) > 0;
}

static char *dio_wide_utf8(const wchar_t *value) {
    const wchar_t *source = value != NULL ? value : L"";
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        source,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    char *result = required > 0 ? (char *)malloc((size_t)required) : NULL;
    if (result == NULL ||
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            source,
            -1,
            result,
            required,
            NULL,
            NULL) != required) {
        free(result);
        return NULL;
    }
    return result;
}

static bool dio_add_wide_json(
    yyjson_mut_doc *document,
    yyjson_mut_val *object,
    const char *name,
    const wchar_t *value,
    bool omit_empty) {
    if (omit_empty && (value == NULL || value[0] == L'\0')) {
        return true;
    }
    char *utf8 = dio_wide_utf8(value);
    const bool added = utf8 != NULL && yyjson_mut_obj_add_strcpy(
        document,
        object,
        name,
        utf8);
    if (utf8 != NULL) {
        SecureZeroMemory(utf8, strlen(utf8));
        free(utf8);
    }
    return added;
}

typedef struct DioSecretUtf8 {
    char *value;
    struct DioSecretUtf8 *next;
} DioSecretUtf8;

static void dio_secret_utf8_release(DioSecretUtf8 *secret) {
    while (secret != NULL) {
        DioSecretUtf8 *next = secret->next;
        if (secret->value != NULL) {
            SecureZeroMemory(secret->value, strlen(secret->value));
            free(secret->value);
        }
        SecureZeroMemory(secret, sizeof(*secret));
        free(secret);
        secret = next;
    }
}

static char *dio_wide_span_utf8(
    const wchar_t *value,
    size_t length) {
    if (value == NULL || length > INT_MAX) {
        return NULL;
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        (int)length,
        NULL,
        0,
        NULL,
        NULL);
    char *result = required >= 0
        ? (char *)malloc((size_t)required + 1u)
        : NULL;
    if (result == NULL ||
        (required != 0 &&
         WideCharToMultiByte(
             CP_UTF8,
             WC_ERR_INVALID_CHARS,
             value,
             (int)length,
             result,
             required,
             NULL,
             NULL) != required)) {
        free(result);
        return NULL;
    }
    result[required] = '\0';
    return result;
}

static bool dio_add_secret_wide_json(
    yyjson_mut_doc *document,
    yyjson_mut_val *object,
    const char *name,
    const wchar_t *value,
    DioSecretUtf8 **secrets) {
    if (value == NULL || value[0] == L'\0') {
        return true;
    }
    DioSecretUtf8 *secret = (DioSecretUtf8 *)calloc(1u, sizeof(*secret));
    if (secret == NULL) {
        return false;
    }
    secret->value = dio_wide_utf8(value);
    if (secret->value == NULL ||
        !yyjson_mut_obj_add_str(document, object, name, secret->value)) {
        dio_secret_utf8_release(secret);
        return false;
    }
    secret->next = *secrets;
    *secrets = secret;
    return true;
}

static bool dio_add_mcp_always(
    yyjson_mut_doc *document,
    yyjson_mut_val *server,
    const wchar_t *lines) {
    yyjson_mut_val *always = yyjson_mut_arr(document);
    const wchar_t *cursor = lines != NULL ? lines : L"";
    if (always == NULL) {
        return false;
    }
    while (*cursor != L'\0') {
        const wchar_t *end = cursor;
        while (*end != L'\0' && *end != L'\r' && *end != L'\n') {
            ++end;
        }
        if (end != cursor) {
            char *utf8 = dio_wide_span_utf8(cursor, (size_t)(end - cursor));
            yyjson_mut_val *name = utf8 != NULL
                ? yyjson_mut_strcpy(document, utf8)
                : NULL;
            free(utf8);
            if (name == NULL || !yyjson_mut_arr_append(always, name)) {
                return false;
            }
        }
        cursor = end;
        while (*cursor == L'\r' || *cursor == L'\n') {
            ++cursor;
        }
    }
    return yyjson_mut_obj_add_val(document, server, "always", always);
}

static bool dio_add_mcp_environment(
    yyjson_mut_doc *document,
    yyjson_mut_val *server,
    const wchar_t *lines,
    DioSecretUtf8 **secrets) {
    yyjson_mut_val *environment = yyjson_mut_obj(document);
    const wchar_t *cursor = lines != NULL ? lines : L"";
    size_t count = 0u;
    if (environment == NULL) {
        return false;
    }
    while (*cursor != L'\0') {
        const wchar_t *end = cursor;
        const wchar_t *separator = NULL;
        while (*end != L'\0' && *end != L'\r' && *end != L'\n') {
            if (*end == L'=' && separator == NULL) {
                separator = end;
            }
            ++end;
        }
        if (end != cursor) {
            char *key_utf8;
            char *value_utf8;
            DioSecretUtf8 *secret;
            yyjson_mut_val *key;
            yyjson_mut_val *value;
            if (separator == NULL || separator == cursor || count >= 64u) {
                return false;
            }
            key_utf8 = dio_wide_span_utf8(
                cursor,
                (size_t)(separator - cursor));
            value_utf8 = dio_wide_span_utf8(
                separator + 1,
                (size_t)(end - separator - 1));
            secret = (DioSecretUtf8 *)calloc(1u, sizeof(*secret));
            key = key_utf8 != NULL
                ? yyjson_mut_strcpy(document, key_utf8)
                : NULL;
            value = value_utf8 != NULL
                ? yyjson_mut_str(document, value_utf8)
                : NULL;
            free(key_utf8);
            if (secret == NULL || key == NULL || value == NULL ||
                !yyjson_mut_obj_add(environment, key, value)) {
                free(secret);
                if (value_utf8 != NULL) {
                    SecureZeroMemory(value_utf8, strlen(value_utf8));
                }
                free(value_utf8);
                return false;
            }
            secret->value = value_utf8;
            secret->next = *secrets;
            *secrets = secret;
            ++count;
        }
        cursor = end;
        while (*cursor == L'\r' || *cursor == L'\n') {
            ++cursor;
        }
    }
    return yyjson_mut_obj_add_val(document, server, "env", environment);
}

static bool dio_add_mcp_servers(
    yyjson_mut_doc *document,
    yyjson_mut_val *config,
    const DioAgentConfig *agent_config,
    DioSecretUtf8 **secrets) {
    yyjson_mut_val *servers = yyjson_mut_arr(document);
    if (servers == NULL || agent_config->mcp_server_count > 16u ||
        (agent_config->mcp_server_count != 0u &&
         agent_config->mcp_servers == NULL)) {
        return false;
    }
    for (size_t index = 0u; index < agent_config->mcp_server_count; ++index) {
        const DioAgentMcpServerConfig *source =
            &agent_config->mcp_servers[index];
        yyjson_mut_val *server = yyjson_mut_obj(document);
        if (server == NULL || source->name == NULL ||
            source->name[0] == L'\0' || source->target == NULL ||
            source->target[0] == L'\0' ||
            !dio_add_wide_json(document, server, "name", source->name, false) ||
            !yyjson_mut_obj_add_str(
                document,
                server,
                "transport",
                source->stdio ? "stdio" : "http") ||
            !yyjson_mut_obj_add_bool(
                document,
                server,
                "enabled",
                source->enabled) ||
            !dio_add_wide_json(
                document,
                server,
                source->stdio ? "exe" : "url",
                source->target,
                false)) {
            return false;
        }
        if (source->stdio) {
            if (!dio_add_wide_json(
                    document,
                    server,
                    "args",
                    source->arguments,
                    true) ||
                !dio_add_wide_json(
                    document,
                    server,
                    "cwd",
                    source->working_directory,
                    true) ||
                !dio_add_mcp_environment(
                    document,
                    server,
                    source->secret,
                    secrets)) {
                return false;
            }
        } else if (!dio_add_secret_wide_json(
                       document,
                       server,
                       "bearer",
                       source->secret,
                       secrets)) {
            return false;
        }
        if (!dio_add_mcp_always(document, server, source->always_tools) ||
            !yyjson_mut_arr_append(servers, server)) {
            return false;
        }
    }
    return yyjson_mut_obj_add_val(document, config, "mcp_servers", servers);
}

static char *dio_hello_json(
    const DioAgentConfig *agent_config,
    size_t *json_length) {
    const DioAgentProviderConfig *provider_config = agent_config->provider;
    DioSecretUtf8 *secrets = NULL;
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document != NULL ? yyjson_mut_obj(document) : NULL;
    yyjson_mut_val *config = document != NULL ? yyjson_mut_obj(document) : NULL;
    yyjson_mut_val *provider = document != NULL ? yyjson_mut_obj(document) : NULL;
    const bool has_provider = provider_config != NULL &&
        ((provider_config->base_url != NULL && provider_config->base_url[0] != L'\0') ||
         (provider_config->api_key != NULL && provider_config->api_key[0] != L'\0') ||
         (provider_config->model != NULL && provider_config->model[0] != L'\0'));
    bool valid = document != NULL && root != NULL && config != NULL &&
        provider != NULL;
    if (valid) {
        yyjson_mut_doc_set_root(document, root);
        valid =
            yyjson_mut_obj_add_str(document, root, "type", "hello") &&
            yyjson_mut_obj_add_uint(document, root, "protocol", 2u) &&
            (has_provider
                ? (dio_add_wide_json(document, provider, "base_url", provider_config->base_url, true) &&
                   dio_add_wide_json(document, provider, "api_key", provider_config->api_key, true) &&
                   dio_add_wide_json(document, provider, "model", provider_config->model, true) &&
                   dio_add_wide_json(document, provider, "reasoning_effort", provider_config->reasoning_effort, true) &&
                   dio_add_wide_json(document, provider, "service_tier", provider_config->service_tier, true) &&
                   yyjson_mut_obj_add_val(document, config, "provider", provider))
                : yyjson_mut_obj_add_null(document, config, "provider")) &&
            dio_add_wide_json(
                document,
                config,
                "system_prompt",
                provider_config != NULL ? provider_config->system_prompt : L"",
                false) &&
            dio_add_mcp_servers(
                document,
                config,
                agent_config,
                &secrets) &&
            yyjson_mut_obj_add_val(document, root, "config", config);
    }
    char *json = valid
        ? yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, json_length)
        : NULL;
    dio_secret_utf8_release(secrets);
    yyjson_mut_doc_free(document);
    if (json != NULL && *json_length > DIO_HARNESS_MAX_PAYLOAD) {
        SecureZeroMemory(json, *json_length);
        free(json);
        json = NULL;
    }
    return json;
}

static uint16_t dio_read_u16(const unsigned char *input) {
    return
        (uint16_t)input[0] |
        ((uint16_t)input[1] << 8u);
}

static uint32_t dio_read_u32(const unsigned char *input) {
    uint32_t value = 0u;
    for (unsigned int index = 0u; index < 4u; ++index) {
        value |= (uint32_t)input[index] << (index * 8u);
    }
    return value;
}

static uint64_t dio_read_u64(const unsigned char *input) {
    uint64_t value = 0u;
    for (unsigned int index = 0u; index < 8u; ++index) {
        value |= (uint64_t)input[index] << (index * 8u);
    }
    return value;
}

static void dio_write_u16(
    unsigned char *output,
    uint16_t value) {
    output[0] = (unsigned char)(value & 0xffu);
    output[1] = (unsigned char)((value >> 8u) & 0xffu);
}

static void dio_write_u32(
    unsigned char *output,
    uint32_t value) {
    for (unsigned int index = 0u; index < 4u; ++index) {
        output[index] =
            (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static void dio_write_u64(
    unsigned char *output,
    uint64_t value) {
    for (unsigned int index = 0u; index < 8u; ++index) {
        output[index] =
            (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static bool dio_read_exact(
    DioAgent *agent,
    void *buffer,
    size_t size) {
    unsigned char *output = (unsigned char *)buffer;
    size_t offset = 0u;
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD failure = ERROR_SUCCESS;
    if (event == NULL) {
        return false;
    }
    while (offset < size) {
        const size_t remaining = size - offset;
        const DWORD wanted =
            remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
        OVERLAPPED operation;
        (void)memset(&operation, 0, sizeof(operation));
        operation.hEvent = event;
        (void)ResetEvent(event);
        DWORD received = 0u;
        EnterCriticalSection(&agent->io_lock);
        bool completed = false;
        DWORD operation_error = ERROR_SUCCESS;
        if (!dio_is_closing(agent)) {
            completed = ReadFile(
                agent->pipe,
                output + offset,
                wanted,
                &received,
                &operation) != 0;
            if (!completed) {
                operation_error = GetLastError();
            }
        } else {
            operation_error = ERROR_CANCELLED;
        }
        LeaveCriticalSection(&agent->io_lock);
        if (!completed && operation_error == ERROR_IO_PENDING) {
            completed =
                WaitForSingleObject(event, INFINITE) ==
                    WAIT_OBJECT_0 &&
                GetOverlappedResult(
                    agent->pipe,
                    &operation,
                    &received,
                    FALSE) != 0;
            if (!completed) {
                operation_error = GetLastError();
            }
        }
        if (!completed || received == 0u) {
            failure = operation_error;
            if (failure == ERROR_SUCCESS) {
                failure = ERROR_BROKEN_PIPE;
            }
            break;
        }
        offset += received;
    }
    (void)CloseHandle(event);
    if (offset != size) {
        SetLastError(failure);
        return false;
    }
    return true;
}

static bool dio_write_all(
    DioAgent *agent,
    const void *buffer,
    size_t size) {
    const unsigned char *input = (const unsigned char *)buffer;
    size_t offset = 0u;
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD failure = ERROR_SUCCESS;
    if (event == NULL) {
        return false;
    }
    while (offset < size) {
        const size_t remaining = size - offset;
        const DWORD wanted =
            remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
        OVERLAPPED operation;
        (void)memset(&operation, 0, sizeof(operation));
        operation.hEvent = event;
        (void)ResetEvent(event);
        DWORD written = 0u;
        EnterCriticalSection(&agent->io_lock);
        bool completed = false;
        DWORD operation_error = ERROR_SUCCESS;
        if (!dio_is_closing(agent)) {
            completed = WriteFile(
                agent->pipe,
                input + offset,
                wanted,
                &written,
                &operation) != 0;
            if (!completed) {
                operation_error = GetLastError();
            }
        } else {
            operation_error = ERROR_CANCELLED;
        }
        LeaveCriticalSection(&agent->io_lock);
        if (!completed && operation_error == ERROR_IO_PENDING) {
            const DWORD wait = WaitForSingleObject(
                event,
                DIO_HARNESS_WRITE_TIMEOUT_MS);
            if (wait == WAIT_TIMEOUT) {
                (void)CancelIoEx(agent->pipe, &operation);
                (void)WaitForSingleObject(event, INFINITE);
                (void)GetOverlappedResult(
                    agent->pipe,
                    &operation,
                    &written,
                    FALSE);
                operation_error = WAIT_TIMEOUT;
            } else {
                completed =
                    wait == WAIT_OBJECT_0 &&
                    GetOverlappedResult(
                        agent->pipe,
                        &operation,
                        &written,
                        FALSE) != 0;
                if (!completed) {
                    operation_error = GetLastError();
                }
            }
        }
        if (!completed || written == 0u) {
            failure = operation_error;
            if (failure == ERROR_SUCCESS) {
                failure = ERROR_BROKEN_PIPE;
            }
            break;
        }
        offset += written;
    }
    (void)CloseHandle(event);
    if (offset != size) {
        SetLastError(failure);
        return false;
    }
    return true;
}

static bool dio_send_json_locked(
    DioAgent *agent,
    uint64_t turn_id,
    const char *json,
    size_t json_length) {
    if (json == NULL ||
        json_length > DIO_HARNESS_MAX_PAYLOAD ||
        agent->next_sequence == 0u) {
        return false;
    }
    const size_t total = DIO_HARNESS_HEADER_SIZE + json_length;
    unsigned char *frame = (unsigned char *)malloc(total);
    if (frame == NULL) {
        return false;
    }
    (void)memset(frame, 0, DIO_HARNESS_HEADER_SIZE);
    (void)memcpy(frame, "DIOH", 4u);
    dio_write_u16(frame + 4u, 1u);
    dio_write_u16(frame + 6u, 1u);
    dio_write_u64(frame + 12u, agent->generation);
    dio_write_u64(frame + 20u, turn_id);
    dio_write_u32(frame + 28u, agent->next_sequence);
    dio_write_u32(frame + 32u, (uint32_t)json_length);
    if (json_length != 0u) {
        (void)memcpy(
            frame + DIO_HARNESS_HEADER_SIZE,
            json,
            json_length);
    }
    ++agent->next_sequence;
    const bool written = dio_write_all(agent, frame, total);
    SecureZeroMemory(frame, total);
    free(frame);
    return written;
}

static char *dio_turn_start_json(
    const char *text,
    size_t text_length,
    size_t *json_length) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root =
        document != NULL ? yyjson_mut_obj(document) : NULL;
    const bool valid =
        root != NULL &&
        yyjson_mut_obj_add_strcpy(
            document,
            root,
            "type",
            "turn.start") &&
        yyjson_mut_obj_add_strncpy(
            document,
            root,
            "text",
            text,
            text_length);
    if (valid) {
        yyjson_mut_doc_set_root(document, root);
    }
    char *json =
        valid
            ? yyjson_mut_write(
                  document,
                  YYJSON_WRITE_NOFLAG,
                  json_length)
            : NULL;
    yyjson_mut_doc_free(document);
    return json;
}

static const char *dio_json_string(
    yyjson_val *object,
    const char *key) {
    yyjson_val *value =
        yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool dio_accept_control(
    DioAgent *agent,
    uint64_t turn_id,
    const char *json,
    size_t json_length) {
    yyjson_doc *document =
        yyjson_read((char *)json, json_length, YYJSON_READ_NOFLAG);
    if (document == NULL) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(document);
    const char *type = dio_json_string(root, "type");
    bool valid = type != NULL;

    if (valid && strcmp(type, "ready") == 0) {
        bool ready = false;
        yyjson_val *protocol = yyjson_obj_get(root, "protocol");
        yyjson_val *configured = yyjson_obj_get(root, "provider_configured");
        const bool provider_configured = yyjson_is_true(configured);
        EnterCriticalSection(&agent->state_lock);
        if (agent->state == DIO_AGENT_STATE_STARTING &&
            turn_id == 0u &&
            yyjson_is_int(protocol) &&
            yyjson_get_sint(protocol) == 2 &&
            yyjson_is_bool(configured)) {
            agent->state = DIO_AGENT_STATE_READY;
            ready = true;
        }
        LeaveCriticalSection(&agent->state_lock);
        valid = ready;
        if (ready) {
            dio_emit_ready(agent, provider_configured);
        }
    } else if (valid && strcmp(type, "provider.models") == 0) {
        yyjson_val *request = yyjson_obj_get(root, "request_id");
        yyjson_val *models = yyjson_obj_get(root, "models");
        const uint64_t request_id = yyjson_is_uint(request)
            ? yyjson_get_uint(request)
            : 0u;
        size_t bytes = 1u;
        size_t count = 0u;
        yyjson_val *model;
        size_t index;
        size_t maximum;
        valid = turn_id == 0u && request_id != 0u && yyjson_is_arr(models);
        yyjson_arr_foreach(models, index, maximum, model) {
            const char *id = yyjson_is_str(model) ? yyjson_get_str(model) : NULL;
            const size_t length = id != NULL ? strlen(id) : 0u;
            if (!valid || count >= 1000u || length == 0u || length >= 256u ||
                memchr(id, '\n', length) != NULL ||
                bytes > (256u * 1024u) - length - 1u) {
                valid = false;
                break;
            }
            bytes += length + 1u;
            count += 1u;
        }
        char *catalog = valid ? (char *)malloc(bytes) : NULL;
        if (valid && catalog == NULL) {
            valid = false;
        }
        size_t used = 0u;
        if (valid) {
            yyjson_arr_foreach(models, index, maximum, model) {
                const char *id = yyjson_get_str(model);
                const size_t length = strlen(id);
                memcpy(catalog + used, id, length);
                used += length;
                catalog[used++] = '\n';
            }
            catalog[used] = '\0';
            dio_emit(
                agent,
                DIO_AGENT_EVENT_MODELS,
                request_id,
                catalog,
                used,
                ERROR_SUCCESS,
                false);
        }
        free(catalog);
    } else if (valid && strcmp(type, "provider.models.error") == 0) {
        yyjson_val *request = yyjson_obj_get(root, "request_id");
        const uint64_t request_id = yyjson_is_uint(request)
            ? yyjson_get_uint(request)
            : 0u;
        const char *code = dio_json_string(root, "code");
        valid = turn_id == 0u && request_id != 0u && code != NULL;
        if (valid) {
            dio_emit(
                agent,
                DIO_AGENT_EVENT_MODELS_ERROR,
                request_id,
                code,
                strlen(code),
                ERROR_SUCCESS,
                false);
        }
    } else if (valid && strcmp(type, "mcp.status") == 0) {
        yyjson_val *configured_value = yyjson_obj_get(root, "configured");
        yyjson_val *available_value = yyjson_obj_get(root, "available");
        yyjson_val *tools_value = yyjson_obj_get(root, "tools");
        const uint64_t configured = yyjson_is_uint(configured_value)
            ? yyjson_get_uint(configured_value)
            : UINT64_MAX;
        const uint64_t available = yyjson_is_uint(available_value)
            ? yyjson_get_uint(available_value)
            : UINT64_MAX;
        const uint64_t tools = yyjson_is_uint(tools_value)
            ? yyjson_get_uint(tools_value)
            : UINT64_MAX;
        bool active;
        EnterCriticalSection(&agent->state_lock);
        active = agent->state == DIO_AGENT_STATE_ACTIVE &&
            agent->active_turn_id == turn_id &&
            !agent->cancel_requested;
        LeaveCriticalSection(&agent->state_lock);
        valid = active && configured <= 16u && available <= configured &&
            tools <= 128u;
        if (valid) {
            dio_emit_mcp_status(
                agent,
                turn_id,
                (unsigned int)configured,
                (unsigned int)available,
                (unsigned int)tools);
        }
    } else if (valid && strcmp(type, "tool.approval.required") == 0) {
        yyjson_val *request_value = yyjson_obj_get(root, "request_id");
        yyjson_val *arguments = yyjson_obj_get(root, "arguments");
        const uint64_t request_id = yyjson_is_uint(request_value)
            ? yyjson_get_uint(request_value)
            : 0u;
        const char *server_name = dio_json_string(root, "server");
        const char *tool_name = dio_json_string(root, "tool");
        bool active;
        EnterCriticalSection(&agent->state_lock);
        active = agent->state == DIO_AGENT_STATE_ACTIVE &&
            agent->active_turn_id == turn_id &&
            !agent->cancel_requested;
        LeaveCriticalSection(&agent->state_lock);
        size_t arguments_length = 0u;
        char *arguments_json = yyjson_is_obj(arguments)
            ? yyjson_val_write(
                  arguments,
                  YYJSON_WRITE_NOFLAG,
                  &arguments_length)
            : NULL;
        valid = active && request_id != 0u &&
            request_id <= LONG_MAX && server_name != NULL &&
            server_name[0] != '\0' && strlen(server_name) <= 64u &&
            tool_name != NULL && tool_name[0] != '\0' &&
            strlen(tool_name) <= 128u && arguments_json != NULL &&
            arguments_length <= 256u * 1024u;
        if (valid) {
            dio_emit_tool_approval(
                agent,
                turn_id,
                request_id,
                server_name,
                tool_name,
                arguments_json,
                arguments_length);
        }
        if (arguments_json != NULL) {
            SecureZeroMemory(arguments_json, arguments_length);
            free(arguments_json);
        }
    } else if (valid && strcmp(type, "turn.accepted") == 0) {
        bool accepted = false;
        unsigned int sample_rate = 0u;
        yyjson_val *sample_rate_value =
            yyjson_obj_get(root, "audio_pcm16_hz");
        if (sample_rate_value != NULL) {
            const uint64_t value =
                yyjson_is_uint(sample_rate_value)
                    ? yyjson_get_uint(sample_rate_value)
                    : UINT64_MAX;
            if (value != DIO_HARNESS_OUTPUT_SAMPLE_RATE) {
                valid = false;
            } else {
                sample_rate = (unsigned int)value;
            }
        }
        EnterCriticalSection(&agent->state_lock);
        if (valid &&
            agent->state == DIO_AGENT_STATE_ACTIVE &&
            agent->active_turn_id == turn_id &&
            !agent->accepted) {
            agent->accepted = true;
            agent->active_audio_sample_rate = sample_rate;
            accepted = true;
        }
        LeaveCriticalSection(&agent->state_lock);
        if (accepted) {
            dio_emit_accepted(
                agent,
                turn_id,
                sample_rate);
        }
    } else if (valid && strcmp(type, "text.delta") == 0) {
        const char *text = dio_json_string(root, "text");
        bool accepted = false;
        if (text == NULL) {
            valid = false;
        } else {
            EnterCriticalSection(&agent->state_lock);
            accepted =
                agent->state == DIO_AGENT_STATE_ACTIVE &&
                agent->active_turn_id == turn_id &&
                agent->accepted &&
                !agent->cancel_requested;
            LeaveCriticalSection(&agent->state_lock);
            if (accepted && text[0] != '\0') {
                dio_emit(
                    agent,
                    DIO_AGENT_EVENT_TEXT_DELTA,
                    turn_id,
                    text,
                    strlen(text),
                    ERROR_SUCCESS,
                    false);
            }
        }
    } else if (valid && strcmp(type, "turn.done") == 0) {
        yyjson_val *cancelled_value =
            yyjson_obj_get(root, "cancelled");
        const char *text = dio_json_string(root, "text");
        bool finished = false;
        bool cancelled = false;
        if (!yyjson_is_bool(cancelled_value)) {
            valid = false;
        } else {
            EnterCriticalSection(&agent->state_lock);
            if (agent->state == DIO_AGENT_STATE_ACTIVE &&
                agent->active_turn_id == turn_id) {
                cancelled =
                    agent->cancel_requested ||
                    yyjson_is_true(cancelled_value);
                agent->state = DIO_AGENT_STATE_READY;
                agent->active_turn_id = 0u;
                agent->active_audio_sample_rate = 0u;
                agent->accepted = false;
                agent->cancel_requested = false;
                finished = true;
            }
            LeaveCriticalSection(&agent->state_lock);
            if (finished) {
                dio_emit(
                    agent,
                    DIO_AGENT_EVENT_COMPLETE,
                    turn_id,
                    !cancelled ? text : NULL,
                    !cancelled && text != NULL ? strlen(text) : 0u,
                    ERROR_SUCCESS,
                    cancelled);
            }
        }
    } else if (valid && strcmp(type, "turn.error") == 0) {
        const char *code = dio_json_string(root, "code");
        const char *message = dio_json_string(root, "message");
        yyjson_val *http_status_value = yyjson_obj_get(root, "http_status");
        const uint64_t http_status = yyjson_is_uint(http_status_value)
            ? yyjson_get_uint(http_status_value)
            : 0u;
        const bool provider_error =
            code != NULL && strcmp(code, "provider_error") == 0;
        bool failed = false;
        bool cancelled = false;
        valid = provider_error
            ? http_status >= 100u && http_status <= 599u &&
                  message != NULL && strlen(message) < 4096u
            : http_status_value == NULL;
        EnterCriticalSection(&agent->state_lock);
        if (valid && agent->state == DIO_AGENT_STATE_ACTIVE &&
            agent->active_turn_id == turn_id) {
            cancelled = agent->cancel_requested;
            agent->state = DIO_AGENT_STATE_READY;
            agent->active_turn_id = 0u;
            agent->active_audio_sample_rate = 0u;
            agent->accepted = false;
            agent->cancel_requested = false;
            failed = true;
        }
        LeaveCriticalSection(&agent->state_lock);
        if (failed && cancelled) {
            dio_emit(
                agent,
                DIO_AGENT_EVENT_COMPLETE,
                turn_id,
                NULL,
                0u,
                ERROR_SUCCESS,
                true);
        } else if (failed) {
            static const char fallback[] =
                "Dio Harness could not complete the turn.";
            if (provider_error) {
                dio_emit_provider_error(
                    agent,
                    turn_id,
                    (unsigned int)http_status,
                    message);
            } else {
                dio_emit(
                    agent,
                    DIO_AGENT_EVENT_ERROR,
                    turn_id,
                    code != NULL ? code : fallback,
                    code != NULL ? strlen(code) : sizeof(fallback) - 1u,
                    ERROR_SUCCESS,
                    false);
            }
        }
    }
    yyjson_doc_free(document);
    return valid;
}

static bool dio_accept_pcm16(
    DioAgent *agent,
    uint64_t turn_id,
    const unsigned char *payload,
    size_t payload_size) {
    bool accepted;
    bool unexpected;

    if (payload == NULL ||
        payload_size == 0u ||
        (payload_size % sizeof(int16_t)) != 0u) {
        return false;
    }
    EnterCriticalSection(&agent->state_lock);
    accepted =
        agent->state == DIO_AGENT_STATE_ACTIVE &&
        agent->active_turn_id == turn_id &&
        agent->accepted &&
        !agent->cancel_requested &&
        agent->active_audio_sample_rate ==
            DIO_HARNESS_OUTPUT_SAMPLE_RATE;
    unexpected =
        agent->state == DIO_AGENT_STATE_ACTIVE &&
        agent->active_turn_id == turn_id &&
        !agent->cancel_requested &&
        (!agent->accepted ||
         agent->active_audio_sample_rate == 0u);
    LeaveCriticalSection(&agent->state_lock);
    if (unexpected) {
        return false;
    }
    if (accepted) {
        dio_emit_pcm16(
            agent,
            turn_id,
            (const int16_t *)payload,
            payload_size / sizeof(int16_t));
    }
    return true;
}

static bool dio_read_and_accept_frame(DioAgent *agent) {
    unsigned char header[DIO_HARNESS_HEADER_SIZE];
    if (!dio_read_exact(agent, header, sizeof(header))) {
        return false;
    }
    const uint16_t version = dio_read_u16(header + 4u);
    const uint16_t type = dio_read_u16(header + 6u);
    const uint32_t flags = dio_read_u32(header + 8u);
    const uint64_t generation = dio_read_u64(header + 12u);
    const uint64_t turn_id = dio_read_u64(header + 20u);
    const uint32_t sequence = dio_read_u32(header + 28u);
    const uint32_t payload_size = dio_read_u32(header + 32u);
    if (memcmp(header, "DIOH", 4u) != 0 ||
        version != 1u ||
        (type != DIO_HARNESS_CONTROL_FRAME &&
         type != DIO_HARNESS_OUTPUT_PCM16_FRAME) ||
        flags != 0u ||
        generation != agent->generation ||
        sequence == 0u ||
        sequence <= agent->last_remote_sequence ||
        payload_size == 0u ||
        payload_size > DIO_HARNESS_MAX_PAYLOAD) {
        return false;
    }
    unsigned char *payload =
        (unsigned char *)malloc((size_t)payload_size + 1u);
    if (payload == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return false;
    }
    const bool read =
        dio_read_exact(agent, payload, payload_size);
    payload[payload_size] = '\0';
    if (read) {
        agent->last_remote_sequence = sequence;
    }
    const bool accepted =
        read &&
        (type == DIO_HARNESS_CONTROL_FRAME
             ? dio_accept_control(
                   agent,
                   turn_id,
                   (const char *)payload,
                   payload_size)
             : dio_accept_pcm16(
                   agent,
                   turn_id,
                   payload,
                   payload_size));
    SecureZeroMemory(payload, (size_t)payload_size + 1u);
    free(payload);
    if (!accepted && read) {
        SetLastError(ERROR_INVALID_DATA);
    }
    return accepted;
}

static DWORD WINAPI dio_reader_worker(void *context) {
    DioAgent *agent = (DioAgent *)context;
    OVERLAPPED operation;
    (void)memset(&operation, 0, sizeof(operation));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (operation.hEvent == NULL) {
        dio_transport_failure(agent, GetLastError());
        return 0u;
    }
    EnterCriticalSection(&agent->io_lock);
    bool connected = false;
    DWORD connect_error = ERROR_SUCCESS;
    if (!dio_is_closing(agent)) {
        connected =
            ConnectNamedPipe(agent->pipe, &operation) != 0;
        if (!connected) {
            connect_error = GetLastError();
        }
    } else {
        connect_error = ERROR_CANCELLED;
    }
    LeaveCriticalSection(&agent->io_lock);
    if (!connected) {
        if (connect_error == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (connect_error == ERROR_IO_PENDING) {
            DWORD transferred = 0u;
            connected =
                WaitForSingleObject(
                    operation.hEvent,
                    INFINITE) == WAIT_OBJECT_0 &&
                GetOverlappedResult(
                    agent->pipe,
                    &operation,
                    &transferred,
                    FALSE) != 0;
            if (!connected) {
                connect_error = GetLastError();
            }
        }
    }
    (void)CloseHandle(operation.hEvent);
    if (!connected) {
        if (!dio_is_closing(agent)) {
            dio_transport_failure(agent, connect_error);
        }
        return 0u;
    }
    ULONG client_pid = 0u;
    if (!GetNamedPipeClientProcessId(agent->pipe, &client_pid) ||
        client_pid != agent->process_id) {
        dio_transport_failure(agent, ERROR_ACCESS_DENIED);
        return 0u;
    }
    bool hello_sent = false;
    EnterCriticalSection(&agent->state_lock);
    if (agent->state == DIO_AGENT_STATE_STARTING &&
        agent->hello_json != NULL) {
        hello_sent = dio_send_json_locked(
            agent,
            0u,
            agent->hello_json,
            agent->hello_json_length);
        SecureZeroMemory(agent->hello_json, agent->hello_json_length);
        free(agent->hello_json);
        agent->hello_json = NULL;
        agent->hello_json_length = 0u;
    }
    LeaveCriticalSection(&agent->state_lock);
    if (!hello_sent) {
        dio_transport_failure(agent, GetLastError());
        return 0u;
    }
    while (!dio_is_closing(agent)) {
        if (!dio_read_and_accept_frame(agent)) {
            if (!dio_is_closing(agent)) {
                const DWORD error = GetLastError();
                dio_transport_failure(
                    agent,
                    error != ERROR_SUCCESS
                        ? error
                        : ERROR_BROKEN_PIPE);
            }
            break;
        }
    }
    return 0u;
}

static bool dio_resolve_executable(
    const wchar_t *configured,
    wchar_t output[DIO_HARNESS_PATH_CAP]) {
    DWORD attributes;
    if (configured != NULL && configured[0] != L'\0') {
        const DWORD length = GetFullPathNameW(
            configured,
            DIO_HARNESS_PATH_CAP,
            output,
            NULL);
        if (length == 0u || length >= DIO_HARNESS_PATH_CAP) {
            return false;
        }
        attributes = GetFileAttributesW(output);
        return
            attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
    }
    const DWORD module_length = GetModuleFileNameW(
        NULL,
        output,
        DIO_HARNESS_PATH_CAP);
    wchar_t *separator =
        module_length != 0u && module_length < DIO_HARNESS_PATH_CAP
            ? wcsrchr(output, L'\\')
            : NULL;
    if (separator != NULL &&
        SUCCEEDED(StringCchCopyW(
            separator + 1,
            DIO_HARNESS_PATH_CAP -
                (size_t)(separator + 1 - output),
            L"dio.exe"))) {
        attributes = GetFileAttributesW(output);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) {
            return true;
        }
    }
    const DWORD length = SearchPathW(
        NULL,
        L"dio.exe",
        NULL,
        DIO_HARNESS_PATH_CAP,
        output,
        NULL);
    return length != 0u && length < DIO_HARNESS_PATH_CAP;
}

static void dio_close_handle(HANDLE *handle) {
    if (handle != NULL &&
        *handle != NULL &&
        *handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(*handle);
    }
    if (handle != NULL) {
        *handle = NULL;
    }
}

static void dio_destroy(DioAgent *agent) {
    if (agent == NULL) {
        return;
    }
    InterlockedExchange(&agent->closing, 1);
    if (agent->pipe != NULL &&
        agent->pipe != INVALID_HANDLE_VALUE) {
        EnterCriticalSection(&agent->io_lock);
        (void)CancelIoEx(agent->pipe, NULL);
        LeaveCriticalSection(&agent->io_lock);
    }
    EnterCriticalSection(&agent->state_lock);
    agent->state = DIO_AGENT_STATE_CLOSING;
    LeaveCriticalSection(&agent->state_lock);
    if (agent->job != NULL) {
        (void)TerminateJobObject(agent->job, ERROR_CANCELLED);
    }
    if (agent->reader_thread != NULL) {
        (void)WaitForSingleObject(agent->reader_thread, INFINITE);
    }
    if (agent->pipe != NULL &&
        agent->pipe != INVALID_HANDLE_VALUE) {
        (void)DisconnectNamedPipe(agent->pipe);
    }
    dio_close_handle(&agent->reader_thread);
    dio_close_handle(&agent->pipe);
    dio_close_handle(&agent->process);
    dio_close_handle(&agent->job);
    DeleteCriticalSection(&agent->io_lock);
    DeleteCriticalSection(&agent->callback_lock);
    DeleteCriticalSection(&agent->state_lock);
    if (agent->hello_json != NULL) {
        SecureZeroMemory(agent->hello_json, agent->hello_json_length);
        free(agent->hello_json);
    }
    free(agent);
}

DioAgentResult dio_agent_open(
    const DioAgentConfig *config,
    DioAgent **output) {
    wchar_t executable[DIO_HARNESS_PATH_CAP];
    wchar_t pipe_name[256];
    wchar_t command[DIO_HARNESS_PATH_CAP];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SECURITY_ATTRIBUTES security;
    PSECURITY_DESCRIPTOR security_descriptor = NULL;

    if (output == NULL) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (config == NULL ||
        config->callback == NULL ||
        !dio_resolve_executable(
            config->executable_path,
            executable)) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    DioAgent *agent = (DioAgent *)calloc(1u, sizeof(*agent));
    if (agent == NULL) {
        return DIO_AGENT_OUT_OF_MEMORY;
    }
    InitializeCriticalSection(&agent->state_lock);
    InitializeCriticalSection(&agent->callback_lock);
    InitializeCriticalSection(&agent->io_lock);
    agent->callback = config->callback;
    agent->callback_context = config->callback_context;
    agent->state = DIO_AGENT_STATE_STARTING;
    agent->pipe = INVALID_HANDLE_VALUE;
    agent->generation = GetTickCount64();
    if (agent->generation == 0u) {
        agent->generation = 1u;
    }
    agent->next_sequence = 1u;
    agent->hello_json = dio_hello_json(
        config,
        &agent->hello_json_length);
    if (agent->hello_json == NULL) {
        dio_destroy(agent);
        return DIO_AGENT_INVALID_ARGUMENT;
    }

    if (!dio_pipe_name(pipe_name, _countof(pipe_name)) ||
        !dio_pipe_security(
            &security,
            &security_descriptor)) {
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    agent->pipe = CreateNamedPipeW(
        pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
            FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1u,
        65536u,
        65536u,
        0u,
        &security);
    (void)LocalFree(security_descriptor);
    security_descriptor = NULL;
    if (agent->pipe == INVALID_HANDLE_VALUE) {
        agent->pipe = NULL;
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    agent->job = CreateJobObjectW(NULL, NULL);
    (void)memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (agent->job == NULL ||
        !SetInformationJobObject(
            agent->job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits)) ||
        FAILED(StringCchPrintfW(
            command,
            _countof(command),
            L"\"%ls\" --pipe \"%ls\"",
            executable,
            pipe_name))) {
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(
            executable,
            command,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            NULL,
            config->working_directory != NULL &&
                    config->working_directory[0] != L'\0'
                ? config->working_directory
                : NULL,
            &startup,
            &process)) {
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    agent->process = process.hProcess;
    agent->process_id = process.dwProcessId;
    if (!AssignProcessToJobObject(agent->job, process.hProcess)) {
        (void)TerminateProcess(process.hProcess, ERROR_ACCESS_DENIED);
        (void)CloseHandle(process.hThread);
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    agent->reader_thread = CreateThread(
        NULL,
        0u,
        dio_reader_worker,
        agent,
        0u,
        NULL);
    if (agent->reader_thread == NULL ||
        ResumeThread(process.hThread) == (DWORD)-1) {
        (void)CloseHandle(process.hThread);
        dio_destroy(agent);
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    (void)CloseHandle(process.hThread);
    *output = agent;
    return DIO_AGENT_OK;
}

DioAgentResult dio_agent_submit(
    DioAgent *agent,
    const char *utf8_text,
    size_t text_length) {
    static const char commit[] = "{\"type\":\"turn.commit\"}";
    if (agent == NULL ||
        !dio_valid_utf8(utf8_text, text_length) ||
        text_length > DIO_HARNESS_MAX_PAYLOAD - 128u) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    size_t start_length = 0u;
    char *start = dio_turn_start_json(
        utf8_text,
        text_length,
        &start_length);
    if (start == NULL) {
        return DIO_AGENT_OUT_OF_MEMORY;
    }
    uint64_t turn_id = 0u;
    bool sent = false;
    DioAgentResult result = DIO_AGENT_OK;
    EnterCriticalSection(&agent->state_lock);
    if (agent->state == DIO_AGENT_STATE_STARTING) {
        result = DIO_AGENT_NOT_READY;
    } else if (agent->state == DIO_AGENT_STATE_ACTIVE) {
        result = DIO_AGENT_BUSY;
    } else if (agent->state != DIO_AGENT_STATE_READY) {
        result = DIO_AGENT_CLOSED;
    } else {
        turn_id = agent->next_turn_id + 1u;
        if (turn_id == 0u) {
            turn_id = 1u;
        }
        agent->next_turn_id = turn_id;
        agent->active_turn_id = turn_id;
        agent->active_audio_sample_rate = 0u;
        agent->accepted = false;
        agent->cancel_requested = false;
        agent->state = DIO_AGENT_STATE_ACTIVE;
        sent =
            dio_send_json_locked(
                agent,
                turn_id,
                start,
                start_length) &&
            dio_send_json_locked(
                agent,
                turn_id,
                commit,
                sizeof(commit) - 1u);
    }
    LeaveCriticalSection(&agent->state_lock);
    SecureZeroMemory(start, start_length);
    free(start);
    if (result != DIO_AGENT_OK) {
        return result;
    }
    if (!sent) {
        dio_transport_failure(agent, GetLastError());
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    return DIO_AGENT_OK;
}

DioAgentResult dio_agent_list_models(
    DioAgent *agent,
    uint64_t request_id) {
    if (agent == NULL || request_id == 0u) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    char request[128];
    const int length = sprintf_s(
        request,
        sizeof(request),
        "{\"type\":\"provider.models.list\",\"request_id\":%llu}",
        (unsigned long long)request_id);
    if (length <= 0) {
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    bool sent = false;
    DioAgentResult result = DIO_AGENT_OK;
    EnterCriticalSection(&agent->state_lock);
    if (agent->state == DIO_AGENT_STATE_STARTING) {
        result = DIO_AGENT_NOT_READY;
    } else if (agent->state == DIO_AGENT_STATE_ACTIVE) {
        result = DIO_AGENT_BUSY;
    } else if (agent->state != DIO_AGENT_STATE_READY) {
        result = DIO_AGENT_CLOSED;
    } else {
        sent = dio_send_json_locked(
            agent,
            0u,
            request,
            (size_t)length);
    }
    LeaveCriticalSection(&agent->state_lock);
    if (result != DIO_AGENT_OK) {
        return result;
    }
    if (!sent) {
        dio_transport_failure(agent, GetLastError());
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    return DIO_AGENT_OK;
}

DioAgentResult dio_agent_approve_tool(
    DioAgent *agent,
    uint64_t turn_id,
    uint64_t request_id,
    DioAgentToolDecision decision) {
    if (agent == NULL || turn_id == 0u || request_id == 0u ||
        request_id > LONG_MAX || decision < DIO_AGENT_TOOL_ONCE ||
        decision > DIO_AGENT_TOOL_DENY) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    const char *name = decision == DIO_AGENT_TOOL_ONCE
        ? "once"
        : decision == DIO_AGENT_TOOL_ALWAYS
            ? "always"
            : "deny";
    char response[160];
    const int length = sprintf_s(
        response,
        sizeof(response),
        "{\"type\":\"tool.approval\",\"request_id\":%llu,"
        "\"decision\":\"%s\"}",
        (unsigned long long)request_id,
        name);
    if (length <= 0) {
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    bool sent = false;
    DioAgentResult result = DIO_AGENT_OK;
    EnterCriticalSection(&agent->state_lock);
    if (agent->state == DIO_AGENT_STATE_STARTING) {
        result = DIO_AGENT_NOT_READY;
    } else if (agent->state != DIO_AGENT_STATE_ACTIVE ||
               agent->active_turn_id != turn_id ||
               agent->cancel_requested) {
        result = agent->state == DIO_AGENT_STATE_READY
            ? DIO_AGENT_NOT_READY
            : DIO_AGENT_CLOSED;
    } else {
        sent = dio_send_json_locked(
            agent,
            turn_id,
            response,
            (size_t)length);
    }
    LeaveCriticalSection(&agent->state_lock);
    SecureZeroMemory(response, sizeof(response));
    if (result != DIO_AGENT_OK) {
        return result;
    }
    if (!sent) {
        dio_transport_failure(agent, GetLastError());
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    return DIO_AGENT_OK;
}

DioAgentResult dio_agent_cancel(DioAgent *agent) {
    static const char cancel[] = "{\"type\":\"turn.cancel\"}";
    if (agent == NULL) {
        return DIO_AGENT_INVALID_ARGUMENT;
    }
    bool sent = false;
    DioAgentResult result = DIO_AGENT_OK;
    EnterCriticalSection(&agent->state_lock);
    if (agent->state != DIO_AGENT_STATE_ACTIVE) {
        result =
            agent->state == DIO_AGENT_STATE_READY ||
                    agent->state == DIO_AGENT_STATE_STARTING
                ? DIO_AGENT_NOT_READY
                : DIO_AGENT_CLOSED;
    } else if (agent->cancel_requested) {
        result = DIO_AGENT_BUSY;
    } else {
        agent->cancel_requested = true;
        sent = dio_send_json_locked(
            agent,
            agent->active_turn_id,
            cancel,
            sizeof(cancel) - 1u);
        if (!sent) {
            agent->cancel_requested = false;
        }
    }
    LeaveCriticalSection(&agent->state_lock);
    if (result != DIO_AGENT_OK) {
        return result;
    }
    if (!sent) {
        dio_transport_failure(agent, GetLastError());
        return DIO_AGENT_PLATFORM_FAILURE;
    }
    return DIO_AGENT_OK;
}

void dio_agent_close(DioAgent *agent) {
    dio_destroy(agent);
}
