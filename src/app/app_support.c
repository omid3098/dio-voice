#include "app_support.h"

#include <limits.h>
#include <pathcch.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yyjson.h>

enum {
    DIO_SETTINGS_MAX_BYTES = 1024 * 1024,
    DIO_SETTINGS_SCHEMA = 4,
    DIO_SYSTEM_PROMPT_MAX_CHARS = 65536,
    DIO_SYSTEM_PROMPT_MAX_UTF8_BYTES = 64 * 1024
};

static const wchar_t DIO_DEFAULT_SYSTEM_PROMPT[] =
    L"Input is spoken. Reply concisely in the user's language, using plain "
    L"text suitable for speech synthesis.";

static bool dio_system_prompt_valid(const wchar_t *prompt) {
    if (prompt == NULL ||
        wcslen(prompt) > DIO_SYSTEM_PROMPT_MAX_CHARS) {
        return false;
    }
    const int bytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        prompt,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    return bytes > 0 &&
        (size_t)(bytes - 1) <= DIO_SYSTEM_PROMPT_MAX_UTF8_BYTES;
}

static void dio_error(
    wchar_t *target,
    size_t capacity,
    const wchar_t *message) {
    if (target != NULL && capacity != 0u) {
        (void)wcsncpy_s(target, capacity, message, _TRUNCATE);
    }
}

static bool dio_join(
    wchar_t *output,
    size_t capacity,
    const wchar_t *left,
    const wchar_t *right) {
    return
        output != NULL &&
        left != NULL &&
        right != NULL &&
        SUCCEEDED(PathCchCombine(output, capacity, left, right));
}

static bool dio_directory(const wchar_t *path) {
    const DWORD attributes = GetFileAttributesW(path);
    int result;
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
    }
    result = SHCreateDirectoryExW(NULL, path, NULL);
    return result == ERROR_SUCCESS ||
        result == ERROR_ALREADY_EXISTS ||
        result == ERROR_FILE_EXISTS;
}

static bool dio_executable_directory(
    wchar_t *output,
    size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, output, (DWORD)capacity);
    wchar_t *separator;
    if (length == 0u || (size_t)length >= capacity) {
        return false;
    }
    separator = wcsrchr(output, L'\\');
    if (separator == NULL) {
        return false;
    }
    *separator = L'\0';
    return true;
}

static bool dio_release_roots(
    const wchar_t *executable_directory,
    wchar_t *root,
    size_t root_capacity,
    wchar_t *version,
    size_t version_capacity) {
    static const wchar_t marker[] = L"\\.dio\\versions\\";
    const size_t length = wcslen(executable_directory);
    size_t index;
    for (index = 0u; index + _countof(marker) - 1u <= length; ++index) {
        if (_wcsnicmp(
                executable_directory + index,
                marker,
                _countof(marker) - 1u) == 0) {
            const wchar_t *version_start =
                executable_directory + index + _countof(marker) - 1u;
            const wchar_t *version_end = wcschr(version_start, L'\\');
            const size_t root_length = index + 5u;
            const size_t version_length = version_end != NULL
                ? (size_t)(version_end - executable_directory)
                : length;
            if (root_length >= root_capacity ||
                version_length >= version_capacity ||
                version_start == version_end ||
                *version_start == L'\0') {
                return false;
            }
            memcpy(root, executable_directory, root_length * sizeof(*root));
            root[root_length] = L'\0';
            memcpy(version, executable_directory, version_length * sizeof(*version));
            version[version_length] = L'\0';
            return true;
        }
    }
    return false;
}

int dio_utf8_to_wide(const char *source, wchar_t *target, size_t capacity) {
    int result;
    if (source == NULL || target == NULL || capacity == 0u || capacity > INT_MAX) {
        return 0;
    }
    result = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source,
        -1,
        target,
        (int)capacity);
    if (result == 0) {
        target[0] = L'\0';
    }
    return result;
}

int dio_wide_to_utf8(const wchar_t *source, char *target, size_t capacity) {
    int result;
    if (source == NULL || target == NULL || capacity == 0u || capacity > INT_MAX) {
        return 0;
    }
    result = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        source,
        -1,
        target,
        (int)capacity,
        NULL,
        NULL);
    if (result == 0) {
        target[0] = '\0';
    }
    return result;
}

