#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <fdi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>

#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <yyjson.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "cabinet.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")

#ifndef DIO_BOOTSTRAP_MANIFEST_URL
#define DIO_BOOTSTRAP_MANIFEST_URL L""
#endif

#ifndef DIO_BOOTSTRAP_MANIFEST_SHA256
#define DIO_BOOTSTRAP_MANIFEST_SHA256 L""
#endif

#define DIO_BOOTSTRAP_CLASS L"DioVoiceBootstrapWindow"
#define DIO_PATH_CAP MAX_PATH
#define DIO_URL_CAP 2048u
#define DIO_MAX_COMPONENTS 32u
#define DIO_MAX_MANIFEST_BYTES (2u * 1024u * 1024u)
#define DIO_DOWNLOAD_BUFFER (64u * 1024u)
#define DIO_DISK_RESERVE (256ull * 1024ull * 1024ull)
#define DIO_WM_STATUS (WM_APP + 1u)
#define DIO_WM_DONE (WM_APP + 2u)
#define DIO_WM_ERROR (WM_APP + 3u)
#define DIO_WM_PLAN (WM_APP + 4u)

typedef enum DioProfile {
    DIO_PROFILE_ALL = 0,
    DIO_PROFILE_SMALL,
    DIO_PROFILE_LARGE
} DioProfile;

typedef enum DioArchive {
    DIO_ARCHIVE_FILE = 0,
    DIO_ARCHIVE_CAB
} DioArchive;

typedef struct DioComponent {
    wchar_t id[64];
    wchar_t url[DIO_URL_CAP];
    wchar_t target[DIO_PATH_CAP];
    char sha256[65];
    uint64_t download_bytes;
    uint64_t installed_bytes;
    DioProfile profile;
    DioArchive archive;
} DioComponent;

typedef struct DioManifest {
    wchar_t version[64];
    wchar_t entrypoint[DIO_PATH_CAP];
    DioComponent components[DIO_MAX_COMPONENTS];
    size_t component_count;
} DioManifest;

typedef struct DioUiStatus {
    uint64_t completed;
    uint64_t total;
    wchar_t text[384];
} DioUiStatus;

typedef struct DioInstallPlan {
    uint64_t download_bytes;
    uint64_t installed_bytes;
    uint64_t replaced_bytes;
    uint64_t required_bytes;
    uint64_t free_bytes;
} DioInstallPlan;

typedef struct DioBootstrap {
    HINSTANCE instance;
    HWND window;
    HWND title;
    HWND detail;
    HWND requirements;
    HWND log_path;
    HWND profile_label;
    HWND profile;
    HWND repair;
    HWND progress;
    HWND primary;
    HWND cancel;
    HFONT font;
    HFONT title_font;
    HANDLE cancel_event;
    HANDLE worker;
    wchar_t root[DIO_PATH_CAP];
    wchar_t manifest_url[DIO_URL_CAP];
    wchar_t manifest_file[DIO_PATH_CAP];
    wchar_t manifest_sha256[65];
    DioProfile selected_profile;
    bool force_repair;
    bool install_only;
    bool allow_test_http;
    bool busy;
} DioBootstrap;

typedef struct DioCabContext {
    wchar_t source[DIO_PATH_CAP];
    wchar_t destination[DIO_PATH_CAP];
    HANDLE cancel_event;
    DWORD error;
    bool failed;
} DioCabContext;

static __declspec(thread) DioCabContext *dio_fdi_context = NULL;

static bool dio_copy_wide(
    wchar_t *destination,
    size_t capacity,
    const wchar_t *source) {
    if (destination == NULL || capacity == 0u || source == NULL) {
        return false;
    }
    const size_t length = wcslen(source);
    if (length >= capacity) {
        return false;
    }
    memcpy(destination, source, (length + 1u) * sizeof(*destination));
    return true;
}

static bool dio_utf8_to_wide(
    const char *source,
    wchar_t *destination,
    size_t capacity) {
    if (source == NULL || destination == NULL || capacity == 0u) {
        return false;
    }
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source,
        -1,
        destination,
        (int)capacity);
    return converted > 0;
}

static bool dio_wide_to_utf8(
    const wchar_t *source,
    char *destination,
    size_t capacity) {
    if (source == NULL || destination == NULL || capacity == 0u) {
        return false;
    }
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        source,
        -1,
        destination,
        (int)capacity,
        NULL,
        NULL);
    return converted > 0;
}

static bool dio_is_hex_sha256(const char *value) {
    if (value == NULL || strlen(value) != 64u) {
        return false;
    }
    for (size_t index = 0u; index < 64u; ++index) {
        const char ch = value[index];
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool dio_safe_component_id(const wchar_t *value) {
    if (value == NULL || value[0] == L'\0') {
        return false;
    }
    for (const wchar_t *cursor = value; *cursor != L'\0'; ++cursor) {
        const wchar_t ch = *cursor;
        if (!((ch >= L'a' && ch <= L'z') ||
              (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_')) {
            return false;
        }
    }
    return true;
}

static bool dio_safe_relative_path(const wchar_t *path);

static bool dio_safe_version(const wchar_t *value) {
    if (value == NULL || value[0] == L'\0') {
        return false;
    }
    for (const wchar_t *cursor = value; *cursor != L'\0'; ++cursor) {
        const wchar_t ch = *cursor;
        if (!((ch >= L'a' && ch <= L'z') ||
              (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' ||
              ch == L'.')) {
            return false;
        }
    }
    return dio_safe_relative_path(value);
}

static bool dio_reserved_device_segment(
    const wchar_t *segment,
    size_t length) {
    size_t base_length = 0u;
    while (base_length < length && segment[base_length] != L'.') {
        ++base_length;
    }
    while (base_length > 0u &&
           (segment[base_length - 1u] == L' ' ||
            segment[base_length - 1u] == L'.')) {
        --base_length;
    }
    if (base_length == 3u &&
        (_wcsnicmp(segment, L"CON", 3u) == 0 ||
         _wcsnicmp(segment, L"PRN", 3u) == 0 ||
         _wcsnicmp(segment, L"AUX", 3u) == 0 ||
         _wcsnicmp(segment, L"NUL", 3u) == 0)) {
        return true;
    }
    return base_length == 4u &&
        (_wcsnicmp(segment, L"COM", 3u) == 0 ||
         _wcsnicmp(segment, L"LPT", 3u) == 0) &&
        segment[3] >= L'1' && segment[3] <= L'9';
}

static bool dio_safe_relative_path(const wchar_t *path) {
    if (path == NULL || path[0] == L'\0' || path[0] == L'\\' ||
        path[0] == L'/' || (wcslen(path) >= 2u && path[1] == L':')) {
        return false;
    }
    const wchar_t *segment = path;
    for (const wchar_t *cursor = path;; ++cursor) {
        if (*cursor == L':' || *cursor == L'<' || *cursor == L'>' ||
            *cursor == L'"' || *cursor == L'|' || *cursor == L'?' ||
            *cursor == L'*' || (*cursor != L'\0' && *cursor < L' ')) {
            return false;
        }
        if (*cursor == L'\\' || *cursor == L'/' || *cursor == L'\0') {
            const size_t length = (size_t)(cursor - segment);
            if (length == 0u ||
                (length == 1u && segment[0] == L'.') ||
                (length == 2u && segment[0] == L'.' && segment[1] == L'.') ||
                segment[length - 1u] == L'.' ||
                segment[length - 1u] == L' ' ||
                dio_reserved_device_segment(segment, length)) {
                return false;
            }
            if (*cursor == L'\0') {
                break;
            }
            segment = cursor + 1;
        }
    }
    return true;
}

static bool dio_path_is_parent(const wchar_t *parent, const wchar_t *child) {
    const size_t parent_length = wcslen(parent);
    const size_t child_length = wcslen(child);
    return child_length > parent_length &&
        _wcsnicmp(parent, child, parent_length) == 0 &&
        (child[parent_length] == L'\\' || child[parent_length] == L'/');
}

static bool dio_join_path(
    wchar_t *destination,
    size_t capacity,
    const wchar_t *left,
    const wchar_t *right) {
    if (destination == NULL || capacity == 0u || left == NULL || right == NULL) {
        return false;
    }
    const size_t left_length = wcslen(left);
    const bool separator = left_length != 0u &&
        left[left_length - 1u] != L'\\' && left[left_length - 1u] != L'/';
    const int written = swprintf_s(
        destination,
        capacity,
        separator ? L"%ls\\%ls" : L"%ls%ls",
        left,
        right);
    if (written < 0 || (size_t)written >= capacity) {
        if (capacity != 0u) {
            destination[0] = L'\0';
        }
        return false;
    }
    for (wchar_t *cursor = destination; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'/') {
            *cursor = L'\\';
        }
    }
    return true;
}

static bool dio_parent_path(
    wchar_t *destination,
    size_t capacity,
    const wchar_t *path) {
    if (!dio_copy_wide(destination, capacity, path)) {
        return false;
    }
    wchar_t *separator = wcsrchr(destination, L'\\');
    if (separator == NULL) {
        separator = wcsrchr(destination, L'/');
    }
    if (separator == NULL) {
        return false;
    }
    *separator = L'\0';
    return true;
}

static bool dio_ensure_directory(const wchar_t *path) {
    wchar_t current[DIO_PATH_CAP];
    if (!dio_copy_wide(current, _countof(current), path)) {
        return false;
    }
    for (wchar_t *cursor = current + 3; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            const wchar_t saved = *cursor;
            *cursor = L'\0';
            if (!CreateDirectoryW(current, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                return false;
            }
            *cursor = saved;
        }
    }
    return CreateDirectoryW(current, NULL) ||
        GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool dio_ensure_parent(const wchar_t *path) {
    wchar_t parent[DIO_PATH_CAP];
    return dio_parent_path(parent, _countof(parent), path) &&
        dio_ensure_directory(parent);
}

static bool dio_path_exists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool dio_file_exists(const wchar_t *path) {
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

static bool dio_delete_tree(const wchar_t *path) {
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u
            ? RemoveDirectoryW(path) != FALSE
            : DeleteFileW(path) != FALSE;
    }

    wchar_t pattern[DIO_PATH_CAP];
    if (!dio_join_path(pattern, _countof(pattern), path, L"*")) {
        return false;
    }
    WIN32_FIND_DATAW item;
    HANDLE find = FindFirstFileW(pattern, &item);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(item.cFileName, L".") == 0 ||
                wcscmp(item.cFileName, L"..") == 0) {
                continue;
            }
            wchar_t child[DIO_PATH_CAP];
            if (!dio_join_path(child, _countof(child), path, item.cFileName) ||
                !dio_delete_tree(child)) {
                FindClose(find);
                return false;
            }
        } while (FindNextFileW(find, &item));
        const DWORD error = GetLastError();
        FindClose(find);
        if (error != ERROR_NO_MORE_FILES) {
            return false;
        }
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return false;
    }
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path) != FALSE;
}

static bool dio_read_file(
    const wchar_t *path,
    size_t maximum,
    char **output,
    size_t *output_size) {
    *output = NULL;
    *output_size = 0u;
    HANDLE file = CreateFileW(
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
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > maximum) {
        CloseHandle(file);
        return false;
    }
    char *buffer = (char *)malloc((size_t)size.QuadPart + 1u);
    if (buffer == NULL) {
        CloseHandle(file);
        return false;
    }
    size_t used = 0u;
    while (used < (size_t)size.QuadPart) {
        const DWORD request = (DWORD)(((size_t)size.QuadPart - used) > MAXDWORD
            ? MAXDWORD
            : ((size_t)size.QuadPart - used));
        DWORD read = 0u;
        if (!ReadFile(file, buffer + used, request, &read, NULL) || read == 0u) {
            free(buffer);
            CloseHandle(file);
            return false;
        }
        used += read;
    }
    buffer[used] = '\0';
    CloseHandle(file);
    *output = buffer;
    *output_size = used;
    return true;
}

static bool dio_write_file_atomic(
    const wchar_t *path,
    const void *bytes,
    size_t size) {
    wchar_t temporary[DIO_PATH_CAP];
    if (swprintf_s(temporary, _countof(temporary), L"%ls.tmp", path) < 0 ||
        !dio_ensure_parent(path)) {
        return false;
    }
    HANDLE file = CreateFileW(
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
    size_t written_total = 0u;
    while (written_total < size) {
        const DWORD request = (DWORD)((size - written_total) > MAXDWORD
            ? MAXDWORD
            : (size - written_total));
        DWORD written = 0u;
        if (!WriteFile(
                file,
                (const unsigned char *)bytes + written_total,
                request,
                &written,
                NULL) ||
            written == 0u) {
            CloseHandle(file);
            DeleteFileW(temporary);
            return false;
        }
        written_total += written;
    }
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!flushed || !MoveFileExW(
            temporary,
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        return false;
    }
    return true;
}

static void dio_log(DioBootstrap *bootstrap, const wchar_t *message) {
    if (bootstrap == NULL || message == NULL || message[0] == L'\0') {
        return;
    }
    wchar_t path[DIO_PATH_CAP];
    if (!dio_join_path(
            path,
            _countof(path),
            bootstrap->root,
            L".dio\\logs\\bootstrap.log") ||
        !dio_ensure_parent(path)) {
        return;
    }
    char utf8[2048];
    if (!dio_wide_to_utf8(message, utf8, sizeof(utf8))) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[2304];
    const int length = sprintf_s(
        line,
        sizeof(line),
        "%04u-%02u-%02uT%02u:%02u:%02u %s\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        utf8);
    if (length <= 0) {
        return;
    }
    HANDLE file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0u;
    (void)WriteFile(file, line, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

static bool dio_sha256_file(const wchar_t *path, char output[65]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD object_size = 0u;
    DWORD result_size = 0u;
    unsigned char *object = NULL;
    unsigned char digest[32];
    HANDLE file = INVALID_HANDLE_VALUE;
    bool success = false;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            NULL,
            0u)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            (PUCHAR)&object_size,
            sizeof(object_size),
            &result_size,
            0u))) {
        goto cleanup;
    }
    object = (unsigned char *)malloc(object_size);
    if (object == NULL || !BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm,
            &hash,
            object,
            object_size,
            NULL,
            0u,
            0u))) {
        goto cleanup;
    }
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }
    unsigned char buffer[DIO_DOWNLOAD_BUFFER];
    for (;;) {
        DWORD read = 0u;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL)) {
            goto cleanup;
        }
        if (read == 0u) {
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer, read, 0u))) {
            goto cleanup;
        }
    }
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0u))) {
        goto cleanup;
    }
    for (size_t index = 0u; index < sizeof(digest); ++index) {
        (void)sprintf_s(output + (index * 2u), 3u, "%02x", digest[index]);
    }
    output[64] = '\0';
    success = true;

