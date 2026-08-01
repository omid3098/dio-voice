#include "vault.h"
#include "app_support.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static void tamper_last_byte(const wchar_t *path) {
    HANDLE file = CreateFileW(
        path,
        GENERIC_READ | GENERIC_WRITE,
        0u,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    LARGE_INTEGER size;
    unsigned char value;
    DWORD transferred;
    assert(file != INVALID_HANDLE_VALUE);
    assert(GetFileSizeEx(file, &size));
    assert(size.QuadPart > 0);
    assert(SetFilePointer(file, -1, NULL, FILE_END) != INVALID_SET_FILE_POINTER);
    assert(ReadFile(file, &value, 1u, &transferred, NULL) && transferred == 1u);
    value ^= 1u;
    assert(SetFilePointer(file, -1, NULL, FILE_CURRENT) != INVALID_SET_FILE_POINTER);
    assert(WriteFile(file, &value, 1u, &transferred, NULL) && transferred == 1u);
    assert(FlushFileBuffers(file));
    assert(CloseHandle(file));
}

int wmain(void) {
    wchar_t temporary[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t copy_path[MAX_PATH];
    wchar_t tamper_path[MAX_PATH];
    wchar_t error[256];
    DioVault vault;
    DioVault copied;
    DioVault tampered;
    DioAgentProfile profile;
    wchar_t detached_id[DIO_AGENT_SECRET_NAME_CAP];

    assert(GetTempPathW((DWORD)_countof(temporary), temporary) != 0u);
    assert(swprintf_s(
        directory,
        _countof(directory),
        L"%lsdio-vault-%lu",
        temporary,
        GetCurrentProcessId()) > 0);
    assert(CreateDirectoryW(directory, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS);
    assert(swprintf_s(
        path,
        _countof(path),
        L"%ls\\secrets.bin",
        directory) > 0);
    assert(swprintf_s(
        copy_path,
        _countof(copy_path),
        L"%ls\\secrets-copy.bin",
        directory) > 0);
    assert(swprintf_s(
        tamper_path,
        _countof(tamper_path),
        L"%ls\\secrets-tamper.bin",
        directory) > 0);
    (void)DeleteFileW(path);
    (void)DeleteFileW(copy_path);
    (void)DeleteFileW(tamper_path);

    dio_vault_init(&vault, path);
    assert(!dio_vault_exists(&vault));
    assert(!dio_vault_create(
        &vault,
        L"too-short",
        error,
        _countof(error)));
    assert(dio_vault_create(
        &vault,
        L"first-password",
        error,
        _countof(error)));
    assert(dio_vault_set(
        &vault,
        L"provider.api_key",
        L"sk-test-secret",
        error,
        _countof(error)));
    assert(dio_vault_save(&vault, error, _countof(error)));
    assert(CopyFileW(path, copy_path, FALSE));
    assert(CopyFileW(path, tamper_path, FALSE));
    dio_vault_init(&copied, copy_path);
    assert(dio_vault_unlock(
        &copied,
        L"first-password",
        error,
        _countof(error)));
    assert(wcscmp(
        dio_vault_get(&copied, L"provider.api_key"),
        L"sk-test-secret") == 0);
    dio_vault_lock(&copied);
    tamper_last_byte(tamper_path);
    dio_vault_init(&tampered, tamper_path);
    assert(!dio_vault_unlock(
        &tampered,
        L"first-password",
        error,
        _countof(error)));
    dio_vault_lock(&tampered);
    dio_vault_lock(&vault);

    assert(!dio_vault_unlock(
        &vault,
        L"wrong-password",
        error,
        _countof(error)));
    assert(dio_vault_unlock(
        &vault,
        L"first-password",
        error,
        _countof(error)));
    assert(wcscmp(
        dio_vault_get(&vault, L"provider.api_key"),
        L"sk-test-secret") == 0);
    assert(dio_vault_change_password(
        &vault,
        L"second-password",
        error,
        _countof(error)));
    dio_vault_lock(&vault);
    assert(!dio_vault_unlock(
        &vault,
        L"first-password",
        error,
        _countof(error)));
    assert(dio_vault_unlock(
        &vault,
        L"second-password",
        error,
        _countof(error)));
    assert(wcscmp(
        dio_vault_get(&vault, L"provider.api_key"),
        L"sk-test-secret") == 0);
    ZeroMemory(&profile, sizeof(profile));
    ZeroMemory(detached_id, sizeof(detached_id));
    dio_agent_profile_init(&profile);
    assert(profile.system_prompt != NULL);
    assert(wcscpy_s(
        profile.api_key_secret_id,
        _countof(profile.api_key_secret_id),
        L"provider.api_key") == 0);
    assert(dio_agent_profile_detach_api_key_secret(
        &profile,
        detached_id,
        _countof(detached_id)));
    assert(profile.api_key_secret_id[0] == L'\0');
    dio_vault_lock(&vault);
    assert(dio_vault_unlock(
        &vault,
        L"second-password",
        error,
        _countof(error)));
    assert(profile.api_key_secret_id[0] == L'\0');
    assert(wcscmp(
        dio_vault_get(&vault, detached_id),
        L"sk-test-secret") == 0);
    assert(dio_vault_remove(
        &vault,
        detached_id,
        error,
        _countof(error)));
    assert(dio_vault_save(&vault, error, _countof(error)));
    assert(dio_vault_get(&vault, detached_id) == NULL);
    dio_agent_profile_free(&profile);
    assert(dio_vault_reset(&vault, error, _countof(error)));
    assert(!dio_vault_exists(&vault));
    assert(DeleteFileW(copy_path));
    assert(DeleteFileW(tamper_path));
    assert(RemoveDirectoryW(directory));

    (void)printf(
        "portable AES-256-GCM vault, copied unlock, tamper rejection and "
        "password rotation: PASS\n");
    return 0;
}