bool dio_paths_initialize(
    DioPaths *paths,
    bool isolated_data) {
    wchar_t executable_directory[MAX_PATH];
    wchar_t data[MAX_PATH];
    wchar_t version[MAX_PATH];
    wchar_t configured_root[MAX_PATH];
    DWORD configured_length;
    bool result = false;

    if (paths == NULL) {
        return false;
    }
    ZeroMemory(paths, sizeof(*paths));
    if (!dio_executable_directory(
            executable_directory,
            _countof(executable_directory))) {
        return false;
    }
    version[0] = L'\0';
    configured_length = isolated_data
        ? 0u
        : GetEnvironmentVariableW(
            L"DIO_ROOT",
            configured_root,
            (DWORD)_countof(configured_root));
    if (configured_length >= _countof(configured_root)) {
        return false;
    }
    if (isolated_data) {
        if (!dio_join(
                paths->root,
                _countof(paths->root),
                executable_directory,
                L"ui-smoke-data")) {
            return false;
        }
        (void)wcscpy_s(version, _countof(version), paths->root);
        (void)wcscpy_s(data, _countof(data), paths->root);
    } else {
        wchar_t detected_root[MAX_PATH];
        wchar_t detected_version[MAX_PATH];
        const bool installed = dio_release_roots(
            executable_directory,
            detected_root,
            _countof(detected_root),
            detected_version,
            _countof(detected_version));
        if (configured_length > 0u) {
            if (wcscpy_s(
                    paths->root,
                    _countof(paths->root),
                    configured_root) != 0) {
                return false;
            }
        } else if (installed) {
            (void)wcscpy_s(
                paths->root,
                _countof(paths->root),
                detected_root);
        } else if (!dio_join(
                       paths->root,
                       _countof(paths->root),
                       executable_directory,
                       L".dio")) {
            return false;
        }
        if (installed && configured_length == 0u) {
            (void)wcscpy_s(
                version,
                _countof(version),
                detected_version);
        } else if (!dio_join(
                       version,
                       _countof(version),
                       paths->root,
                       L"versions\\current")) {
            return false;
        }
        if (!dio_join(data, _countof(data), paths->root, L"data")) {
            return false;
        }
    }
    if (
        !dio_directory(paths->root) ||
        !dio_directory(data) ||
        !dio_directory(version) ||
        !dio_join(paths->settings, _countof(paths->settings), data, L"settings.json") ||
        !dio_join(paths->secrets, _countof(paths->secrets), data, L"secrets.bin") ||
        !dio_join(paths->models, _countof(paths->models), paths->root, L"models") ||
        !dio_join(paths->runtime, _countof(paths->runtime), version, L"runtime") ||
        !dio_join(paths->workspace, _countof(paths->workspace), data, L"workspace") ||
        !dio_join(paths->announce, _countof(paths->announce), data, L"announce") ||
        !dio_join(paths->logs, _countof(paths->logs), paths->root, L"logs") ||
        !dio_directory(paths->models) ||
        !dio_directory(paths->runtime) ||
        !dio_directory(paths->workspace) ||
        !dio_directory(paths->announce) ||
        !dio_directory(paths->logs) ||
        GetModuleFileNameW(
            NULL,
            paths->executable,
            (DWORD)_countof(paths->executable)) == 0u ||
        !dio_join(
            paths->font,
            _countof(paths->font),
            executable_directory,
            L"Vazirmatn-Variable.ttf")) {
        goto cleanup;
    }
    result = true;

cleanup:
    return result;
}

static void dio_settings_defaults(
    const DioPaths *paths,
    DioSettings *settings) {
    ZeroMemory(settings, sizeof(*settings));
    settings->persian = true;
    settings->wake_sensitivity = 0.5f;
    settings->vad_threshold = 0.55f;
    settings->vad_hysteresis = 0.15f;
    settings->command_silence_ms = 1100u;
    settings->command_start_timeout_ms = 6000u;
    settings->command_max_ms = 15000u;
    settings->follow_up_ms = 4000u;
    (void)wcscpy_s(settings->model_dir, _countof(settings->model_dir), paths->models);
    (void)wcscpy_s(settings->runtime_dir, _countof(settings->runtime_dir), paths->runtime);
    (void)wcscpy_s(
        settings->announcement_dir,
        _countof(settings->announcement_dir),
        paths->announce);
}

static wchar_t *dio_text_duplicate(const wchar_t *source) {
    wchar_t *copy;
    size_t length;
    if (source == NULL) {
        return NULL;
    }
    length = wcslen(source);
    if (!dio_system_prompt_valid(source)) {
        return NULL;
    }
    copy = (wchar_t *)malloc((length + 1u) * sizeof(*copy));
    if (copy != NULL) {
        memcpy(copy, source, (length + 1u) * sizeof(*copy));
    }
    return copy;
}

void dio_agent_profile_init(DioAgentProfile *profile) {
    if (profile == NULL) {
        return;
    }
    ZeroMemory(profile, sizeof(*profile));
    profile->system_prompt = dio_text_duplicate(DIO_DEFAULT_SYSTEM_PROMPT);
}

void dio_agent_profile_free(DioAgentProfile *profile) {
    size_t index;
    if (profile == NULL) {
        return;
    }
    if (profile->system_prompt != NULL) {
        SecureZeroMemory(
            profile->system_prompt,
            (wcslen(profile->system_prompt) + 1u) * sizeof(wchar_t));
        free(profile->system_prompt);
    }
    for (index = 0u; index < DIO_AGENT_MCP_MAX; ++index) {
        free(profile->mcp_servers[index].always_tools);
        SecureZeroMemory(
            profile->mcp_servers[index].secret_value,
            sizeof(profile->mcp_servers[index].secret_value));
    }
    SecureZeroMemory(profile, sizeof(*profile));
}