cleanup:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (hash != NULL) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != NULL) {
        BCryptCloseAlgorithmProvider(algorithm, 0u);
    }
    if (object != NULL) {
        SecureZeroMemory(object, object_size);
        free(object);
    }
    SecureZeroMemory(digest, sizeof(digest));
    return success;
}

static bool dio_hash_matches(const wchar_t *path, const char *expected) {
    char actual[65];
    return dio_sha256_file(path, actual) && _stricmp(actual, expected) == 0;
}

static bool dio_post_status(
    DioBootstrap *bootstrap,
    uint64_t completed,
    uint64_t total,
    const wchar_t *text) {
    DioUiStatus *status = (DioUiStatus *)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(*status));
    if (status == NULL) {
        return false;
    }
    status->completed = completed;
    status->total = total;
    if (!dio_copy_wide(status->text, _countof(status->text), text) ||
        !PostMessageW(bootstrap->window, DIO_WM_STATUS, 0u, (LPARAM)status)) {
        HeapFree(GetProcessHeap(), 0u, status);
        return false;
    }
    return true;
}

static bool dio_post_plan(DioBootstrap *bootstrap, const wchar_t *text) {
    if (bootstrap->window == NULL) {
        return true;
    }
    const size_t bytes = (wcslen(text) + 1u) * sizeof(*text);
    wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0u, bytes);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, text, bytes);
    if (!PostMessageW(bootstrap->window, DIO_WM_PLAN, 0u, (LPARAM)copy)) {
        HeapFree(GetProcessHeap(), 0u, copy);
        return false;
    }
    return true;
}

static bool dio_cancelled(const DioBootstrap *bootstrap) {
    return WaitForSingleObject(bootstrap->cancel_event, 0u) == WAIT_OBJECT_0;
}

static bool dio_cancel_requested(
    DioBootstrap *bootstrap,
    wchar_t *error,
    size_t error_capacity) {
    if (!dio_cancelled(bootstrap)) {
        return false;
    }
    (void)swprintf_s(error, error_capacity, L"عملیات با درخواست کاربر لغو شد.");
    dio_log(bootstrap, L"CANCEL requested");
    return true;
}

static DioProfile dio_recommended_profile(const wchar_t *root) {
    MEMORYSTATUSEX memory;
    ZeroMemory(&memory, sizeof(memory));
    memory.dwLength = sizeof(memory);
    ULARGE_INTEGER free_bytes;
    ZeroMemory(&free_bytes, sizeof(free_bytes));
    if (GlobalMemoryStatusEx(&memory) &&
        GetDiskFreeSpaceExW(root, &free_bytes, NULL, NULL) &&
        memory.ullTotalPhys >= (16ull * 1024ull * 1024ull * 1024ull) &&
        free_bytes.QuadPart >= (8ull * 1024ull * 1024ull * 1024ull)) {
        return DIO_PROFILE_LARGE;
    }
    return DIO_PROFILE_SMALL;
}

static bool dio_component_selected(
    const DioComponent *component,
    DioProfile profile) {
    return component->profile == DIO_PROFILE_ALL ||
        component->profile == profile;
}

static bool dio_json_string(
    yyjson_val *object,
    const char *name,
    const char **output) {
    yyjson_val *value = yyjson_obj_get(object, name);
    if (value == NULL || !yyjson_is_str(value)) {
        return false;
    }
    *output = yyjson_get_str(value);
    return *output != NULL;
}

static bool dio_parse_profile(const char *text, DioProfile *profile) {
    if (strcmp(text, "all") == 0) {
        *profile = DIO_PROFILE_ALL;
        return true;
    }
    if (strcmp(text, "small") == 0) {
        *profile = DIO_PROFILE_SMALL;
        return true;
    }
    if (strcmp(text, "large") == 0) {
        *profile = DIO_PROFILE_LARGE;
        return true;
    }
    return false;
}

static bool dio_url_allowed(const wchar_t *url, bool allow_test_http) {
    wchar_t host[256];
    URL_COMPONENTSW parts;
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = _countof(host);
    if (url == NULL || !WinHttpCrackUrl(url, 0u, 0u, &parts) ||
        parts.dwHostNameLength == 0u) {
        return false;
    }
    if (parts.nScheme == INTERNET_SCHEME_HTTPS) {
        return true;
    }
    return allow_test_http && parts.nScheme == INTERNET_SCHEME_HTTP &&
        (_wcsicmp(host, L"127.0.0.1") == 0 ||
         _wcsicmp(host, L"localhost") == 0 ||
         _wcsicmp(host, L"::1") == 0 ||
         _wcsicmp(host, L"[::1]") == 0);
}

