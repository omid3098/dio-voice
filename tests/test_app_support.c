#include "app_support.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void write_json(
    const wchar_t *path,
    const char *json) {
    HANDLE file;
    DWORD written = 0u;
    const DWORD length = (DWORD)strlen(json);

    file = CreateFileW(
        path,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(file, json, length, &written, NULL));
    assert(written == length);
    CloseHandle(file);
}

static char *read_json(const wchar_t *path) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0u;
    char *result;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(GetFileSizeEx(file, &size));
    assert(size.QuadPart >= 0 && size.QuadPart < 1024 * 1024);
    result = (char *)malloc((size_t)size.QuadPart + 1u);
    assert(result != NULL);
    assert(ReadFile(file, result, (DWORD)size.QuadPart, &read, NULL));
    assert(read == (DWORD)size.QuadPart);
    result[read] = '\0';
    CloseHandle(file);
    return result;
}

static wchar_t *duplicate_wide(const wchar_t *source) {
    const size_t bytes = (wcslen(source) + 1u) * sizeof(wchar_t);
    wchar_t *result = (wchar_t *)malloc(bytes);
    assert(result != NULL);
    memcpy(result, source, bytes);
    return result;
}

int wmain(void) {
    wchar_t temporary[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t portable_root[MAX_PATH];
    wchar_t expected[MAX_PATH];
    wchar_t error[256];
    DioPaths paths;
    DioPaths isolated_paths;
    DioPaths portable_paths;
    DioSettings settings;
    DioAgentProfile profile;
    DioAgentProfile *copied_profile;
    DioAgentProfile loaded_profile;
    char *saved_json;
    wchar_t *separator;
    DWORD executable_length;

    assert(DIO_MODEL_DISCOVERY_TIMEOUT_MS == 10000ull);
    assert(!dio_model_discovery_deadline_reached(11000u, 10999u));
    assert(dio_model_discovery_deadline_reached(11000u, 11000u));
    assert(!dio_model_discovery_deadline_reached(0u, 50000u));

    executable_length = GetModuleFileNameW(
        NULL,
        temporary,
        (DWORD)_countof(temporary));
    assert(executable_length != 0u);
    assert((size_t)executable_length < _countof(temporary));
    separator = wcsrchr(temporary, L'\\');
    assert(separator != NULL);
    *separator = L'\0';
    assert(swprintf_s(
        directory,
        _countof(directory),
        L"%ls\\ui-smoke-data",
        temporary) > 0);
    assert(dio_paths_initialize(&isolated_paths, true));
    assert(_wcsicmp(isolated_paths.root, directory) == 0);

    assert(GetTempPathW(
        (DWORD)_countof(temporary),
        temporary) != 0u);
    assert(swprintf_s(
        portable_root,
        _countof(portable_root),
        L"%lsdio-portable-root-%lu",
        temporary,
        GetCurrentProcessId()) > 0);
    assert(SetEnvironmentVariableW(L"DIO_ROOT", portable_root));
    assert(dio_paths_initialize(&portable_paths, false));
    assert(SetEnvironmentVariableW(L"DIO_ROOT", NULL));
    assert(wcscmp(portable_paths.root, portable_root) == 0);
    assert(swprintf_s(
        expected,
        _countof(expected),
        L"%ls\\data\\settings.json",
        portable_root) > 0);
    assert(wcscmp(portable_paths.settings, expected) == 0);
    assert(swprintf_s(
        expected,
        _countof(expected),
        L"%ls\\data\\secrets.bin",
        portable_root) > 0);
    assert(wcscmp(portable_paths.secrets, expected) == 0);
    assert(swprintf_s(
        expected,
        _countof(expected),
        L"%ls\\versions\\current\\runtime",
        portable_root) > 0);
    assert(wcscmp(portable_paths.runtime, expected) == 0);
    assert(swprintf_s(
        directory,
        _countof(directory),
        L"%lsdio-voice-settings-%lu",
        temporary,
        GetCurrentProcessId()) > 0);
    assert(CreateDirectoryW(directory, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS);

    ZeroMemory(&paths, sizeof(paths));
    assert(swprintf_s(
        paths.settings,
        _countof(paths.settings),
        L"%ls\\settings.json",
        directory) > 0);
    assert(wcscpy_s(
        paths.models,
        _countof(paths.models),
        directory) == 0);
    assert(wcscpy_s(
        paths.runtime,
        _countof(paths.runtime),
        directory) == 0);
    assert(wcscpy_s(
        paths.workspace,
        _countof(paths.workspace),
        directory) == 0);
    assert(wcscpy_s(
        paths.announce,
        _countof(paths.announce),
        directory) == 0);

    assert(dio_settings_load(
        &paths,
        &settings,
        error,
        _countof(error)));
    assert(settings.follow_up_ms == 4000u);
    assert(settings.wake_sensitivity == 0.5f);

    write_json(
        paths.settings,
        "{\"schema\":1,\"follow_up_ms\":4000}");
    assert(dio_settings_load(
        &paths,
        &settings,
        error,
        _countof(error)));
    assert(settings.follow_up_ms == 4000u);

    write_json(
        paths.settings,
        "{\"schema\":2,\"follow_up_ms\":4000,\"wake_threshold\":0.4,"
        "\"runtime_dir\":\"C:\\\\outside\"}");
    assert(dio_settings_load(
        &paths,
        &settings,
        error,
        _countof(error)));
    assert(settings.follow_up_ms == 4000u);
    assert(settings.microphone_id[0] == L'\0');
    assert(settings.wake_sensitivity == 0.4f);
    assert(wcscmp(settings.runtime_dir, paths.runtime) == 0);
    assert(wcscpy_s(
        settings.microphone_name,
        _countof(settings.microphone_name),
        L"Desk microphone") == 0);
    assert(wcscpy_s(
        settings.microphone_id,
        _countof(settings.microphone_id),
        L"{0.0.1.00000000}.stable-endpoint") == 0);
    assert(dio_settings_save(
        &paths,
        &settings,
        error,
        _countof(error)));
    assert(dio_settings_load(
        &paths,
        &settings,
        error,
        _countof(error)));
    assert(wcscmp(
        settings.microphone_name,
        L"Desk microphone") == 0);
    assert(wcscmp(
        settings.microphone_id,
        L"{0.0.1.00000000}.stable-endpoint") == 0);
    assert(settings.follow_up_ms == 4000u);

    ZeroMemory(&profile, sizeof(profile));
    dio_agent_profile_init(&profile);
    assert(profile.system_prompt != NULL);
    assert(!dio_agent_profile_configured(&profile));
    assert(wcscpy_s(
        profile.base_url,
        _countof(profile.base_url),
        L"https://provider.example/v1") == 0);
    assert(wcscpy_s(
        profile.model,
        _countof(profile.model),
        L"voice-model") == 0);
    assert(wcscpy_s(
        profile.reasoning_effort,
        _countof(profile.reasoning_effort),
        L"high") == 0);
    assert(wcscpy_s(
        profile.service_tier,
        _countof(profile.service_tier),
        L"fast") == 0);
    assert(wcscpy_s(
        profile.api_key_secret_id,
        _countof(profile.api_key_secret_id),
        L"provider.api_key") == 0);
    profile.mcp_server_count = 2u;
    profile.mcp_servers[0].enabled = true;
    assert(wcscpy_s(
        profile.mcp_servers[0].name,
        _countof(profile.mcp_servers[0].name),
        L"memory") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[0].target,
        _countof(profile.mcp_servers[0].target),
        L"https://mcp.example/rpc") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[0].secret_id,
        _countof(profile.mcp_servers[0].secret_id),
        L"mcp.memory.token") == 0);
    profile.mcp_servers[0].always_tools =
        duplicate_wide(L"memory.read\nmemory.write");
    assert(wcscpy_s(
        profile.mcp_servers[0].secret_value,
        _countof(profile.mcp_servers[0].secret_value),
        L"RAW-MCP-SECRET-DO-NOT-SERIALIZE") == 0);
    profile.mcp_servers[1].enabled = true;
    profile.mcp_servers[1].stdio = true;
    assert(wcscpy_s(
        profile.mcp_servers[1].name,
        _countof(profile.mcp_servers[1].name),
        L"local-tools") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[1].target,
        _countof(profile.mcp_servers[1].target),
        L"C:\\tools\\dio-mcp.exe") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[1].arguments,
        _countof(profile.mcp_servers[1].arguments),
        L"--stdio --lang fa") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[1].working_directory,
        _countof(profile.mcp_servers[1].working_directory),
        L"C:\\tools") == 0);
    assert(wcscpy_s(
        profile.mcp_servers[1].secret_id,
        _countof(profile.mcp_servers[1].secret_id),
        L"mcp.local.environment") == 0);
    profile.mcp_servers[1].always_tools =
        duplicate_wide(L"files.read\ncalendar.create");
    assert(!dio_agent_profile_session_secrets_ready(&profile, L""));
    assert(!dio_agent_profile_session_secrets_ready(&profile, L"provider-key"));
    assert(wcscpy_s(
        profile.mcp_servers[1].secret_value,
        _countof(profile.mcp_servers[1].secret_value),
        L"TOKEN=env-secret") == 0);
    assert(dio_agent_profile_session_secrets_ready(&profile, L"provider-key"));
    copied_profile = (DioAgentProfile *)calloc(1u, sizeof(*copied_profile));
    assert(copied_profile != NULL);
    dio_agent_profile_init(copied_profile);
    assert(copied_profile->system_prompt != NULL);
    assert(dio_agent_profile_copy(copied_profile, &profile));
    assert(copied_profile->mcp_servers[0].always_tools !=
           profile.mcp_servers[0].always_tools);
    assert(wcscmp(
        copied_profile->mcp_servers[0].always_tools,
        L"memory.read\nmemory.write") == 0);
    dio_agent_profile_free(copied_profile);
    free(copied_profile);
    {
        wchar_t *valid_prompt = profile.system_prompt;
        wchar_t *oversize_prompt =
            (wchar_t *)malloc(32770u * sizeof(*oversize_prompt));
        assert(oversize_prompt != NULL);
        for (size_t index = 0u; index < 32769u; ++index) {
            oversize_prompt[index] = L'\u0627';
        }
        oversize_prompt[32769] = L'\0';
        profile.system_prompt = oversize_prompt;
        assert(!dio_settings_save_all(
            &paths,
            &settings,
            &profile,
            error,
            _countof(error)));
        SecureZeroMemory(
            oversize_prompt,
            32770u * sizeof(*oversize_prompt));
        free(oversize_prompt);
        profile.system_prompt = valid_prompt;
    }
    assert(dio_settings_save_all(
        &paths,
        &settings,
        &profile,
        error,
        _countof(error)));
    saved_json = read_json(paths.settings);
    assert(strstr(saved_json, "RAW-MCP-SECRET-DO-NOT-SERIALIZE") == NULL);
    assert(strstr(saved_json, "\"transport\": \"http\"") != NULL);
    assert(strstr(saved_json, "\"transport\": \"stdio\"") != NULL);
    free(saved_json);
    assert(dio_agent_profile_configured(&profile));
    dio_agent_profile_free(&profile);
    assert(profile.mcp_servers[0].secret_value[0] == L'\0');

    ZeroMemory(&loaded_profile, sizeof(loaded_profile));
    assert(dio_settings_load_all(
        &paths,
        &settings,
        &loaded_profile,
        error,
        _countof(error)));
    assert(dio_agent_profile_configured(&loaded_profile));
    assert(wcscmp(
        loaded_profile.base_url,
        L"https://provider.example/v1") == 0);
    assert(wcscmp(loaded_profile.model, L"voice-model") == 0);
    assert(wcscmp(loaded_profile.reasoning_effort, L"high") == 0);
    assert(wcscmp(loaded_profile.service_tier, L"fast") == 0);
    assert(loaded_profile.mcp_server_count == 2u);
    assert(!loaded_profile.mcp_servers[0].stdio);
    assert(wcscmp(
        loaded_profile.mcp_servers[0].target,
        L"https://mcp.example/rpc") == 0);
    assert(wcscmp(
        loaded_profile.mcp_servers[0].secret_id,
        L"mcp.memory.token") == 0);
    assert(wcscmp(
        loaded_profile.mcp_servers[0].always_tools,
        L"memory.read\nmemory.write") == 0);
    assert(loaded_profile.mcp_servers[0].secret_value[0] == L'\0');
    assert(loaded_profile.mcp_servers[1].stdio);
    assert(wcscmp(
        loaded_profile.mcp_servers[1].target,
        L"C:\\tools\\dio-mcp.exe") == 0);
    assert(wcscmp(
        loaded_profile.mcp_servers[1].arguments,
        L"--stdio --lang fa") == 0);
    assert(wcscmp(
        loaded_profile.mcp_servers[1].working_directory,
        L"C:\\tools") == 0);
    assert(wcscmp(
        loaded_profile.mcp_servers[1].always_tools,
        L"files.read\ncalendar.create") == 0);
    dio_agent_profile_free(&loaded_profile);

    write_json(
        paths.settings,
        "{\"schema\":4,\"agent\":{\"mcp_servers\":["
        "{\"name\":\"legacy-id\",\"url\":\"https://mcp.example/id\","
        "\"token_secret\":\"mcp.legacy.token\"},"
        "{\"name\":\"legacy-raw\",\"url\":\"https://mcp.example/raw\","
        "\"token_secret\":\"Bearer raw token\"}]}}");
    ZeroMemory(&loaded_profile, sizeof(loaded_profile));
    assert(dio_settings_load_all(
        &paths,
        &settings,
        &loaded_profile,
        error,
        _countof(error)));
    assert(loaded_profile.mcp_server_count == 2u);
    assert(wcscmp(
        loaded_profile.mcp_servers[0].secret_id,
        L"mcp.legacy.token") == 0);
    assert(loaded_profile.mcp_servers[1].secret_id[0] == L'\0');
    dio_agent_profile_free(&loaded_profile);

    assert(DeleteFileW(paths.settings));
    assert(RemoveDirectoryW(directory));
    assert(RemoveDirectoryW(portable_paths.runtime));
    assert(swprintf_s(expected, _countof(expected), L"%ls\\versions\\current", portable_root) > 0);
    assert(RemoveDirectoryW(expected));
    assert(swprintf_s(expected, _countof(expected), L"%ls\\versions", portable_root) > 0);
    assert(RemoveDirectoryW(expected));
    assert(RemoveDirectoryW(portable_paths.workspace));
    assert(RemoveDirectoryW(portable_paths.announce));
    assert(swprintf_s(expected, _countof(expected), L"%ls\\data", portable_root) > 0);
    assert(RemoveDirectoryW(expected));
    assert(RemoveDirectoryW(portable_paths.models));
    assert(RemoveDirectoryW(portable_paths.logs));
    assert(RemoveDirectoryW(portable_root));
    (void)printf(
        "schema-4 HTTP+stdio MCP roundtrip, transient-secret exclusion and "
        "safe legacy ID migration: PASS\n");
    return 0;
}