bool dio_agent_profile_copy(
    DioAgentProfile *target,
    const DioAgentProfile *source) {
    DioAgentProfile *copy;
    size_t index;
    if (target == NULL || source == NULL || target == source) {
        return target == source && target != NULL;
    }
    if (source->mcp_server_count > DIO_AGENT_MCP_MAX) {
        return false;
    }
    copy = (DioAgentProfile *)malloc(sizeof(*copy));
    if (copy == NULL) {
        return false;
    }
    *copy = *source;
    copy->system_prompt = NULL;
    for (index = 0u; index < DIO_AGENT_MCP_MAX; ++index) {
        copy->mcp_servers[index].always_tools = NULL;
    }
    copy->system_prompt = dio_text_duplicate(source->system_prompt);
    if (copy->system_prompt == NULL) {
        SecureZeroMemory(copy, sizeof(*copy));
        free(copy);
        return false;
    }
    for (index = 0u; index < source->mcp_server_count; ++index) {
        const wchar_t *always = source->mcp_servers[index].always_tools;
        size_t length;
        if (always == NULL) {
            continue;
        }
        length = wcslen(always);
        if (length >= DIO_AGENT_MCP_ALWAYS_CHARS) {
            dio_agent_profile_free(copy);
            free(copy);
            return false;
        }
        copy->mcp_servers[index].always_tools =
            (wchar_t *)malloc((length + 1u) * sizeof(wchar_t));
        if (copy->mcp_servers[index].always_tools == NULL) {
            dio_agent_profile_free(copy);
            free(copy);
            return false;
        }
        memcpy(
            copy->mcp_servers[index].always_tools,
            always,
            (length + 1u) * sizeof(wchar_t));
    }
    dio_agent_profile_free(target);
    *target = *copy;
    SecureZeroMemory(copy, sizeof(*copy));
    free(copy);
    return true;
}

bool dio_agent_profile_configured(const DioAgentProfile *profile) {
    return profile != NULL &&
        profile->base_url[0] != L'\0' &&
        profile->model[0] != L'\0';
}

bool dio_agent_profile_session_secrets_ready(
    const DioAgentProfile *profile,
    const wchar_t *provider_api_key) {
    size_t index;
    if (profile == NULL) {
        return false;
    }
    if (profile->api_key_secret_id[0] != L'\0' &&
        (provider_api_key == NULL || provider_api_key[0] == L'\0')) {
        return false;
    }
    for (index = 0u; index < profile->mcp_server_count; ++index) {
        const DioMcpServer *server = &profile->mcp_servers[index];
        if (server->enabled && server->secret_id[0] != L'\0' &&
            server->secret_value[0] == L'\0') {
            return false;
        }
    }
    return true;
}

bool dio_agent_profile_detach_api_key_secret(
    DioAgentProfile *profile,
    wchar_t *detached_id,
    size_t detached_capacity) {
    if (profile == NULL || detached_id == NULL || detached_capacity == 0u) {
        return false;
    }
    if (profile->api_key_secret_id[0] != L'\0') {
        if (detached_id[0] == L'\0' &&
            wcscpy_s(
                detached_id,
                detached_capacity,
                profile->api_key_secret_id) != 0) {
            return false;
        }
        SecureZeroMemory(
            profile->api_key_secret_id,
            sizeof(profile->api_key_secret_id));
    }
    return detached_id[0] != L'\0';
}

bool dio_model_discovery_deadline_reached(
    unsigned long long deadline,
    unsigned long long now) {
    return deadline != 0u && now >= deadline;
}

static bool dio_read_file(
    const wchar_t *path,
    char **output,
    size_t *output_size) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0u;
    char *buffer;
    bool result = false;

    *output = NULL;
    *output_size = 0u;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 ||
        size.QuadPart > DIO_SETTINGS_MAX_BYTES) {
        goto cleanup;
    }
    buffer = (char *)malloc((size_t)size.QuadPart + 1u);
    if (buffer == NULL) {
        goto cleanup;
    }
    if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL) ||
        read != (DWORD)size.QuadPart) {
        free(buffer);
        goto cleanup;
    }
    buffer[read] = '\0';
    *output = buffer;
    *output_size = read;
    result = true;

cleanup:
    CloseHandle(file);
    return result;
}

static void dio_json_wide(
    yyjson_val *root,
    const char *name,
    wchar_t *target,
    size_t capacity) {
    yyjson_val *value = yyjson_obj_get(root, name);
    const char *text = yyjson_get_str(value);
    if (text != NULL) {
        (void)dio_utf8_to_wide(text, target, capacity);
    }
}

static unsigned int dio_json_uint(
    yyjson_val *root,
    const char *name,
    unsigned int fallback) {
    yyjson_val *value = yyjson_obj_get(root, name);
    uint64_t number;
    if (value == NULL || !yyjson_is_uint(value)) {
        return fallback;
    }
    number = yyjson_get_uint(value);
    return number <= UINT_MAX ? (unsigned int)number : fallback;
}

static float dio_json_float(
    yyjson_val *root,
    const char *name,
    float fallback) {
    yyjson_val *value = yyjson_obj_get(root, name);
    double number;
    if (value == NULL || !yyjson_is_num(value)) {
        return fallback;
    }
    number = yyjson_get_num(value);
    return number >= -1000000.0 && number <= 1000000.0
        ? (float)number
        : fallback;
}