static bool dio_parse_manifest(
    const char *json,
    size_t size,
    bool allow_test_http,
    DioManifest *manifest,
    wchar_t *error,
    size_t error_capacity) {
    ZeroMemory(manifest, sizeof(*manifest));
    yyjson_read_err read_error;
    yyjson_doc *document = yyjson_read_opts(
        (char *)json,
        size,
        YYJSON_READ_NOFLAG,
        NULL,
        &read_error);
    if (document == NULL) {
        (void)swprintf_s(error, error_capacity, L"مانیفست JSON معتبر نیست.");
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *schema = yyjson_obj_get(root, "schema");
    yyjson_val *components = yyjson_obj_get(root, "components");
    const char *version = NULL;
    const char *entrypoint = NULL;
    bool valid = yyjson_is_obj(root) && yyjson_is_int(schema) &&
        yyjson_get_int(schema) == 1 &&
        dio_json_string(root, "version", &version) &&
        dio_json_string(root, "entrypoint", &entrypoint) &&
        yyjson_is_arr(components) &&
        yyjson_arr_size(components) > 0u &&
        yyjson_arr_size(components) <= DIO_MAX_COMPONENTS &&
        dio_utf8_to_wide(version, manifest->version, _countof(manifest->version)) &&
        dio_safe_version(manifest->version) &&
        dio_utf8_to_wide(
            entrypoint,
            manifest->entrypoint,
            _countof(manifest->entrypoint)) &&
        dio_safe_relative_path(manifest->entrypoint);

    size_t index = 0u;
    yyjson_val *component_value = NULL;
    yyjson_arr_iter iterator;
    ZeroMemory(&iterator, sizeof(iterator));
    if (valid) {
        iterator = yyjson_arr_iter_with(components);
    }
    while (valid && (component_value = yyjson_arr_iter_next(&iterator)) != NULL) {
        DioComponent *component = &manifest->components[index];
        const char *id = NULL;
        const char *url = NULL;
        const char *target = NULL;
        const char *sha256 = NULL;
        const char *profile = NULL;
        const char *archive = NULL;
        yyjson_val *download_bytes = yyjson_obj_get(component_value, "bytes");
        yyjson_val *installed_bytes = yyjson_obj_get(
            component_value,
            "installed_bytes");
        valid = yyjson_is_obj(component_value) &&
            dio_json_string(component_value, "id", &id) &&
            dio_json_string(component_value, "url", &url) &&
            dio_json_string(component_value, "target", &target) &&
            dio_json_string(component_value, "sha256", &sha256) &&
            dio_json_string(component_value, "profile", &profile) &&
            dio_json_string(component_value, "archive", &archive) &&
            yyjson_is_uint(download_bytes) && yyjson_is_uint(installed_bytes) &&
            yyjson_get_uint(download_bytes) > 0u &&
            yyjson_get_uint(installed_bytes) > 0u &&
            dio_is_hex_sha256(sha256) &&
            dio_utf8_to_wide(id, component->id, _countof(component->id)) &&
            dio_safe_component_id(component->id) &&
            dio_utf8_to_wide(url, component->url, _countof(component->url)) &&
            dio_utf8_to_wide(
                target,
                component->target,
                _countof(component->target)) &&
            dio_safe_relative_path(component->target) &&
            dio_url_allowed(component->url, allow_test_http) &&
            dio_parse_profile(profile, &component->profile) &&
            (strcmp(archive, "cab") == 0 || strcmp(archive, "file") == 0);
        if (valid) {
            component->archive = strcmp(archive, "cab") == 0
                ? DIO_ARCHIVE_CAB
                : DIO_ARCHIVE_FILE;
            component->download_bytes = yyjson_get_uint(download_bytes);
            component->installed_bytes = yyjson_get_uint(installed_bytes);
            (void)strcpy_s(component->sha256, sizeof(component->sha256), sha256);
            ++index;
        }
    }
    for (size_t left = 0u; valid && left < index; ++left) {
        for (size_t right = left + 1u; right < index; ++right) {
            if (_wcsicmp(
                    manifest->components[left].id,
                    manifest->components[right].id) == 0) {
                valid = false;
                break;
            }
            const wchar_t *left_target = manifest->components[left].target;
            const wchar_t *right_target = manifest->components[right].target;
            if (_wcsicmp(left_target, right_target) != 0 &&
                (dio_path_is_parent(left_target, right_target) ||
                 dio_path_is_parent(right_target, left_target))) {
                valid = false;
                break;
            }
        }
    }
    manifest->component_count = index;
    yyjson_doc_free(document);
    if (!valid || index == 0u) {
        (void)swprintf_s(
            error,
            error_capacity,
            L"ساختار یا مسیرهای مانیفست معتبر نیست.");
        return false;
    }
    return true;
}

static bool dio_query_header(
    HINTERNET request,
    DWORD query,
    wchar_t *output,
    size_t capacity) {
    DWORD bytes = (DWORD)(capacity * sizeof(*output));
    if (!WinHttpQueryHeaders(
            request,
            query,
            WINHTTP_HEADER_NAME_BY_INDEX,
            output,
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        output[0] = L'\0';
        return false;
    }
    output[capacity - 1u] = L'\0';
    return true;
}

static bool dio_get_file_size(const wchar_t *path, uint64_t *size) {
    *size = 0u;
    HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER value;
    const bool success = GetFileSizeEx(file, &value) && value.QuadPart >= 0;
    CloseHandle(file);
    if (success) {
        *size = (uint64_t)value.QuadPart;
    }
    return success;
}

static bool dio_add_size(uint64_t *total, uint64_t value) {
    if (UINT64_MAX - *total < value) {
        return false;
    }
    *total += value;
    return true;
}

static bool dio_tree_size(const wchar_t *path, uint64_t *size) {
    *size = 0u;
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) {
        return dio_get_file_size(path, size);
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        return true;
    }
    wchar_t pattern[DIO_PATH_CAP];
    if (!dio_join_path(pattern, _countof(pattern), path, L"*")) {
        return false;
    }
    WIN32_FIND_DATAW item;
    HANDLE find = FindFirstFileW(pattern, &item);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool success = true;
    do {
        if (wcscmp(item.cFileName, L".") == 0 ||
            wcscmp(item.cFileName, L"..") == 0) {
            continue;
        }
        wchar_t child[DIO_PATH_CAP];
        uint64_t child_size = 0u;
        if (!dio_join_path(child, _countof(child), path, item.cFileName) ||
            !dio_tree_size(child, &child_size) ||
            !dio_add_size(size, child_size)) {
            success = false;
            break;
        }
    } while (FindNextFileW(find, &item));
    const DWORD last_error = GetLastError();
    FindClose(find);
    return success && last_error == ERROR_NO_MORE_FILES;
}

static bool dio_download(
    DioBootstrap *bootstrap,
    const wchar_t *url,
    const wchar_t *part_path,
    uint64_t expected_size,
    uint64_t maximum_size,
    const wchar_t *label,
    wchar_t *error,
    size_t error_capacity) {
    bool success = false;
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    HANDLE output = INVALID_HANDLE_VALUE;
    wchar_t host[256];
    wchar_t path[DIO_URL_CAP];
    wchar_t extra[DIO_URL_CAP];
    URL_COMPONENTSW parts;
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = _countof(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = _countof(path);
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = _countof(extra);
    if (!WinHttpCrackUrl(url, 0u, 0u, &parts) ||
        !dio_url_allowed(url, bootstrap->allow_test_http)) {
        (void)swprintf_s(error, error_capacity, L"آدرس دانلود HTTPS معتبر نیست.");
        goto cleanup;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    wchar_t request_path[DIO_URL_CAP];
    if (swprintf_s(
            request_path,
            _countof(request_path),
            L"%.*ls%.*ls",
            (int)parts.dwUrlPathLength,
            parts.lpszUrlPath,
            (int)parts.dwExtraInfoLength,
            parts.lpszExtraInfo) < 0) {
        (void)swprintf_s(error, error_capacity, L"مسیر دانلود بیش از حد بلند است.");
        goto cleanup;
    }

    session = WinHttpOpen(
        L"DIO Voice Bootstrap/0.1",
        secure
            ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
            : WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0u);
    if (session == NULL ||
        !WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000)) {
        (void)swprintf_s(error, error_capacity, L"WinHTTP آماده نشد.");
        goto cleanup;
    }
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    (void)WinHttpSetOption(
        session,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect_policy,
        sizeof(redirect_policy));
    connection = WinHttpConnect(
        session,
        host,
        parts.nPort,
        0u);
    if (connection == NULL) {
        (void)swprintf_s(error, error_capacity, L"اتصال به میزبان دانلود برقرار نشد.");
        goto cleanup;
    }
    request = WinHttpOpenRequest(
        connection,
        L"GET",
        request_path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (secure ? WINHTTP_FLAG_SECURE : 0u) | WINHTTP_FLAG_REFRESH);
    if (request == NULL) {
        (void)swprintf_s(error, error_capacity, L"درخواست دانلود ساخته نشد.");
        goto cleanup;
    }

    uint64_t offset = 0u;
    (void)dio_get_file_size(part_path, &offset);
    wchar_t etag_path[DIO_PATH_CAP];
    char *etag_utf8 = NULL;
    size_t etag_size = 0u;
    wchar_t headers[1024];
    headers[0] = L'\0';
    if (swprintf_s(etag_path, _countof(etag_path), L"%ls.etag", part_path) < 0) {
        offset = 0u;
    }
    if (offset > 0u) {
        wchar_t etag[512];
        etag[0] = L'\0';
        if (dio_read_file(etag_path, 1024u, &etag_utf8, &etag_size)) {
            (void)dio_utf8_to_wide(etag_utf8, etag, _countof(etag));
        }
        if (etag[0] != L'\0') {
            (void)swprintf_s(
                headers,
                _countof(headers),
                L"Range: bytes=%llu-\r\nIf-Range: %ls\r\n",
                (unsigned long long)offset,
                etag);
        } else {
            (void)swprintf_s(
                headers,
                _countof(headers),
                L"Range: bytes=%llu-\r\n",
                (unsigned long long)offset);
        }
    }
    free(etag_utf8);
    if (!WinHttpSendRequest(
            request,
            headers[0] != L'\0' ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
            headers[0] != L'\0' ? (DWORD)-1L : 0u,
            WINHTTP_NO_REQUEST_DATA,
            0u,
            0u,
            0u) ||
        !WinHttpReceiveResponse(request, NULL)) {
        (void)swprintf_s(error, error_capacity, L"دانلود شروع نشد؛ اتصال را بررسی کنید.");
        goto cleanup;
    }
    DWORD status = 0u;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        (status != 200u && status != 206u)) {
        (void)swprintf_s(
            error,
            error_capacity,
            L"سرور دانلود پاسخ HTTP %lu داد.",
            status);
        goto cleanup;
    }
    if (status == 200u) {
        offset = 0u;
    }
    output = CreateFileW(
        part_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        offset > 0u ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (output == INVALID_HANDLE_VALUE) {
        (void)swprintf_s(error, error_capacity, L"فایل موقت دانلود ساخته نشد.");
        goto cleanup;
    }
    if (offset > 0u) {
        LARGE_INTEGER position;
        position.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(output, position, NULL, FILE_BEGIN)) {
            (void)swprintf_s(error, error_capacity, L"Resume فایل موقت ممکن نشد.");
            goto cleanup;
        }
    }

    wchar_t content_length_text[64];
    uint64_t remaining = 0u;
    if (dio_query_header(
            request,
            WINHTTP_QUERY_CONTENT_LENGTH,
            content_length_text,
            _countof(content_length_text))) {
        remaining = _wcstoui64(content_length_text, NULL, 10);
    }
    uint64_t total = remaining > 0u ? offset + remaining : expected_size;
    if (total == 0u) {
        total = expected_size;
    }
    wchar_t etag[512];
    if (dio_query_header(request, WINHTTP_QUERY_ETAG, etag, _countof(etag))) {
        char etag_bytes[1024];
        if (dio_wide_to_utf8(etag, etag_bytes, sizeof(etag_bytes))) {
            (void)dio_write_file_atomic(
                etag_path,
                etag_bytes,
                strlen(etag_bytes));
        }
    }

    unsigned char buffer[DIO_DOWNLOAD_BUFFER];
    uint64_t completed = offset;
    ULONGLONG last_tick = GetTickCount64();
    uint64_t last_completed = completed;
    for (;;) {
        if (dio_cancelled(bootstrap)) {
            SetLastError(ERROR_CANCELLED);
            (void)swprintf_s(error, error_capacity, L"دانلود لغو شد.");
            goto cleanup;
        }
        DWORD read = 0u;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
            (void)swprintf_s(error, error_capacity, L"خواندن پاسخ دانلود شکست خورد.");
            goto cleanup;
        }
        if (read == 0u) {
            break;
        }
        if (maximum_size != 0u && completed + read > maximum_size) {
            (void)swprintf_s(error, error_capacity, L"حجم دانلود از سقف مانیفست بیشتر شد.");
            goto cleanup;
        }
        DWORD written = 0u;
        if (!WriteFile(output, buffer, read, &written, NULL) || written != read) {
            (void)swprintf_s(error, error_capacity, L"نوشتن فایل دانلود شکست خورد.");
            goto cleanup;
        }
        completed += read;
        const ULONGLONG now = GetTickCount64();
        if (now - last_tick >= 400u) {
            const uint64_t per_second = (completed - last_completed) * 1000u /
                (uint64_t)(now - last_tick);
            wchar_t status_text[384];
            (void)swprintf_s(
                status_text,
                _countof(status_text),
                L"%ls — %.1f MB از %.1f MB — %.1f MB/s",
                label,
                (double)completed / (1024.0 * 1024.0),
                (double)total / (1024.0 * 1024.0),
                (double)per_second / (1024.0 * 1024.0));
            (void)dio_post_status(bootstrap, completed, total, status_text);
            last_tick = now;
            last_completed = completed;
        }
    }
    if (!FlushFileBuffers(output)) {
        (void)swprintf_s(error, error_capacity, L"ثبت فایل دانلود کامل نشد.");
        goto cleanup;
    }
    success = true;

cleanup:
    if (output != INVALID_HANDLE_VALUE) {
        CloseHandle(output);
    }
    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    if (session != NULL) {
        WinHttpCloseHandle(session);
    }
    return success;
}

static FNALLOC(dio_fdi_alloc) {
    return HeapAlloc(GetProcessHeap(), 0u, (SIZE_T)cb);
}

static FNFREE(dio_fdi_free) {
    if (pv != NULL) {
        HeapFree(GetProcessHeap(), 0u, pv);
    }
}

static FNOPEN(dio_fdi_open) {
    (void)pszFile;
    (void)pmode;
    DioCabContext *cab = dio_fdi_context;
    if (cab == NULL || (oflag & (_O_WRONLY | _O_RDWR | _O_CREAT)) != 0) {
        if (cab != NULL) {
            cab->error = ERROR_ACCESS_DENIED;
            cab->failed = true;
        }
        return -1;
    }
    HANDLE file = CreateFileW(
        cab->source,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        cab->error = GetLastError();
        cab->failed = true;
        return -1;
    }
    return (INT_PTR)file;
}

static FNREAD(dio_fdi_read) {
    DioCabContext *cab = dio_fdi_context;
    if (cab != NULL && cab->cancel_event != NULL &&
        WaitForSingleObject(cab->cancel_event, 0u) == WAIT_OBJECT_0) {
        cab->error = ERROR_CANCELLED;
        cab->failed = true;
        return (UINT)-1;
    }
    DWORD read = 0u;
    if (!ReadFile((HANDLE)hf, pv, cb, &read, NULL)) {
        if (cab != NULL) {
            cab->error = GetLastError();
            cab->failed = true;
        }
        return (UINT)-1;
    }
    return read;
}

static FNWRITE(dio_fdi_write) {
    DioCabContext *cab = dio_fdi_context;
    if (cab != NULL && cab->cancel_event != NULL &&
        WaitForSingleObject(cab->cancel_event, 0u) == WAIT_OBJECT_0) {
        cab->error = ERROR_CANCELLED;
        cab->failed = true;
        return (UINT)-1;
    }
    DWORD written = 0u;
    if (!WriteFile((HANDLE)hf, pv, cb, &written, NULL) || written != cb) {
        if (cab != NULL) {
            cab->error = GetLastError();
            if (cab->error == ERROR_SUCCESS) {
                cab->error = ERROR_WRITE_FAULT;
            }
            cab->failed = true;
        }
        return (UINT)-1;
    }
    return written;
}

static FNCLOSE(dio_fdi_close) {
    if (CloseHandle((HANDLE)hf)) {
        return 0;
    }
    if (dio_fdi_context != NULL) {
        dio_fdi_context->error = GetLastError();
        dio_fdi_context->failed = true;
    }
    return -1;
}