static wchar_t *dio_utf8_duplicate(const char *source) {
    wchar_t *text;
    int required;
    if (source == NULL ||
        strlen(source) > DIO_SYSTEM_PROMPT_MAX_UTF8_BYTES) {
        return NULL;
    }
    required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source,
        -1,
        NULL,
        0);
    if (required <= 0 ||
        required > DIO_SYSTEM_PROMPT_MAX_CHARS + 1) {
        return NULL;
    }
    text = (wchar_t *)malloc((size_t)required * sizeof(*text));
    if (text == NULL ||
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            source,
            -1,
            text,
            required) != required) {
        free(text);
        return NULL;
    }
    return text;
}

static bool dio_option_valid(const wchar_t *value) {
    return value != NULL &&
        wcschr(value, L'\r') == NULL &&
        wcschr(value, L'\n') == NULL;
}

static bool dio_secret_id_valid(const wchar_t *value) {
    const wchar_t *cursor;
    if (value == NULL) {
        return false;
    }
    for (cursor = value; *cursor != L'\0'; ++cursor) {
        const wchar_t character = *cursor;
        if (!((character >= L'a' && character <= L'z') ||
              (character >= L'A' && character <= L'Z') ||
              (character >= L'0' && character <= L'9') ||
              character == L'.' || character == L'_' ||
              character == L'-' || character == L':' ||
              character == L'{' || character == L'}')) {
            return false;
        }
    }
    return true;
}

static bool dio_mcp_always_valid(const wchar_t *value) {
    const wchar_t *line;
    const wchar_t *cursor;
    size_t count = 0u;
    if (value == NULL || value[0] == L'\0') {
        return true;
    }
    if (wcslen(value) >= DIO_AGENT_MCP_ALWAYS_CHARS ||
        wcschr(value, L'\r') != NULL) {
        return false;
    }
    line = value;
    for (cursor = value; ; ++cursor) {
        if (*cursor != L'\n' && *cursor != L'\0') {
            continue;
        }
        if (cursor == line || ++count > DIO_AGENT_MCP_ALWAYS_MAX) {
            return false;
        }
        if (*cursor == L'\0') {
            return true;
        }
        line = cursor + 1;
    }
}

static bool dio_agent_profile_valid(const DioAgentProfile *profile) {
    size_t index;
    if (profile == NULL ||
        !dio_system_prompt_valid(profile->system_prompt) ||
        !dio_option_valid(profile->reasoning_effort) ||
        !dio_option_valid(profile->service_tier) ||
        profile->mcp_server_count > DIO_AGENT_MCP_MAX) {
        return false;
    }
    for (index = 0u; index < profile->mcp_server_count; ++index) {
        const DioMcpServer *server = &profile->mcp_servers[index];
        size_t other;
        if (server->name[0] == L'\0' || server->target[0] == L'\0' ||
            !dio_option_valid(server->name) ||
            !dio_option_valid(server->target) ||
            !dio_option_valid(server->arguments) ||
            !dio_option_valid(server->working_directory) ||
            !dio_option_valid(server->secret_id) ||
            !dio_secret_id_valid(server->secret_id) ||
            !dio_mcp_always_valid(server->always_tools)) {
            return false;
        }
        for (other = 0u; other < index; ++other) {
            if (_wcsicmp(
                    server->name,
                    profile->mcp_servers[other].name) == 0) {
                return false;
            }
        }
    }
    return true;
}

static bool dio_json_wide_checked(
    yyjson_val *root,
    const char *name,
    wchar_t *target,
    size_t capacity) {
    yyjson_val *value = yyjson_obj_get(root, name);
    const char *text;
    if (value == NULL || yyjson_is_null(value)) {
        target[0] = L'\0';
        return true;
    }
    text = yyjson_get_str(value);
    return text != NULL && dio_utf8_to_wide(text, target, capacity) != 0;
}

static bool dio_mcp_parse_always(
    yyjson_val *server,
    wchar_t **output) {
    yyjson_val *array = yyjson_obj_get(server, "always");
    yyjson_val *value;
    size_t index;
    size_t maximum;
    size_t total = 0u;
    wchar_t *result;
    wchar_t *cursor;
    if (output == NULL) {
        return false;
    }
    *output = NULL;
    if (array == NULL || yyjson_is_null(array)) {
        return true;
    }
    if (!yyjson_is_arr(array) ||
        yyjson_arr_size(array) > DIO_AGENT_MCP_ALWAYS_MAX) {
        return false;
    }
    yyjson_arr_foreach(array, index, maximum, value) {
        const char *text = yyjson_get_str(value);
        int required;
        if (text == NULL || text[0] == '\0' ||
            strchr(text, '\r') != NULL || strchr(text, '\n') != NULL) {
            return false;
        }
        required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            -1,
            NULL,
            0);
        if (required <= 1 ||
            total + (size_t)required > DIO_AGENT_MCP_ALWAYS_CHARS) {
            return false;
        }
        total += (size_t)required - 1u;
        if (index + 1u < maximum) {
            total += 1u;
        }
    }
    if (total == 0u) {
        return true;
    }
    result = (wchar_t *)malloc((total + 1u) * sizeof(*result));
    if (result == NULL) {
        return false;
    }
    cursor = result;
    yyjson_arr_foreach(array, index, maximum, value) {
        const char *text = yyjson_get_str(value);
        const int capacity = (int)(total + 1u - (size_t)(cursor - result));
        const int written = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            -1,
            cursor,
            capacity);
        if (written <= 1) {
            free(result);
            return false;
        }
        cursor += written - 1;
        if (index + 1u < maximum) {
            *cursor++ = L'\n';
        }
    }
    *cursor = L'\0';
    *output = result;
    return true;
}

static bool dio_agent_profile_parse(
    yyjson_val *root,
    DioAgentProfile *profile) {
    yyjson_val *agent;
    yyjson_val *provider;
    yyjson_val *servers;
    yyjson_val *server;
    size_t index;
    size_t maximum;
    const char *prompt;
    wchar_t *prompt_copy;
    if (profile == NULL || profile->system_prompt == NULL) {
        return false;
    }
    agent = yyjson_obj_get(root, "agent");
    if (!yyjson_is_obj(agent)) {
        return true;
    }
    provider = yyjson_obj_get(agent, "provider");
    if (yyjson_is_obj(provider)) {
        dio_json_wide(
            provider,
            "base_url",
            profile->base_url,
            _countof(profile->base_url));
        dio_json_wide(
            provider,
            "model",
            profile->model,
            _countof(profile->model));
        dio_json_wide(
            provider,
            "api_key_secret_id",
            profile->api_key_secret_id,
            _countof(profile->api_key_secret_id));
        dio_json_wide(
            provider,
            "reasoning_effort",
            profile->reasoning_effort,
            _countof(profile->reasoning_effort));
        dio_json_wide(
            provider,
            "service_tier",
            profile->service_tier,
            _countof(profile->service_tier));
    }
    {
        yyjson_val *prompts = yyjson_obj_get(agent, "prompts");
        prompt = yyjson_is_obj(prompts)
            ? yyjson_get_str(yyjson_obj_get(prompts, "system"))
            : NULL;
    }
    if (prompt != NULL) {
        prompt_copy = dio_utf8_duplicate(prompt);
        if (prompt_copy == NULL) {
            return false;
        }
        SecureZeroMemory(
            profile->system_prompt,
            (wcslen(profile->system_prompt) + 1u) * sizeof(wchar_t));
        free(profile->system_prompt);
        profile->system_prompt = prompt_copy;
    }
    servers = yyjson_obj_get(agent, "mcp_servers");
    if (servers != NULL && !yyjson_is_arr(servers)) {
        return false;
    }
    profile->mcp_server_count = 0u;
    if (servers != NULL) {
        yyjson_arr_foreach(servers, index, maximum, server) {
            DioMcpServer *target;
            yyjson_val *enabled;
            yyjson_val *transport_value;
            yyjson_val *secret_value;
            const char *transport;
            if (profile->mcp_server_count >= DIO_AGENT_MCP_MAX ||
                !yyjson_is_obj(server)) {
                return false;
            }
            target = &profile->mcp_servers[profile->mcp_server_count];
            ZeroMemory(target, sizeof(*target));
            target->enabled = true;
            transport_value = yyjson_obj_get(server, "transport");
            transport = yyjson_get_str(transport_value);
            if (transport_value != NULL && !yyjson_is_null(transport_value) &&
                transport == NULL) {
                return false;
            }
            if (transport != NULL) {
                if (strcmp(transport, "stdio") == 0) {
                    target->stdio = true;
                } else if (strcmp(transport, "http") != 0) {
                    return false;
                }
            }
            if (!dio_json_wide_checked(
                    server,
                    "name",
                    target->name,
                    _countof(target->name)) ||
                !dio_json_wide_checked(
                    server,
                    target->stdio ? "exe" : "url",
                    target->target,
                    _countof(target->target)) ||
                (target->stdio &&
                 (!dio_json_wide_checked(
                      server,
                      "args",
                      target->arguments,
                      _countof(target->arguments)) ||
                  !dio_json_wide_checked(
                      server,
                      "cwd",
                      target->working_directory,
                      _countof(target->working_directory)))) ||
                !dio_json_wide_checked(
                    server,
                    "secret_id",
                    target->secret_id,
                    _countof(target->secret_id)) ||
                !dio_mcp_parse_always(server, &target->always_tools)) {
                return false;
            }
            secret_value = yyjson_obj_get(server, "secret_id");
            if (secret_value == NULL || yyjson_is_null(secret_value)) {
                if (!dio_json_wide_checked(
                        server,
                        "token_secret",
                        target->secret_id,
                        _countof(target->secret_id))) {
                    return false;
                }
                if (wcsncmp(target->secret_id, L"mcp.", 4u) != 0 ||
                    !dio_secret_id_valid(target->secret_id)) {
                    target->secret_id[0] = L'\0';
                }
            }
            enabled = yyjson_obj_get(server, "enabled");
            if (enabled != NULL && !yyjson_is_null(enabled) &&
                !yyjson_is_bool(enabled)) {
                return false;
            }
            if (yyjson_is_bool(enabled)) {
                target->enabled = yyjson_get_bool(enabled);
            }
            profile->mcp_server_count += 1u;
        }
    }
    return dio_agent_profile_valid(profile);
}