static FNSEEK(dio_fdi_seek) {
    LARGE_INTEGER distance;
    LARGE_INTEGER position;
    distance.QuadPart = dist;
    DWORD method = FILE_BEGIN;
    if (seektype == SEEK_CUR) {
        method = FILE_CURRENT;
    } else if (seektype == SEEK_END) {
        method = FILE_END;
    } else if (seektype != SEEK_SET) {
        if (dio_fdi_context != NULL) {
            dio_fdi_context->error = ERROR_INVALID_PARAMETER;
            dio_fdi_context->failed = true;
        }
        return -1;
    }
    if (!SetFilePointerEx((HANDLE)hf, distance, &position, method) ||
        position.QuadPart < LONG_MIN || position.QuadPart > LONG_MAX) {
        if (dio_fdi_context != NULL) {
            dio_fdi_context->error = GetLastError();
            if (dio_fdi_context->error == ERROR_SUCCESS) {
                dio_fdi_context->error = ERROR_ARITHMETIC_OVERFLOW;
            }
            dio_fdi_context->failed = true;
        }
        return -1;
    }
    return (long)position.QuadPart;
}

static bool dio_fdi_ascii_path(
    const char *source,
    wchar_t *destination,
    size_t capacity) {
    if (source == NULL || destination == NULL || capacity == 0u) {
        return false;
    }
    size_t index = 0u;
    for (; source[index] != '\0'; ++index) {
        const unsigned char value = (unsigned char)source[index];
        if (index + 1u >= capacity || value < 0x20u || value > 0x7eu) {
            return false;
        }
        destination[index] = value == '/' ? L'\\' : (wchar_t)value;
    }
    destination[index] = L'\0';
    return index != 0u;
}

static FNFDINOTIFY(dio_fdi_notify) {
    DioCabContext *cab = (DioCabContext *)pfdin->pv;
    if (cab == NULL) {
        return -1;
    }
    if (cab->cancel_event != NULL &&
        WaitForSingleObject(cab->cancel_event, 0u) == WAIT_OBJECT_0) {
        cab->error = ERROR_CANCELLED;
        cab->failed = true;
        return -1;
    }
    if (fdint == fdintCABINET_INFO) {
        if (pfdin->iCabinet != 0u) {
            cab->error = ERROR_INVALID_DATA;
            cab->failed = true;
            return -1;
        }
        return 0;
    }
    if (fdint == fdintCOPY_FILE) {
        wchar_t relative[DIO_PATH_CAP];
        wchar_t target[DIO_PATH_CAP];
        if (!dio_fdi_ascii_path(
                pfdin->psz1,
                relative,
                _countof(relative)) ||
            !dio_safe_relative_path(relative)) {
            cab->error = ERROR_INVALID_NAME;
            cab->failed = true;
            return -1;
        }
        if (!dio_join_path(
                target,
                _countof(target),
                cab->destination,
                relative)) {
            cab->error = ERROR_BUFFER_OVERFLOW;
            cab->failed = true;
            return -1;
        }
        if (!dio_ensure_parent(target)) {
            cab->error = GetLastError();
            cab->failed = true;
            return -1;
        }
        HANDLE output = CreateFileW(
            target,
            GENERIC_WRITE,
            0u,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            NULL);
        if (output == INVALID_HANDLE_VALUE) {
            cab->error = GetLastError();
            cab->failed = true;
            return -1;
        }
        return (INT_PTR)output;
    }
    if (fdint == fdintCLOSE_FILE_INFO) {
        if (!FlushFileBuffers((HANDLE)pfdin->hf) ||
            !CloseHandle((HANDLE)pfdin->hf)) {
            cab->error = GetLastError();
            cab->failed = true;
            return FALSE;
        }
        return TRUE;
    }
    if (fdint == fdintNEXT_CABINET || fdint == fdintPARTIAL_FILE) {
        cab->error = ERROR_INVALID_DATA;
        cab->failed = true;
        return -1;
    }
    return 0;
}