static bool dio_settings_valid(const DioSettings *settings) {
    return
        settings->wake_sensitivity >= 0.0f &&
        settings->wake_sensitivity <= 1.0f &&
        settings->vad_threshold >= 0.0f &&
        settings->vad_threshold <= 1.0f &&
        settings->vad_hysteresis >= 0.0f &&
        settings->vad_hysteresis <= settings->vad_threshold &&
        settings->command_silence_ms >= 100u &&
        settings->command_start_timeout_ms >= 100u &&
        settings->command_max_ms >= settings->command_start_timeout_ms &&
        settings->follow_up_ms <= 60000u &&
        settings->model_dir[0] != L'\0' &&
        settings->runtime_dir[0] != L'\0';
}

bool dio_settings_load_all(
    const DioPaths *paths,
    DioSettings *settings,
    DioAgentProfile *profile,
    wchar_t *error,
    size_t error_capacity) {
    char *data = NULL;
    size_t data_size = 0u;
    yyjson_doc *document = NULL;
    yyjson_val *root;
    const char *locale;

    if (paths == NULL || settings == NULL || profile == NULL) {
        dio_error(error, error_capacity, L"Invalid settings arguments.");
        return false;
    }
    dio_settings_defaults(paths, settings);
    dio_agent_profile_init(profile);
    if (profile->system_prompt == NULL) {
        dio_error(error, error_capacity, L"Out of memory.");
        return false;
    }
    if (GetFileAttributesW(paths->settings) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    if (!dio_read_file(paths->settings, &data, &data_size)) {
        dio_error(error, error_capacity, L"Could not read settings.json.");
        dio_agent_profile_free(profile);
        return false;
    }
    document = yyjson_read(data, data_size, YYJSON_READ_NOFLAG);
    free(data);
    if (document == NULL) {
        dio_error(error, error_capacity, L"settings.json is not valid JSON.");
        dio_agent_profile_free(profile);
        return false;
    }
    root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root)) {
        dio_error(error, error_capacity, L"settings.json must contain an object.");
        yyjson_doc_free(document);
        dio_agent_profile_free(profile);
        return false;
    }
    locale = yyjson_get_str(yyjson_obj_get(root, "locale"));
    if (locale != NULL) {
        settings->persian = strcmp(locale, "fa") == 0;
    }
    settings->reduced_motion =
        yyjson_get_bool(yyjson_obj_get(root, "reduced_motion"));
    dio_json_wide(
        root,
        "microphone_name",
        settings->microphone_name,
        _countof(settings->microphone_name));
    dio_json_wide(
        root,
        "microphone_id",
        settings->microphone_id,
        _countof(settings->microphone_id));
    settings->wake_sensitivity =
        dio_json_float(
            root,
            "wake_sensitivity",
            dio_json_float(
                root,
                "wake_threshold",
                settings->wake_sensitivity));
    settings->vad_threshold =
        dio_json_float(root, "vad_threshold", settings->vad_threshold);
    settings->vad_hysteresis =
        dio_json_float(root, "vad_hysteresis", settings->vad_hysteresis);
    settings->command_silence_ms =
        dio_json_uint(root, "command_silence_ms", settings->command_silence_ms);
    settings->command_start_timeout_ms =
        dio_json_uint(
            root,
            "command_start_timeout_ms",
            settings->command_start_timeout_ms);
    settings->command_max_ms =
        dio_json_uint(root, "command_max_ms", settings->command_max_ms);
    settings->follow_up_ms =
        dio_json_uint(root, "follow_up_ms", settings->follow_up_ms);
    if (!dio_agent_profile_parse(root, profile)) {
        yyjson_doc_free(document);
        dio_error(error, error_capacity, L"settings.json contains an invalid agent profile.");
        dio_agent_profile_free(profile);
        return false;
    }
    yyjson_doc_free(document);
    if (!dio_settings_valid(settings)) {
        dio_error(error, error_capacity, L"settings.json contains an out-of-range value.");
        dio_settings_defaults(paths, settings);
        dio_agent_profile_free(profile);
        return false;
    }
    return true;
}

bool dio_settings_load(
    const DioPaths *paths,
    DioSettings *settings,
    wchar_t *error,
    size_t error_capacity) {
    DioAgentProfile profile;
    bool result;
    ZeroMemory(&profile, sizeof(profile));
    result = dio_settings_load_all(
        paths,
        settings,
        &profile,
        error,
        error_capacity);
    dio_agent_profile_free(&profile);
    return result;
}

static bool dio_mut_add_wide(
    yyjson_mut_doc *document,
    yyjson_mut_val *root,
    const char *name,
    const wchar_t *value) {
    int bytes;
    char *utf8;
    bool result;

    bytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (bytes <= 0) {
        return false;
    }
    utf8 = (char *)malloc((size_t)bytes);
    if (utf8 == NULL ||
        !dio_wide_to_utf8(value, utf8, (size_t)bytes)) {
        free(utf8);
        return false;
    }
    result = yyjson_mut_obj_add_strcpy(document, root, name, utf8);
    free(utf8);
    return result;
}

static bool dio_mut_add_nullable_wide(
    yyjson_mut_doc *document,
    yyjson_mut_val *root,
    const char *name,
    const wchar_t *value) {
    return value != NULL && value[0] != L'\0'
        ? dio_mut_add_wide(document, root, name, value)
        : yyjson_mut_obj_add_null(document, root, name);
}

static bool dio_mut_add_always(
    yyjson_mut_doc *document,
    yyjson_mut_val *root,
    const wchar_t *value) {
    yyjson_mut_val *array = yyjson_mut_arr(document);
    const wchar_t *line;
    if (array == NULL) {
        return false;
    }
    line = value;
    while (line != NULL && *line != L'\0') {
        const wchar_t *end = wcschr(line, L'\n');
        const size_t length = end != NULL
            ? (size_t)(end - line)
            : wcslen(line);
        int bytes;
        char *utf8;
        if (length == 0u || length > INT_MAX) {
            return false;
        }
        bytes = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            line,
            (int)length,
            NULL,
            0,
            NULL,
            NULL);
        if (bytes <= 0) {
            return false;
        }
        utf8 = (char *)malloc((size_t)bytes + 1u);
        if (utf8 == NULL ||
            WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                line,
                (int)length,
                utf8,
                bytes,
                NULL,
                NULL) != bytes) {
            free(utf8);
            return false;
        }
        utf8[bytes] = '\0';
        if (!yyjson_mut_arr_add_strcpy(document, array, utf8)) {
            free(utf8);
            return false;
        }
        free(utf8);
        line = end != NULL ? end + 1 : NULL;
    }
    return yyjson_mut_obj_add_val(document, root, "always", array);
}

static bool dio_mut_add_agent_profile(
    yyjson_mut_doc *document,
    yyjson_mut_val *root,
    const DioAgentProfile *profile) {
    yyjson_mut_val *agent;
    yyjson_mut_val *provider;
    yyjson_mut_val *prompts;
    yyjson_mut_val *servers;
    size_t index;
    if (!dio_agent_profile_valid(profile)) {
        return false;
    }
    agent = yyjson_mut_obj(document);
    provider = yyjson_mut_obj(document);
    prompts = yyjson_mut_obj(document);
    servers = yyjson_mut_arr(document);
    if (agent == NULL || provider == NULL || prompts == NULL || servers == NULL ||
        !dio_mut_add_wide(document, provider, "base_url", profile->base_url) ||
        !dio_mut_add_nullable_wide(document, provider, "model", profile->model) ||
        !dio_mut_add_nullable_wide(
            document,
            provider,
            "api_key_secret_id",
            profile->api_key_secret_id) ||
        !dio_mut_add_nullable_wide(
            document,
            provider,
            "reasoning_effort",
            profile->reasoning_effort) ||
        !dio_mut_add_nullable_wide(
            document,
            provider,
            "service_tier",
            profile->service_tier) ||
        !yyjson_mut_obj_add_val(document, agent, "provider", provider) ||
        !dio_mut_add_wide(
            document,
            prompts,
            "system",
            profile->system_prompt) ||
        !yyjson_mut_obj_add_val(document, agent, "prompts", prompts)) {
        return false;
    }
    for (index = 0u; index < profile->mcp_server_count; ++index) {
        const DioMcpServer *source = &profile->mcp_servers[index];
        yyjson_mut_val *server = yyjson_mut_obj(document);
        if (server == NULL ||
            !yyjson_mut_obj_add_bool(
                document,
                server,
                "enabled",
                source->enabled) ||
            !dio_mut_add_wide(document, server, "name", source->name) ||
            !yyjson_mut_obj_add_str(
                document,
                server,
                "transport",
                source->stdio ? "stdio" : "http") ||
            !(source->stdio
                ? (dio_mut_add_wide(
                       document,
                       server,
                       "exe",
                       source->target) &&
                   dio_mut_add_wide(
                       document,
                       server,
                       "args",
                       source->arguments) &&
                   dio_mut_add_wide(
                       document,
                       server,
                       "cwd",
                       source->working_directory))
                : dio_mut_add_wide(
                      document,
                      server,
                      "url",
                      source->target)) ||
            !dio_mut_add_nullable_wide(
                document,
                server,
                "secret_id",
                source->secret_id) ||
            !dio_mut_add_always(document, server, source->always_tools) ||
            !yyjson_mut_arr_append(servers, server)) {
            return false;
        }
    }
    return yyjson_mut_obj_add_val(document, agent, "mcp_servers", servers) &&
        yyjson_mut_obj_add_val(document, root, "agent", agent);
}