static bool dio_extract_cabinet(
    const wchar_t *cabinet,
    const wchar_t *destination,
    HANDLE cancel_event) {
    DioCabContext context;
    ERF error;
    ZeroMemory(&context, sizeof(context));
    ZeroMemory(&error, sizeof(error));
    context.cancel_event = cancel_event;
    if (!dio_copy_wide(context.source, _countof(context.source), cabinet) ||
        !dio_copy_wide(
            context.destination,
            _countof(context.destination),
            destination) ||
        !dio_ensure_directory(destination)) {
        return false;
    }
    dio_fdi_context = &context;
    HFDI fdi = FDICreate(
        dio_fdi_alloc,
        dio_fdi_free,
        dio_fdi_open,
        dio_fdi_read,
        dio_fdi_write,
        dio_fdi_close,
        dio_fdi_seek,
        cpu80386,
        &error);
    if (fdi == NULL) {
        dio_fdi_context = NULL;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    const BOOL copied = FDICopy(
        fdi,
        "payload.cab",
        "",
        0,
        dio_fdi_notify,
        NULL,
        &context);
    const BOOL destroyed = FDIDestroy(fdi);
    dio_fdi_context = NULL;
    if (!copied || !destroyed || context.failed) {
        SetLastError(context.error != ERROR_SUCCESS
            ? context.error
            : ERROR_INVALID_DATA);
        return false;
    }
    return true;
}

static bool dio_prepare_manifest(
    DioBootstrap *bootstrap,
    wchar_t *manifest_path,
    size_t path_capacity,
    wchar_t *error,
    size_t error_capacity) {
    wchar_t cache[DIO_PATH_CAP];
    if (!dio_join_path(cache, _countof(cache), bootstrap->root, L".dio\\cache") ||
        !dio_ensure_directory(cache) ||
        !dio_join_path(
            manifest_path,
            path_capacity,
            cache,
            L"release-manifest.json")) {
        (void)swprintf_s(error, error_capacity, L"پوشهٔ Cache ساخته نشد.");
        return false;
    }
    char expected[65];
    expected[0] = '\0';
    if (bootstrap->manifest_sha256[0] != L'\0' &&
        !dio_wide_to_utf8(
            bootstrap->manifest_sha256,
            expected,
            sizeof(expected))) {
        (void)swprintf_s(error, error_capacity, L"SHA-256 مانیفست معتبر نیست.");
        return false;
    }
    if (expected[0] != '\0' && !dio_is_hex_sha256(expected)) {
        (void)swprintf_s(error, error_capacity, L"SHA-256 مانیفست باید ۶۴ رقم باشد.");
        return false;
    }
    if (bootstrap->manifest_file[0] != L'\0') {
        if (!CopyFileW(bootstrap->manifest_file, manifest_path, FALSE)) {
            (void)swprintf_s(error, error_capacity, L"مانیفست محلی خوانده نشد.");
            return false;
        }
    } else if (expected[0] != '\0' && dio_hash_matches(manifest_path, expected)) {
        return true;
    } else {
        if (bootstrap->manifest_url[0] == L'\0') {
            (void)swprintf_s(
                error,
                error_capacity,
                L"این Build آدرس مانیفست Release ندارد.");
            return false;
        }
        wchar_t part[DIO_PATH_CAP];
        if (swprintf_s(part, _countof(part), L"%ls.part", manifest_path) < 0 ||
            !dio_download(
                bootstrap,
                bootstrap->manifest_url,
                part,
                0u,
                DIO_MAX_MANIFEST_BYTES,
                L"دریافت مانیفست",
                error,
                error_capacity) ||
            !MoveFileExW(
                part,
                manifest_path,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return false;
        }
    }
    if (expected[0] != '\0' && !dio_hash_matches(manifest_path, expected)) {
        DeleteFileW(manifest_path);
        (void)swprintf_s(error, error_capacity, L"SHA-256 مانیفست تطابق ندارد.");
        return false;
    }
    if (expected[0] == '\0' && bootstrap->manifest_file[0] == L'\0') {
        (void)swprintf_s(error, error_capacity, L"مانیفست Remote باید SHA-256 ثابت داشته باشد.");
        return false;
    }
    return true;
}

static bool dio_check_disk_space(
    const wchar_t *root,
    uint64_t required,
    uint64_t *free_space,
    wchar_t *error,
    size_t error_capacity) {
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(root, &free_bytes, NULL, NULL)) {
        (void)swprintf_s(error, error_capacity, L"فضای آزاد دیسک خوانده نشد.");
        return false;
    }
    *free_space = free_bytes.QuadPart;
    if (free_bytes.QuadPart < required) {
        (void)swprintf_s(
            error,
            error_capacity,
            L"حداقل %.1f GB فضای آزاد لازم است.",
            (double)required / (1024.0 * 1024.0 * 1024.0));
        return false;
    }
    return true;
}

static bool dio_component_cache_path(
    const DioBootstrap *bootstrap,
    const DioComponent *component,
    wchar_t *output,
    size_t capacity) {
    wchar_t cache[DIO_PATH_CAP];
    wchar_t filename[128];
    const wchar_t *extension = component->archive == DIO_ARCHIVE_CAB
        ? L"cab"
        : L"bin";
    if (!dio_join_path(cache, _countof(cache), bootstrap->root, L".dio\\cache") ||
        swprintf_s(
            filename,
            _countof(filename),
            L"%ls-%.12hs.%ls",
            component->id,
            component->sha256,
            extension) < 0) {
        return false;
    }
    return dio_join_path(output, capacity, cache, filename);
}

static bool dio_delete_file_if_present(const wchar_t *path) {
    if (DeleteFileW(path)) {
        return true;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static bool dio_cleanup_component_cache(
    const DioBootstrap *bootstrap,
    const DioManifest *manifest) {
    for (size_t index = 0u; index < manifest->component_count; ++index) {
        const DioComponent *component = &manifest->components[index];
        if (!dio_component_selected(component, bootstrap->selected_profile)) {
            continue;
        }
        wchar_t cache[DIO_PATH_CAP];
        wchar_t part[DIO_PATH_CAP];
        wchar_t etag[DIO_PATH_CAP];
        if (!dio_component_cache_path(
                bootstrap,
                component,
                cache,
                _countof(cache)) ||
            swprintf_s(part, _countof(part), L"%ls.part", cache) < 0 ||
            swprintf_s(etag, _countof(etag), L"%ls.etag", part) < 0 ||
            !dio_delete_file_if_present(cache) ||
            !dio_delete_file_if_present(part) ||
            !dio_delete_file_if_present(etag)) {
            return false;
        }
    }
    return true;
}

static bool dio_build_install_plan(
    const DioBootstrap *bootstrap,
    const DioManifest *manifest,
    DioInstallPlan *plan,
    wchar_t *error,
    size_t error_capacity) {
    ZeroMemory(plan, sizeof(*plan));
    wchar_t dio_root[DIO_PATH_CAP];
    if (!dio_join_path(dio_root, _countof(dio_root), bootstrap->root, L".dio")) {
        (void)swprintf_s(error, error_capacity, L"مسیر نصب معتبر نیست.");
        return false;
    }
    for (size_t index = 0u; index < manifest->component_count; ++index) {
        const DioComponent *component = &manifest->components[index];
        if (!dio_component_selected(component, bootstrap->selected_profile) ||
            !dio_add_size(&plan->installed_bytes, component->installed_bytes)) {
            if (dio_component_selected(component, bootstrap->selected_profile)) {
                (void)swprintf_s(error, error_capacity, L"حجم نصب نامعتبر است.");
                return false;
            }
            continue;
        }

        wchar_t cache[DIO_PATH_CAP];
        uint64_t cache_size = 0u;
        if (!dio_component_cache_path(
                bootstrap,
                component,
                cache,
                _countof(cache))) {
            (void)swprintf_s(error, error_capacity, L"مسیر Cache معتبر نیست.");
            return false;
        }
        const bool cache_ready = dio_get_file_size(cache, &cache_size) &&
            cache_size == component->download_bytes &&
            dio_hash_matches(cache, component->sha256);
        if (!cache_ready) {
            wchar_t part[DIO_PATH_CAP];
            uint64_t part_size = 0u;
            if (swprintf_s(part, _countof(part), L"%ls.part", cache) < 0) {
                (void)swprintf_s(error, error_capacity, L"مسیر Cache معتبر نیست.");
                return false;
            }
            (void)dio_get_file_size(part, &part_size);
            const uint64_t remaining = part_size < component->download_bytes
                ? component->download_bytes - part_size
                : component->download_bytes;
            if (!dio_add_size(&plan->download_bytes, remaining)) {
                (void)swprintf_s(error, error_capacity, L"حجم دانلود نامعتبر است.");
                return false;
            }
        }

        bool target_seen = false;
        for (size_t previous = 0u; previous < index; ++previous) {
            if (dio_component_selected(
                    &manifest->components[previous],
                    bootstrap->selected_profile) &&
                _wcsicmp(
                    manifest->components[previous].target,
                    component->target) == 0) {
                target_seen = true;
                break;
            }
        }
        if (!target_seen) {
            wchar_t destination[DIO_PATH_CAP];
            uint64_t replaced = 0u;
            if (!dio_join_path(
                    destination,
                    _countof(destination),
                    dio_root,
                    component->target) ||
                !dio_tree_size(destination, &replaced) ||
                !dio_add_size(&plan->replaced_bytes, replaced)) {
                (void)swprintf_s(error, error_capacity, L"حجم نسخهٔ قبلی خوانده نشد.");
                return false;
            }
        }
    }
    plan->required_bytes = DIO_DISK_RESERVE;
    if (!dio_add_size(&plan->required_bytes, plan->download_bytes) ||
        !dio_add_size(&plan->required_bytes, plan->installed_bytes) ||
        !dio_add_size(&plan->required_bytes, plan->replaced_bytes)) {
        (void)swprintf_s(error, error_capacity, L"حجم موردنیاز نامعتبر است.");
        return false;
    }
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(bootstrap->root, &free_bytes, NULL, NULL)) {
        (void)swprintf_s(error, error_capacity, L"فضای آزاد دیسک خوانده نشد.");
        return false;
    }
    plan->free_bytes = free_bytes.QuadPart;
    return true;
}

static bool dio_prepare_component(
    DioBootstrap *bootstrap,
    const DioComponent *component,
    const wchar_t *staging,
    wchar_t *error,
    size_t error_capacity) {
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    wchar_t cache_path[DIO_PATH_CAP];
    wchar_t part_path[DIO_PATH_CAP];
    if (!dio_component_cache_path(
            bootstrap,
            component,
            cache_path,
            _countof(cache_path)) ||
        swprintf_s(part_path, _countof(part_path), L"%ls.part", cache_path) < 0) {
        (void)swprintf_s(error, error_capacity, L"مسیر Cache بیش از حد بلند است.");
        return false;
    }
    uint64_t cache_size = 0u;
    if (!(dio_get_file_size(cache_path, &cache_size) &&
          cache_size == component->download_bytes &&
          dio_hash_matches(cache_path, component->sha256))) {
        uint64_t part_size = 0u;
        if (dio_get_file_size(part_path, &part_size) &&
            part_size >= component->download_bytes) {
            DeleteFileW(part_path);
            wchar_t etag[DIO_PATH_CAP];
            if (swprintf_s(etag, _countof(etag), L"%ls.etag", part_path) >= 0) {
                DeleteFileW(etag);
            }
        }
        if (!dio_download(
                bootstrap,
                component->url,
                part_path,
                component->download_bytes,
                component->download_bytes,
                component->id,
                error,
                error_capacity)) {
            return false;
        }
        uint64_t downloaded = 0u;
        if (!dio_get_file_size(part_path, &downloaded)) {
            (void)swprintf_s(
                error,
                error_capacity,
                L"فایل موقت جزء %ls خوانده نشد.",
                component->id);
            return false;
        }
        if (downloaded < component->download_bytes) {
            (void)swprintf_s(
                error,
                error_capacity,
                L"دانلود جزء %ls ناتمام ماند و در تلاش بعدی ادامه پیدا می‌کند.",
                component->id);
            return false;
        }
        if (downloaded > component->download_bytes ||
            !dio_hash_matches(part_path, component->sha256)) {
            DeleteFileW(part_path);
            wchar_t etag[DIO_PATH_CAP];
            if (swprintf_s(etag, _countof(etag), L"%ls.etag", part_path) >= 0) {
                DeleteFileW(etag);
            }
            (void)swprintf_s(
                error,
                error_capacity,
                L"اندازه یا SHA-256 جزء %ls صحیح نیست.",
                component->id);
            return false;
        }
        if (!MoveFileExW(
                part_path,
                cache_path,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            (void)swprintf_s(error, error_capacity, L"انتقال فایل Cache شکست خورد.");
            return false;
        }
    }

    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }

    wchar_t staged_target[DIO_PATH_CAP];
    if (!dio_join_path(
            staged_target,
            _countof(staged_target),
            staging,
            component->target)) {
        (void)swprintf_s(error, error_capacity, L"مسیر Staging بیش از حد بلند است.");
        return false;
    }
    wchar_t status[256];
    (void)swprintf_s(status, _countof(status), L"آماده‌سازی %ls", component->id);
    (void)dio_post_status(bootstrap, 0u, 0u, status);
    dio_log(bootstrap, status);
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    if (component->archive == DIO_ARCHIVE_CAB) {
        if (!dio_extract_cabinet(
                cache_path,
                staged_target,
                bootstrap->cancel_event)) {
            const DWORD cabinet_error = GetLastError();
            if (dio_cancel_requested(bootstrap, error, error_capacity)) {
                return false;
            }
            (void)swprintf_s(
                error,
                error_capacity,
                L"استخراج CAB جزء %ls شکست خورد (خطای Windows %lu).",
                component->id,
                cabinet_error);
            return false;
        }
    } else if (!dio_ensure_parent(staged_target) ||
               !CopyFileW(cache_path, staged_target, FALSE)) {
        (void)swprintf_s(
            error,
            error_capacity,
            L"آماده‌سازی فایل %ls شکست خورد.",
            component->id);
        return false;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    (void)swprintf_s(status, _countof(status), L"جزء %ls آماده شد", component->id);
    dio_log(bootstrap, status);
    return true;
}

static bool dio_publish_component(
    DioBootstrap *bootstrap,
    const DioComponent *component,
    const wchar_t *staging,
    wchar_t *error,
    size_t error_capacity) {
    wchar_t staged[DIO_PATH_CAP];
    wchar_t dio_root[DIO_PATH_CAP];
    wchar_t destination[DIO_PATH_CAP];
    wchar_t backup[DIO_PATH_CAP];
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    if (!dio_join_path(staged, _countof(staged), staging, component->target) ||
        !dio_join_path(dio_root, _countof(dio_root), bootstrap->root, L".dio") ||
        !dio_join_path(
            destination,
            _countof(destination),
            dio_root,
            component->target) ||
        swprintf_s(
            backup,
            _countof(backup),
            L"%ls.dio-previous",
            destination) < 0 ||
        !dio_ensure_parent(destination)) {
        (void)swprintf_s(error, error_capacity, L"مسیر انتشار معتبر نیست.");
        return false;
    }
    if (dio_path_exists(backup)) {
        if ((!dio_path_exists(destination) &&
             !MoveFileExW(
                 backup,
                 destination,
                 MOVEFILE_WRITE_THROUGH)) ||
            (dio_path_exists(destination) &&
             !dio_delete_tree(backup))) {
            (void)swprintf_s(
                error,
                error_capacity,
                L"بازیابی نسخهٔ قبلی %ls شکست خورد.",
                component->id);
            return false;
        }
    }
    const bool replacing = dio_path_exists(destination);
    if (replacing &&
        !MoveFileExW(
            destination,
            backup,
            MOVEFILE_WRITE_THROUGH)) {
        (void)swprintf_s(
            error,
            error_capacity,
            L"نسخهٔ قبلی %ls قابل جایگزینی نیست.",
            component->id);
        return false;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        if (replacing) {
            (void)MoveFileExW(backup, destination, MOVEFILE_WRITE_THROUGH);
        }
        return false;
    }
    if (!MoveFileExW(staged, destination, MOVEFILE_WRITE_THROUGH)) {
        if (replacing) {
            (void)MoveFileExW(
                backup,
                destination,
                MOVEFILE_WRITE_THROUGH);
        }
        (void)swprintf_s(
            error,
            error_capacity,
            L"انتشار اتمیک %ls شکست خورد.",
            component->id);
        return false;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        const bool moved_back = MoveFileExW(
            destination,
            staged,
            MOVEFILE_WRITE_THROUGH) != FALSE;
        const bool restored = !replacing || MoveFileExW(
            backup,
            destination,
            MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!moved_back || !restored) {
            (void)swprintf_s(
                error,
                error_capacity,
                L"لغو انجام شد، اما بازیابی جزء %ls کامل نشد؛ Repair را اجرا کنید.",
                component->id);
        }
        return false;
    }
    if (replacing) {
        (void)dio_delete_tree(backup);
    }
    wchar_t status[256];
    (void)swprintf_s(status, _countof(status), L"جزء %ls منتشر شد", component->id);
    dio_log(bootstrap, status);
    return true;
}

static bool dio_write_install_marker(
    const DioBootstrap *bootstrap,
    const DioManifest *manifest,
    const wchar_t *manifest_path) {
    char version[128];
    char entrypoint[DIO_PATH_CAP * 3u];
    char manifest_hash[65];
    if (!dio_wide_to_utf8(manifest->version, version, sizeof(version)) ||
        !dio_wide_to_utf8(manifest->entrypoint, entrypoint, sizeof(entrypoint)) ||
        !dio_sha256_file(manifest_path, manifest_hash)) {
        return false;
    }
    const char *profile = bootstrap->selected_profile == DIO_PROFILE_LARGE
        ? "large"
        : "small";
    char json[2048];
    const int length = sprintf_s(
        json,
        sizeof(json),
        "{\"schema\":1,\"version\":\"%s\",\"profile\":\"%s\","
        "\"manifest_sha256\":\"%s\",\"entrypoint\":\"%s\"}\n",
        version,
        profile,
        manifest_hash,
        entrypoint);
    wchar_t marker[DIO_PATH_CAP];
    return length > 0 &&
        dio_join_path(
            marker,
            _countof(marker),
            bootstrap->root,
            L".dio\\data\\install.json") &&
        dio_write_file_atomic(marker, json, (size_t)length);
}

static bool dio_launch(
    DioBootstrap *bootstrap,
    const DioManifest *manifest,
    wchar_t *error,
    size_t error_capacity) {
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    wchar_t dio_root[DIO_PATH_CAP];
    wchar_t executable[DIO_PATH_CAP];
    wchar_t working[DIO_PATH_CAP];
    wchar_t cache[DIO_PATH_CAP];
    wchar_t temporary[DIO_PATH_CAP];
    if (!dio_join_path(dio_root, _countof(dio_root), bootstrap->root, L".dio") ||
        !dio_join_path(
            executable,
            _countof(executable),
            dio_root,
            manifest->entrypoint) ||
        !dio_file_exists(executable) ||
        !dio_parent_path(working, _countof(working), executable) ||
        !dio_join_path(cache, _countof(cache), dio_root, L"cache") ||
        !dio_join_path(temporary, _countof(temporary), cache, L"tmp") ||
        !dio_ensure_directory(temporary)) {
        (void)swprintf_s(error, error_capacity, L"فایل اجرایی نصب‌شده پیدا نشد.");
        return false;
    }
    (void)SetEnvironmentVariableW(L"DIO_ROOT", dio_root);
    (void)SetEnvironmentVariableW(L"HF_HOME", cache);
    (void)SetEnvironmentVariableW(L"XDG_CACHE_HOME", cache);
    (void)SetEnvironmentVariableW(L"PIP_CACHE_DIR", cache);
    (void)SetEnvironmentVariableW(L"TEMP", temporary);
    (void)SetEnvironmentVariableW(L"TMP", temporary);

    wchar_t command[DIO_PATH_CAP + 4u];
    if (swprintf_s(command, _countof(command), L"\"%ls\"", executable) < 0) {
        return false;
    }
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    if (!CreateProcessW(
            executable,
            command,
            NULL,
            NULL,
            FALSE,
            CREATE_SUSPENDED,
            NULL,
            working,
            &startup,
            &process)) {
        (void)swprintf_s(error, error_capacity, L"اجرای DIO Voice شکست خورد.");
        return false;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity) ||
        ResumeThread(process.hThread) == (DWORD)-1) {
        (void)TerminateProcess(process.hProcess, 1u);
        (void)WaitForSingleObject(process.hProcess, 5000u);
        if (error[0] == L'\0') {
            (void)swprintf_s(error, error_capacity, L"اجرای DIO Voice شکست خورد.");
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return false;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        (void)TerminateProcess(process.hProcess, 1u);
        (void)WaitForSingleObject(process.hProcess, 5000u);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    dio_log(bootstrap, L"LAUNCH complete");
    return true;
}

static bool dio_install(
    DioBootstrap *bootstrap,
    const DioManifest *manifest,
    const wchar_t *manifest_path,
    wchar_t *error,
    size_t error_capacity) {
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    DioInstallPlan plan;
    if (!dio_build_install_plan(
            bootstrap,
            manifest,
            &plan,
            error,
            error_capacity)) {
        return false;
    }
    wchar_t requirements[384];
    (void)swprintf_s(
        requirements,
        _countof(requirements),
        L"دانلود باقی‌مانده %.1f MB  •  نصب %.2f GB  •  نیاز اوج %.2f GB  •  آزاد %.2f GB",
        (double)plan.download_bytes / (1024.0 * 1024.0),
        (double)plan.installed_bytes / (1024.0 * 1024.0 * 1024.0),
        (double)plan.required_bytes / (1024.0 * 1024.0 * 1024.0),
        (double)plan.free_bytes / (1024.0 * 1024.0 * 1024.0));
    (void)dio_post_plan(bootstrap, requirements);
    dio_log(bootstrap, requirements);
    uint64_t checked_free = 0u;
    if (!dio_check_disk_space(
            bootstrap->root,
            plan.required_bytes,
            &checked_free,
            error,
            error_capacity) ||
        dio_cancel_requested(bootstrap, error, error_capacity)) {
        return false;
    }
    wchar_t staging_base[DIO_PATH_CAP];
    wchar_t staging[DIO_PATH_CAP];
    if (!dio_join_path(
            staging_base,
            _countof(staging_base),
            bootstrap->root,
            L".dio\\staging") ||
        !dio_join_path(
            staging,
            _countof(staging),
            staging_base,
            manifest->version)) {
        (void)swprintf_s(error, error_capacity, L"مسیر Staging معتبر نیست.");
        return false;
    }
    if (dio_path_exists(staging) && !dio_delete_tree(staging)) {
        (void)swprintf_s(error, error_capacity, L"Staging قبلی پاک نشد.");
        return false;
    }
    if (!dio_ensure_directory(staging)) {
        (void)swprintf_s(error, error_capacity, L"Staging ساخته نشد.");
        return false;
    }
    bool success = false;
    for (size_t index = 0u; index < manifest->component_count; ++index) {
        const DioComponent *component = &manifest->components[index];
        if (!dio_component_selected(component, bootstrap->selected_profile)) {
            continue;
        }
        if (dio_cancel_requested(bootstrap, error, error_capacity) ||
            !dio_prepare_component(
                bootstrap,
                component,
                staging,
                error,
                error_capacity) ||
            dio_cancel_requested(bootstrap, error, error_capacity)) {
            goto cleanup;
        }
    }
    wchar_t staged_entrypoint[DIO_PATH_CAP];
    if (!dio_join_path(
            staged_entrypoint,
            _countof(staged_entrypoint),
            staging,
            manifest->entrypoint) ||
        !dio_file_exists(staged_entrypoint)) {
        (void)swprintf_s(error, error_capacity, L"Entrypoint داخل Payload وجود ندارد.");
        goto cleanup;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        goto cleanup;
    }
    for (size_t index = 0u; index < manifest->component_count; ++index) {
        const DioComponent *component = &manifest->components[index];
        if (!dio_component_selected(component, bootstrap->selected_profile)) {
            continue;
        }
        bool already_published = false;
        for (size_t previous = 0u; previous < index; ++previous) {
            if (dio_component_selected(
                    &manifest->components[previous],
                    bootstrap->selected_profile) &&
                _wcsicmp(
                    component->target,
                    manifest->components[previous].target) == 0) {
                already_published = true;
                break;
            }
        }
        if (!already_published &&
            (dio_cancel_requested(bootstrap, error, error_capacity) ||
             !dio_publish_component(
                 bootstrap,
                 component,
                 staging,
                 error,
                 error_capacity) ||
             dio_cancel_requested(bootstrap, error, error_capacity))) {
            goto cleanup;
        }
    }
    if (!dio_delete_tree(staging)) {
        (void)swprintf_s(error, error_capacity, L"پاک‌سازی Staging شکست خورد.");
        goto cleanup;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        goto cleanup;
    }
    if (!dio_write_install_marker(bootstrap, manifest, manifest_path)) {
        (void)swprintf_s(error, error_capacity, L"ثبت وضعیت نصب شکست خورد.");
        goto cleanup;
    }
    wchar_t marker[DIO_PATH_CAP];
    if (!dio_join_path(
            marker,
            _countof(marker),
            bootstrap->root,
            L".dio\\data\\install.json")) {
        (void)swprintf_s(error, error_capacity, L"مسیر وضعیت نصب معتبر نیست.");
        goto cleanup;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        DeleteFileW(marker);
        goto cleanup;
    }
    if (!dio_cleanup_component_cache(bootstrap, manifest)) {
        DeleteFileW(marker);
        (void)swprintf_s(error, error_capacity, L"پاک‌سازی Cache نصب شکست خورد.");
        goto cleanup;
    }
    if (dio_cancel_requested(bootstrap, error, error_capacity)) {
        DeleteFileW(marker);
        goto cleanup;
    }
    success = true;
    dio_log(bootstrap, L"INSTALL complete");

cleanup:
    if (dio_path_exists(staging) && !dio_delete_tree(staging) && success) {
        (void)swprintf_s(error, error_capacity, L"پاک‌سازی Staging شکست خورد.");
        success = false;
    }
    return success;
}

static bool dio_install_marker_valid(
    const DioBootstrap *bootstrap,
    const DioManifest *manifest,
    const wchar_t *manifest_path) {
    wchar_t marker_path[DIO_PATH_CAP];
    char *json = NULL;
    size_t json_size = 0u;
    char version[128];
    char entrypoint[DIO_PATH_CAP * 3u];
    char manifest_hash[65];
    if (!dio_join_path(
            marker_path,
            _countof(marker_path),
            bootstrap->root,
            L".dio\\data\\install.json") ||
        !dio_read_file(marker_path, 4096u, &json, &json_size) ||
        !dio_wide_to_utf8(manifest->version, version, sizeof(version)) ||
        !dio_wide_to_utf8(manifest->entrypoint, entrypoint, sizeof(entrypoint)) ||
        !dio_sha256_file(manifest_path, manifest_hash)) {
        free(json);
        return false;
    }
    yyjson_doc *document = yyjson_read(json, json_size, YYJSON_READ_NOFLAG);
    free(json);
    if (document == NULL) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(document);
    const char *stored_version = NULL;
    const char *stored_profile = NULL;
    const char *stored_hash = NULL;
    const char *stored_entrypoint = NULL;
    const char *profile = bootstrap->selected_profile == DIO_PROFILE_LARGE
        ? "large"
        : "small";
    const bool matches = yyjson_is_obj(root) &&
        dio_json_string(root, "version", &stored_version) &&
        dio_json_string(root, "profile", &stored_profile) &&
        dio_json_string(root, "manifest_sha256", &stored_hash) &&
        dio_json_string(root, "entrypoint", &stored_entrypoint) &&
        strcmp(stored_version, version) == 0 &&
        strcmp(stored_profile, profile) == 0 &&
        _stricmp(stored_hash, manifest_hash) == 0 &&
        strcmp(stored_entrypoint, entrypoint) == 0;
    yyjson_doc_free(document);
    if (!matches) {
        return false;
    }
    wchar_t dio_root[DIO_PATH_CAP];
    wchar_t executable[DIO_PATH_CAP];
    return dio_join_path(dio_root, _countof(dio_root), bootstrap->root, L".dio") &&
        dio_join_path(
            executable,
            _countof(executable),
            dio_root,
            manifest->entrypoint) &&
        dio_file_exists(executable);
}

static bool dio_prepare_directories(DioBootstrap *bootstrap) {
    static const wchar_t *const relative[] = {
        L".dio",
        L".dio\\versions",
        L".dio\\models",
        L".dio\\data",
        L".dio\\cache",
        L".dio\\cache\\tmp",
        L".dio\\logs",
        L".dio\\staging",
    };
    for (size_t index = 0u; index < _countof(relative); ++index) {
        wchar_t path[DIO_PATH_CAP];
        if (!dio_join_path(
                path,
                _countof(path),
                bootstrap->root,
                relative[index]) ||
            !dio_ensure_directory(path)) {
            return false;
        }
    }
    return true;
}

static void dio_post_error(DioBootstrap *bootstrap, const wchar_t *message) {
    dio_log(bootstrap, message);
    if (bootstrap->window == NULL) {
        return;
    }
    const size_t bytes = (wcslen(message) + 1u) * sizeof(*message);
    wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0u, bytes);
    if (copy != NULL) {
        memcpy(copy, message, bytes);
        if (!PostMessageW(bootstrap->window, DIO_WM_ERROR, 0u, (LPARAM)copy)) {
            HeapFree(GetProcessHeap(), 0u, copy);
        }
    }
}

static DWORD WINAPI dio_worker(LPVOID context) {
    DioBootstrap *bootstrap = (DioBootstrap *)context;
    wchar_t error[512];
    error[0] = L'\0';
    wchar_t manifest_path[DIO_PATH_CAP];
    DioManifest *manifest = (DioManifest *)calloc(1u, sizeof(*manifest));
    if (manifest == NULL) {
        dio_post_error(bootstrap, L"حافظهٔ کافی برای نصب وجود ندارد.");
        return 1u;
    }
    (void)dio_post_status(bootstrap, 0u, 0u, L"بررسی پوشهٔ قابل‌حمل…");
    if (!dio_prepare_directories(bootstrap) ||
        !dio_prepare_manifest(
            bootstrap,
            manifest_path,
            _countof(manifest_path),
            error,
            _countof(error))) {
        if (error[0] == L'\0') {
            (void)swprintf_s(error, _countof(error), L"پوشهٔ نصب آماده نشد.");
        }
        dio_post_error(bootstrap, error);
        free(manifest);
        return 1u;
    }
    dio_log(bootstrap, L"BOOTSTRAP started");
    char *json = NULL;
    size_t json_size = 0u;
    if (!dio_read_file(
            manifest_path,
            DIO_MAX_MANIFEST_BYTES,
            &json,
            &json_size) ||
        !dio_parse_manifest(
            json,
            json_size,
            bootstrap->allow_test_http,
            manifest,
            error,
            _countof(error))) {
        free(json);
        dio_post_error(bootstrap, error[0] != L'\0' ? error : L"مانیفست خوانده نشد.");
        free(manifest);
        return 1u;
    }
    free(json);
    if (dio_cancel_requested(bootstrap, error, _countof(error))) {
        dio_post_error(bootstrap, error);
        free(manifest);
        return 1u;
    }
    const bool ready = !bootstrap->force_repair &&
        dio_install_marker_valid(bootstrap, manifest, manifest_path);
    if (!ready) {
        if (!dio_install(
                bootstrap,
                manifest,
                manifest_path,
                error,
                _countof(error))) {
            dio_post_error(bootstrap, error);
            free(manifest);
            return 1u;
        }
    }
    if (dio_cancel_requested(bootstrap, error, _countof(error))) {
        dio_post_error(bootstrap, error);
        free(manifest);
        return 1u;
    }
    if (bootstrap->install_only) {
        dio_log(bootstrap, L"INSTALL-ONLY complete");
        free(manifest);
        return 0u;
    }
    (void)dio_post_status(bootstrap, 1u, 1u, L"آماده است؛ اجرای DIO Voice…");
    if (!dio_launch(bootstrap, manifest, error, _countof(error))) {
        dio_post_error(bootstrap, error);
        free(manifest);
        return 1u;
    }
    free(manifest);
    (void)PostMessageW(bootstrap->window, DIO_WM_DONE, 0u, 0u);
    return 0u;
}

static void dio_set_font(HWND control, HFONT font) {
    if (control != NULL && font != NULL) {
        SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

static void dio_update_font(DioBootstrap *bootstrap) {
    const int font_height = -MulDiv(
        10,
        (int)GetDpiForWindow(bootstrap->window),
        72);
    HFONT font = CreateFontW(
        font_height,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    if (font == NULL) {
        return;
    }
    HFONT title_font = CreateFontW(
        -MulDiv(15, (int)GetDpiForWindow(bootstrap->window), 72),
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    if (title_font == NULL) {
        DeleteObject(font);
        return;
    }
    HFONT previous = bootstrap->font;
    HFONT previous_title = bootstrap->title_font;
    bootstrap->font = font;
    bootstrap->title_font = title_font;
    dio_set_font(bootstrap->title, title_font);
    dio_set_font(bootstrap->detail, font);
    dio_set_font(bootstrap->requirements, font);
    dio_set_font(bootstrap->log_path, font);
    dio_set_font(bootstrap->profile_label, font);
    dio_set_font(bootstrap->profile, font);
    dio_set_font(bootstrap->repair, font);
    dio_set_font(bootstrap->primary, font);
    dio_set_font(bootstrap->cancel, font);
    if (previous != NULL) {
        DeleteObject(previous);
    }
    if (previous_title != NULL) {
        DeleteObject(previous_title);
    }
}

static bool dio_high_contrast(void) {
    HIGHCONTRASTW contrast;
    ZeroMemory(&contrast, sizeof(contrast));
    contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(
            SPI_GETHIGHCONTRAST,
            sizeof(contrast),
            &contrast,
            0u) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0u;
}

static int dio_scale(HWND window, int value) {
    const UINT dpi = GetDpiForWindow(window);
    return MulDiv(value, (int)dpi, 96);
}

static void dio_layout(DioBootstrap *bootstrap) {
    RECT client;
    GetClientRect(bootstrap->window, &client);
    const int margin = dio_scale(bootstrap->window, 32);
    const int width = client.right - client.left - (margin * 2);
    int top = dio_scale(bootstrap->window, 42);
    MoveWindow(
        bootstrap->title,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 32),
        TRUE);
    top += dio_scale(bootstrap->window, 48);
    MoveWindow(
        bootstrap->detail,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 36),
        TRUE);
    top += dio_scale(bootstrap->window, 42);
    MoveWindow(
        bootstrap->requirements,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 38),
        TRUE);
    top += dio_scale(bootstrap->window, 42);
    MoveWindow(
        bootstrap->log_path,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 32),
        TRUE);
    top += dio_scale(bootstrap->window, 38);
    MoveWindow(
        bootstrap->profile_label,
        client.right - margin - dio_scale(bootstrap->window, 150),
        top,
        dio_scale(bootstrap->window, 150),
        dio_scale(bootstrap->window, 24),
        TRUE);
    top += dio_scale(bootstrap->window, 28);
    MoveWindow(
        bootstrap->profile,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 180),
        TRUE);
    top += dio_scale(bootstrap->window, 48);
    MoveWindow(
        bootstrap->repair,
        client.right - margin - dio_scale(bootstrap->window, 210),
        top,
        dio_scale(bootstrap->window, 210),
        dio_scale(bootstrap->window, 26),
        TRUE);
    top += dio_scale(bootstrap->window, 34);
    MoveWindow(
        bootstrap->progress,
        margin,
        top,
        width,
        dio_scale(bootstrap->window, 10),
        TRUE);
    const int button_top = client.bottom - margin - dio_scale(bootstrap->window, 36);
    const int primary_width = dio_scale(bootstrap->window, 150);
    const int secondary_width = dio_scale(bootstrap->window, 96);
    MoveWindow(
        bootstrap->primary,
        client.right - margin - primary_width,
        button_top,
        primary_width,
        dio_scale(bootstrap->window, 36),
        TRUE);
    MoveWindow(
        bootstrap->cancel,
        client.right - margin - primary_width - dio_scale(bootstrap->window, 12) -
            secondary_width,
        button_top,
        secondary_width,
        dio_scale(bootstrap->window, 36),
        TRUE);
}

static void dio_set_busy(DioBootstrap *bootstrap, bool busy) {
    bootstrap->busy = busy;
    EnableWindow(bootstrap->profile, !busy);
    EnableWindow(bootstrap->repair, !busy);
    EnableWindow(bootstrap->primary, !busy);
    EnableWindow(bootstrap->cancel, TRUE);
    SetWindowTextW(bootstrap->cancel, busy ? L"لغو" : L"بستن");
    if (busy) {
        SendMessageW(bootstrap->progress, PBM_SETMARQUEE, TRUE, 30u);
    } else {
        SendMessageW(bootstrap->progress, PBM_SETMARQUEE, FALSE, 0u);
    }
}

static void dio_start(DioBootstrap *bootstrap) {
    if (bootstrap->busy) {
        return;
    }
    const LRESULT selected = SendMessageW(
        bootstrap->profile,
        CB_GETCURSEL,
        0u,
        0u);
    bootstrap->selected_profile = selected == 1
        ? DIO_PROFILE_LARGE
        : DIO_PROFILE_SMALL;
    bootstrap->force_repair = SendMessageW(
        bootstrap->repair,
        BM_GETCHECK,
        0u,
        0u) == BST_CHECKED;
    ResetEvent(bootstrap->cancel_event);
    SetWindowTextW(bootstrap->primary, L"در حال آماده‌سازی…");
    dio_set_busy(bootstrap, true);
    SetWindowTextW(bootstrap->detail, L"در حال بررسی مانیفست و فایل‌های محلی…");
    bootstrap->worker = CreateThread(
        NULL,
        0u,
        dio_worker,
        bootstrap,
        0u,
        NULL);
    if (bootstrap->worker == NULL) {
        dio_set_busy(bootstrap, false);
        SetWindowTextW(bootstrap->detail, L"Worker نصب ساخته نشد.");
        SetWindowTextW(bootstrap->primary, L"تلاش دوباره");
    }
}

static LRESULT CALLBACK dio_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    DioBootstrap *bootstrap = (DioBootstrap *)GetWindowLongPtrW(
        window,
        GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        bootstrap = (DioBootstrap *)create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)bootstrap);
        bootstrap->window = window;
    }
    if (bootstrap == NULL) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    switch (message) {
        case WM_CREATE: {
            const DWORD static_style = WS_CHILD | WS_VISIBLE | SS_RIGHT;
            bootstrap->title = CreateWindowExW(
                0u,
                WC_STATICW,
                L"راه‌اندازی DIO Voice",
                static_style,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            bootstrap->detail = CreateWindowExW(
                0u,
                WC_STATICW,
                L"همهٔ نیازمندی‌ها فقط داخل همین پوشه آماده می‌شوند.",
                static_style,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            bootstrap->requirements = CreateWindowExW(
                0u,
                WC_STATICW,
                L"اندازهٔ دقیق دانلود و فضای لازم پس از خواندن مانیفست نمایش داده می‌شود.",
                static_style,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            wchar_t log_label[DIO_PATH_CAP + 32u];
            (void)swprintf_s(
                log_label,
                _countof(log_label),
                L"Log: %ls\\.dio\\logs\\bootstrap.log",
                bootstrap->root);
            bootstrap->log_path = CreateWindowExW(
                WS_EX_LTRREADING,
                WC_STATICW,
                log_label,
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            bootstrap->profile_label = CreateWindowExW(
                0u,
                WC_STATICW,
                L"مدل تشخیص گفتار",
                static_style,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            bootstrap->profile = CreateWindowExW(
                WS_EX_CLIENTEDGE | WS_EX_RTLREADING | WS_EX_RIGHT,
                WC_COMBOBOXW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                    CBS_HASSTRINGS | WS_VSCROLL,
                0,
                0,
                0,
                0,
                window,
                (HMENU)(INT_PTR)1001,
                bootstrap->instance,
                NULL);
            (void)SendMessageW(
                bootstrap->profile,
                CB_ADDSTRING,
                0u,
                (LPARAM)L"مدل کم‌حجم (\x202aVosk Small\x202c) — دانلود سریع‌تر و مصرف کمتر");
            (void)SendMessageW(
                bootstrap->profile,
                CB_ADDSTRING,
                0u,
                (LPARAM)L"مدل دقیق (\x202aVosk Large\x202c) — مناسب سیستم‌های قوی");
            const DioProfile recommended = dio_recommended_profile(bootstrap->root);
            (void)SendMessageW(
                bootstrap->profile,
                CB_SETCURSEL,
                recommended == DIO_PROFILE_LARGE ? 1u : 0u,
                0u);
            bootstrap->repair = CreateWindowExW(
                0u,
                WC_BUTTONW,
                L"بازسازی کامل همهٔ اجزا",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX |
                    BS_RIGHT,
                0,
                0,
                0,
                0,
                window,
                (HMENU)(INT_PTR)1003,
                bootstrap->instance,
                NULL);
            if (bootstrap->force_repair) {
                (void)SendMessageW(
                    bootstrap->repair,
                    BM_SETCHECK,
                    BST_CHECKED,
                    0u);
            }
            bootstrap->progress = CreateWindowExW(
                0u,
                PROGRESS_CLASSW,
                L"",
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE,
                0,
                0,
                0,
                0,
                window,
                NULL,
                bootstrap->instance,
                NULL);
            bootstrap->primary = CreateWindowExW(
                0u,
                WC_BUTTONW,
                L"آماده‌سازی و اجرا",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                0,
                0,
                window,
                (HMENU)(INT_PTR)1002,
                bootstrap->instance,
                NULL);
            bootstrap->cancel = CreateWindowExW(
                0u,
                WC_BUTTONW,
                L"بستن",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                window,
                (HMENU)(INT_PTR)IDCANCEL,
                bootstrap->instance,
                NULL);
            dio_update_font(bootstrap);
            dio_layout(bootstrap);
            return 0;
        }
        case WM_SIZE:
            dio_layout(bootstrap);
            return 0;
        case WM_DPICHANGED: {
            const RECT *suggested = (const RECT *)lparam;
            SetWindowPos(
                window,
                NULL,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            dio_update_font(bootstrap);
            dio_layout(bootstrap);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == 1002 && HIWORD(wparam) == BN_CLICKED) {
                dio_start(bootstrap);
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL && HIWORD(wparam) == BN_CLICKED) {
                if (bootstrap->busy) {
                    SetEvent(bootstrap->cancel_event);
                    EnableWindow(bootstrap->cancel, FALSE);
                    SetWindowTextW(bootstrap->detail, L"در حال لغو امن…");
                } else {
                    DestroyWindow(window);
                }
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wparam;
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case DIO_WM_STATUS: {
            DioUiStatus *status = (DioUiStatus *)lparam;
            SetWindowTextW(bootstrap->detail, status->text);
            if (status->total > 0u) {
                const uint64_t scaled = status->completed >= status->total
                    ? 1000u
                    : (status->completed * 1000u / status->total);
                SendMessageW(bootstrap->progress, PBM_SETMARQUEE, FALSE, 0u);
                SendMessageW(bootstrap->progress, PBM_SETRANGE32, 0u, 1000u);
                SendMessageW(bootstrap->progress, PBM_SETPOS, (WPARAM)scaled, 0u);
            }
            HeapFree(GetProcessHeap(), 0u, status);
            return 0;
        }
        case DIO_WM_PLAN: {
            wchar_t *text = (wchar_t *)lparam;
            SetWindowTextW(bootstrap->requirements, text);
            HeapFree(GetProcessHeap(), 0u, text);
            return 0;
        }
        case DIO_WM_ERROR: {
            wchar_t *error = (wchar_t *)lparam;
            const bool cancelled = dio_cancelled(bootstrap);
            if (bootstrap->worker != NULL) {
                CloseHandle(bootstrap->worker);
                bootstrap->worker = NULL;
            }
            dio_set_busy(bootstrap, false);
            SetWindowTextW(bootstrap->detail, error);
            SetWindowTextW(
                bootstrap->primary,
                cancelled ? L"شروع دوباره" : L"تلاش دوباره");
            if (!cancelled) {
                MessageBoxW(window, error, L"DIO Voice", MB_OK | MB_ICONERROR);
            }
            HeapFree(GetProcessHeap(), 0u, error);
            return 0;
        }
        case DIO_WM_DONE:
            if (bootstrap->worker != NULL) {
                CloseHandle(bootstrap->worker);
                bootstrap->worker = NULL;
            }
            dio_set_busy(bootstrap, false);
            SetWindowTextW(bootstrap->detail, L"آماده شد و برنامه اجرا شد.");
            SetTimer(window, 1u, 700u, NULL);
            return 0;
        case WM_TIMER:
            if (wparam == 1u) {
                KillTimer(window, 1u);
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT client;
            GetClientRect(window, &client);
            HBRUSH background = GetSysColorBrush(COLOR_WINDOW);
            FillRect(dc, &client, background);
            RECT keyline = {
                dio_scale(window, 32),
                dio_scale(window, 25),
                client.right - dio_scale(window, 32),
                dio_scale(window, 28)};
            const bool high_contrast = dio_high_contrast();
            HBRUSH accent = high_contrast
                ? GetSysColorBrush(COLOR_HIGHLIGHT)
                : CreateSolidBrush(RGB(43, 110, 181));
            FillRect(dc, &keyline, accent);
            if (!high_contrast) {
                DeleteObject(accent);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SETTINGCHANGE:
            InvalidateRect(window, NULL, TRUE);
            return 0;
        case WM_CLOSE:
            if (bootstrap->busy) {
                SetEvent(bootstrap->cancel_event);
                EnableWindow(bootstrap->cancel, FALSE);
                SetWindowTextW(bootstrap->detail, L"در حال لغو امن…");
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static bool dio_get_application_root(wchar_t *root, size_t capacity) {
    const DWORD length = GetModuleFileNameW(NULL, root, (DWORD)capacity);
    if (length == 0u || length >= capacity) {
        return false;
    }
    wchar_t *separator = wcsrchr(root, L'\\');
    if (separator == NULL) {
        return false;
    }
    *separator = L'\0';
    return true;
}

static int dio_self_test(void) {
    static const char sample[] =
        "{\"schema\":1,\"version\":\"0.1.0\","
        "\"entrypoint\":\"versions/0.1.0/dio-voice.exe\","
        "\"components\":[{\"id\":\"core\","
        "\"url\":\"https://example.test/core.cab\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"bytes\":10,\"installed_bytes\":20,"
        "\"target\":\"versions/0.1.0\",\"profile\":\"all\","
        "\"archive\":\"cab\"}]}";
    DioManifest manifest;
    wchar_t error[128];
    if (!dio_safe_version(L"0.1.0-rc.1") ||
        !dio_url_allowed(L"https://example.test/payload.bin", false) ||
        !dio_url_allowed(L"http://127.0.0.1:8123/payload.bin", true) ||
        !dio_url_allowed(L"http://localhost:8123/payload.bin", true) ||
        dio_url_allowed(L"http://127.0.0.1:8123/payload.bin", false) ||
        dio_url_allowed(L"http://example.test/payload.bin", true) ||
        dio_safe_version(L"..") ||
        dio_safe_version(L"release.") ||
        !dio_safe_relative_path(L"versions\\0.1.0") ||
        !dio_safe_relative_path(L"models\\normal.con\\file.bin") ||
        !dio_path_is_parent(L"models", L"models\\vosk") ||
        dio_path_is_parent(L"models", L"models-old\\vosk") ||
        dio_path_is_parent(L"models", L"models") ||
        dio_safe_relative_path(L"..\\escape") ||
        dio_safe_relative_path(L".. \\escape") ||
        dio_safe_relative_path(L"C:\\escape") ||
        dio_safe_relative_path(L"\\\\server\\share") ||
        dio_safe_relative_path(L"\\\\?\\C:\\escape") ||
        dio_safe_relative_path(L"versions\\.\\escape") ||
        dio_safe_relative_path(L"versions\\foo.\\escape") ||
        dio_safe_relative_path(L"versions\\foo \\escape") ||
        dio_safe_relative_path(L"versions\\file:stream") ||
        dio_safe_relative_path(L"versions\\CON") ||
        dio_safe_relative_path(L"versions\\con.txt") ||
        dio_safe_relative_path(L"versions\\con .txt") ||
        dio_safe_relative_path(L"versions\\PRN.log") ||
        dio_safe_relative_path(L"versions\\AUX") ||
        dio_safe_relative_path(L"versions\\NUL.bin") ||
        dio_safe_relative_path(L"versions\\COM1") ||
        dio_safe_relative_path(L"versions\\com9.dll") ||
        dio_safe_relative_path(L"versions\\LPT1") ||
        dio_safe_relative_path(L"versions\\lpt9.txt") ||
        !dio_parse_manifest(
            sample,
            strlen(sample),
            false,
            &manifest,
            error,
            _countof(error)) ||
        manifest.component_count != 1u ||
        wcscmp(manifest.components[0].id, L"core") != 0) {
        return 1;
    }
    wchar_t temporary[DIO_PATH_CAP];
    wchar_t staging[DIO_PATH_CAP];
    wchar_t staged_target[DIO_PATH_CAP];
    wchar_t staged_marker[DIO_PATH_CAP];
    wchar_t destination[DIO_PATH_CAP];
    wchar_t marker[DIO_PATH_CAP];
    DioBootstrap bootstrap;
    DioComponent component;
    ZeroMemory(&bootstrap, sizeof(bootstrap));
    ZeroMemory(&component, sizeof(component));
    if (GetTempPathW(_countof(temporary), temporary) == 0u ||
        swprintf_s(
            bootstrap.root,
            _countof(bootstrap.root),
            L"%lsdio-bootstrap-self-%lu",
            temporary,
            GetCurrentProcessId()) < 0 ||
        !dio_join_path(
            staging,
            _countof(staging),
            bootstrap.root,
            L"staging") ||
        !dio_join_path(
            staged_target,
            _countof(staged_target),
            staging,
            L"versions\\test") ||
        !dio_join_path(
            staged_marker,
            _countof(staged_marker),
            staged_target,
            L"new.txt") ||
        !dio_join_path(
            destination,
            _countof(destination),
            bootstrap.root,
            L".dio\\versions\\test") ||
        !dio_join_path(
            marker,
            _countof(marker),
            destination,
            L"old.txt")) {
        return 1;
    }
    (void)dio_delete_tree(bootstrap.root);
    (void)wcscpy_s(component.id, _countof(component.id), L"rollback");
    (void)wcscpy_s(
        component.target,
        _countof(component.target),
        L"versions\\test");
    if (!dio_ensure_directory(staging) ||
        !dio_write_file_atomic(marker, "old", 3u)) {
        (void)dio_delete_tree(bootstrap.root);
        return 1;
    }
    bootstrap.cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (bootstrap.cancel_event == NULL) {
        (void)dio_delete_tree(bootstrap.root);
        return 1;
    }
    const bool rejected = !dio_publish_component(
        &bootstrap,
        &component,
        staging,
        error,
        _countof(error));
    const bool restored = dio_file_exists(marker);
    if (!dio_write_file_atomic(staged_marker, "new", 3u) ||
        !SetEvent(bootstrap.cancel_event)) {
        CloseHandle(bootstrap.cancel_event);
        (void)dio_delete_tree(bootstrap.root);
        return 1;
    }
    const bool cancel_rejected = !dio_publish_component(
        &bootstrap,
        &component,
        staging,
        error,
        _countof(error));
    const bool cancel_preserved = dio_file_exists(marker) &&
        dio_file_exists(staged_marker) && dio_cancelled(&bootstrap);
    CloseHandle(bootstrap.cancel_event);
    const bool cleaned = dio_delete_tree(bootstrap.root);
    if (!rejected || !restored || !cancel_rejected ||
        !cancel_preserved || !cleaned) {
        return 1;
    }
    return 0;
}

static bool dio_parse_arguments(DioBootstrap *bootstrap, bool *self_test) {
    int count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == NULL) {
        return false;
    }
    bool valid = true;
    for (int index = 1; index < count && valid; ++index) {
        if (wcscmp(arguments[index], L"--repair") == 0) {
            bootstrap->force_repair = true;
        } else if (wcscmp(arguments[index], L"--install-only") == 0 &&
                   index + 1 < count) {
            const wchar_t *profile = arguments[++index];
            bootstrap->install_only = true;
            bootstrap->selected_profile = wcscmp(profile, L"small") == 0
                ? DIO_PROFILE_SMALL
                : wcscmp(profile, L"large") == 0
                    ? DIO_PROFILE_LARGE
                    : DIO_PROFILE_ALL;
            valid = bootstrap->selected_profile != DIO_PROFILE_ALL;
        } else if (wcscmp(arguments[index], L"--self-test") == 0) {
            *self_test = true;
        } else if (wcscmp(arguments[index], L"--allow-test-http") == 0) {
            bootstrap->allow_test_http = true;
        } else if (wcscmp(arguments[index], L"--manifest-file") == 0 &&
                   index + 1 < count) {
            wchar_t absolute[DIO_PATH_CAP];
            const DWORD length = GetFullPathNameW(
                arguments[++index],
                _countof(absolute),
                absolute,
                NULL);
            valid = length > 0u && length < _countof(absolute) &&
                dio_copy_wide(
                    bootstrap->manifest_file,
                    _countof(bootstrap->manifest_file),
                    absolute);
        } else {
            valid = false;
        }
    }
    if (bootstrap->install_only && bootstrap->manifest_file[0] == L'\0') {
        valid = false;
    }
    if (bootstrap->allow_test_http &&
        (!bootstrap->install_only || bootstrap->manifest_file[0] == L'\0')) {
        valid = false;
    }
    if (bootstrap->manifest_file[0] != L'\0' && !bootstrap->install_only) {
        valid = false;
    }
    LocalFree(arguments);
    return valid;
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previous,
    PWSTR command_line,
    int show) {
    (void)previous;
    (void)command_line;
    (void)show;
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    DioBootstrap bootstrap;
    ZeroMemory(&bootstrap, sizeof(bootstrap));
    bootstrap.instance = instance;
    bootstrap.cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (bootstrap.cancel_event == NULL ||
        !dio_get_application_root(bootstrap.root, _countof(bootstrap.root)) ||
        !dio_copy_wide(
            bootstrap.manifest_url,
            _countof(bootstrap.manifest_url),
            DIO_BOOTSTRAP_MANIFEST_URL) ||
        !dio_copy_wide(
            bootstrap.manifest_sha256,
            _countof(bootstrap.manifest_sha256),
            DIO_BOOTSTRAP_MANIFEST_SHA256)) {
        if (bootstrap.cancel_event != NULL) {
            CloseHandle(bootstrap.cancel_event);
        }
        return 2;
    }
    bool self_test = false;
    if (!dio_parse_arguments(&bootstrap, &self_test)) {
        MessageBoxW(
            NULL,
            L"پارامترهای Bootstrap معتبر نیستند.",
            L"DIO Voice",
            MB_OK | MB_ICONERROR);
        CloseHandle(bootstrap.cancel_event);
        return 2;
    }
    if (self_test) {
        CloseHandle(bootstrap.cancel_event);
        return dio_self_test();
    }
    if (bootstrap.install_only) {
        const DWORD result = dio_worker(&bootstrap);
        CloseHandle(bootstrap.cancel_event);
        return (int)result;
    }

    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&controls)) {
        CloseHandle(bootstrap.cancel_event);
        return 2;
    }
    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = dio_window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = DIO_BOOTSTRAP_CLASS;
    if (!RegisterClassExW(&window_class)) {
        CloseHandle(bootstrap.cancel_event);
        return 2;
    }
    const UINT initial_dpi = GetDpiForSystem();
    const int width = MulDiv(560, (int)initial_dpi, 96);
    const int height = MulDiv(500, (int)initial_dpi, 96);
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_CONTROLPARENT,
        DIO_BOOTSTRAP_CLASS,
        L"DIO Voice",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
            WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        NULL,
        NULL,
        instance,
        &bootstrap);
    if (window == NULL) {
        CloseHandle(bootstrap.cancel_event);
        return 2;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0u, 0u) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (bootstrap.worker != NULL) {
        SetEvent(bootstrap.cancel_event);
        WaitForSingleObject(bootstrap.worker, 10000u);
        CloseHandle(bootstrap.worker);
    }
    if (bootstrap.font != NULL) {
        DeleteObject(bootstrap.font);
    }
    if (bootstrap.title_font != NULL) {
        DeleteObject(bootstrap.title_font);
    }
    CloseHandle(bootstrap.cancel_event);
    return (int)message.wParam;
}