static bool dio_write_atomic(
    const wchar_t *path,
    const char *data,
    size_t size) {
    wchar_t temporary[MAX_PATH];
    HANDLE file;
    DWORD written = 0u;
    bool result = false;

    if (swprintf_s(temporary, _countof(temporary), L"%ls.tmp", path) < 0) {
        return false;
    }
    file = CreateFileW(
        temporary,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (size <= UINT32_MAX &&
        WriteFile(file, data, (DWORD)size, &written, NULL) &&
        written == (DWORD)size &&
        FlushFileBuffers(file)) {
        result = true;
    }
    CloseHandle(file);
    if (!result ||
        !MoveFileExW(
            temporary,
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary);
        return false;
    }
    return true;
}

bool dio_settings_save_all(
    const DioPaths *paths,
    const DioSettings *settings,
    const DioAgentProfile *profile,
    wchar_t *error,
    size_t error_capacity) {
    yyjson_mut_doc *document;
    yyjson_mut_val *root;
    char *json;
    size_t size = 0u;
    bool built;
    bool result;

    if (paths == NULL || settings == NULL || profile == NULL ||
        !dio_settings_valid(settings) || !dio_agent_profile_valid(profile)) {
        dio_error(error, error_capacity, L"Settings are invalid.");
        return false;
    }
    document = yyjson_mut_doc_new(NULL);
    if (document == NULL) {
        dio_error(error, error_capacity, L"Out of memory.");
        return false;
    }
    root = yyjson_mut_obj(document);
    yyjson_mut_doc_set_root(document, root);
    built =
        yyjson_mut_obj_add_uint(
            document,
            root,
            "schema",
            DIO_SETTINGS_SCHEMA) &&
        yyjson_mut_obj_add_str(document, root, "locale", settings->persian ? "fa" : "en") &&
        yyjson_mut_obj_add_bool(document, root, "reduced_motion", settings->reduced_motion) &&
        dio_mut_add_wide(document, root, "microphone_name", settings->microphone_name) &&
        dio_mut_add_wide(document, root, "microphone_id", settings->microphone_id) &&
        yyjson_mut_obj_add_real(document, root, "wake_sensitivity", settings->wake_sensitivity) &&
        yyjson_mut_obj_add_real(document, root, "vad_threshold", settings->vad_threshold) &&
        yyjson_mut_obj_add_real(document, root, "vad_hysteresis", settings->vad_hysteresis) &&
        yyjson_mut_obj_add_uint(
            document,
            root,
            "command_silence_ms",
            settings->command_silence_ms) &&
        yyjson_mut_obj_add_uint(
            document,
            root,
            "command_start_timeout_ms",
            settings->command_start_timeout_ms) &&
        yyjson_mut_obj_add_uint(document, root, "command_max_ms", settings->command_max_ms) &&
        yyjson_mut_obj_add_uint(document, root, "follow_up_ms", settings->follow_up_ms) &&
        dio_mut_add_agent_profile(document, root, profile);
    json = built
        ? yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &size)
        : NULL;
    result = json != NULL && dio_write_atomic(paths->settings, json, size);
    free(json);
    yyjson_mut_doc_free(document);
    if (!result) {
        dio_error(error, error_capacity, L"Could not save settings.json.");
    }
    return result;
}

bool dio_settings_save(
    const DioPaths *paths,
    const DioSettings *settings,
    wchar_t *error,
    size_t error_capacity) {
    DioAgentProfile profile;
    DioSettings ignored;
    wchar_t ignored_error[128];
    bool result;
    ZeroMemory(&profile, sizeof(profile));
    if (!dio_settings_load_all(
            paths,
            &ignored,
            &profile,
            ignored_error,
            _countof(ignored_error))) {
        dio_agent_profile_init(&profile);
        if (profile.system_prompt == NULL) {
            dio_error(error, error_capacity, L"Out of memory.");
            return false;
        }
    }
    result = dio_settings_save_all(
        paths,
        settings,
        &profile,
        error,
        error_capacity);
    dio_agent_profile_free(&profile);
    return result;
}

void dio_metric(
    const DioPaths *paths,
    const char *event_name,
    unsigned long long duration_ms,
    unsigned long code) {
    wchar_t path[MAX_PATH];
    HANDLE file;
    SYSTEMTIME now;
    char line[512];
    int length;
    DWORD written;

    if (paths == NULL ||
        event_name == NULL ||
        strchr(event_name, '"') != NULL ||
        !dio_join(path, _countof(path), paths->logs, L"metrics.jsonl")) {
        return;
    }
    file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    GetSystemTime(&now);
    length = snprintf(
        line,
        sizeof(line),
        "{\"time\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
        "\"event\":\"%s\",\"duration_ms\":%llu,\"code\":%lu}\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        event_name,
        duration_ms,
        code);
    if (length > 0 && (size_t)length < sizeof(line)) {
        (void)WriteFile(file, line, (DWORD)length, &written, NULL);
    }
    CloseHandle(file);
}

static bool dio_run_marker_path(
    const DioPaths *paths,
    wchar_t *path,
    size_t capacity) {
    if (paths == NULL ||
        !dio_join(path, capacity, paths->logs, L"run.marker")) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return true;
}

bool dio_run_marker_start(
    const DioPaths *paths,
    bool *previous_unclean_exit) {
    wchar_t path[MAX_PATH];
    DWORD attributes;
    DWORD error;
    HANDLE file;

    if (previous_unclean_exit == NULL ||
        !dio_run_marker_path(paths, path, _countof(path))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_PATH_NOT_FOUND) {
            return false;
        }
        *previous_unclean_exit = false;
    } else {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            SetLastError(ERROR_DIRECTORY);
            return false;
        }
        *previous_unclean_exit = true;
    }
    file = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_WRITE_THROUGH,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

bool dio_run_marker_clean(const DioPaths *paths) {
    wchar_t path[MAX_PATH];
    DWORD error;

    if (!dio_run_marker_path(paths, path, _countof(path))) {
        return false;
    }
    if (DeleteFileW(path)) {
        return true;
    }
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND ||
        error == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    SetLastError(error);
    return false;
}
