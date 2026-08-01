#include "announcement.h"

#include <objbase.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

enum {
    DIO_ANNOUNCEMENT_MAX_BYTES = 64 * 1024,
    DIO_ANNOUNCEMENT_MAX_DELAY_SECONDS = 24 * 60 * 60
};

static bool dio_path(
    wchar_t *output,
    size_t capacity,
    const wchar_t *directory,
    const wchar_t *name) {
    int count = swprintf_s(output, capacity, L"%ls\\%ls", directory, name);
    return count > 0 && (size_t)count < capacity;
}

static bool dio_recover_processing(const wchar_t *directory) {
    static const wchar_t suffix[] = L".processing";
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    bool result = true;

    if (!dio_path(pattern, _countof(pattern), directory, L"*.txt.processing")) {
        return false;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    do {
        wchar_t source[MAX_PATH];
        wchar_t target[MAX_PATH];
        size_t length;

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ||
            !dio_path(source, _countof(source), directory, data.cFileName) ||
            wcsncpy_s(target, _countof(target), source, _TRUNCATE) != 0) {
            result = false;
            break;
        }
        length = wcslen(target);
        if (length <= _countof(suffix) - 1u) {
            result = false;
            break;
        }
        target[length - (_countof(suffix) - 1u)] = L'\0';
        if (!MoveFileExW(source, target, MOVEFILE_WRITE_THROUGH)) {
            result = false;
            break;
        }
    } while (FindNextFileW(find, &data));
    if (result && GetLastError() != ERROR_NO_MORE_FILES) {
        result = false;
    }
    FindClose(find);
    return result;
}

static bool dio_due_from_name(
    const wchar_t *name,
    uint64_t *due) {
    uint64_t value = 0u;
    size_t index;

    if (name == NULL || due == NULL) {
        return false;
    }
    for (index = 0u; index < 16u; ++index) {
        unsigned int digit;
        const wchar_t character = name[index];
        if (character >= L'0' && character <= L'9') {
            digit = (unsigned int)(character - L'0');
        } else if (character >= L'A' && character <= L'F') {
            digit = (unsigned int)(character - L'A') + 10u;
        } else if (character >= L'a' && character <= L'f') {
            digit = (unsigned int)(character - L'a') + 10u;
        } else {
            return false;
        }
        value = (value << 4u) | digit;
    }
    if (name[16] != L'-') {
        return false;
    }
    *due = value;
    return true;
}

static bool dio_release_due(
    const DioAnnouncementInbox *inbox) {
    static const wchar_t suffix[] = L".timer";
    wchar_t pattern[MAX_PATH];
    wchar_t selected[MAX_PATH] = L"";
    WIN32_FIND_DATAW data;
    FILETIME current_time;
    ULARGE_INTEGER current;
    HANDLE find;

    if (!dio_path(pattern, _countof(pattern), inbox->directory, L"*.timer")) {
        return false;
    }
    GetSystemTimePreciseAsFileTime(&current_time);
    current.LowPart = current_time.dwLowDateTime;
    current.HighPart = current_time.dwHighDateTime;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    do {
        uint64_t due;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u &&
            dio_due_from_name(data.cFileName, &due) &&
            due <= current.QuadPart &&
            (selected[0] == L'\0' ||
             _wcsicmp(data.cFileName, selected) < 0)) {
            (void)wcsncpy_s(
                selected,
                _countof(selected),
                data.cFileName,
                _TRUNCATE);
        }
    } while (FindNextFileW(find, &data));
    const DWORD find_error = GetLastError();
    FindClose(find);
    if (find_error != ERROR_NO_MORE_FILES) {
        return false;
    }
    if (selected[0] == L'\0') {
        return true;
    }

    wchar_t source[MAX_PATH];
    wchar_t target[MAX_PATH];
    size_t length;
    if (!dio_path(
            source,
            _countof(source),
            inbox->directory,
            selected) ||
        wcsncpy_s(target, _countof(target), source, _TRUNCATE) != 0) {
        return false;
    }
    length = wcslen(target);
    if (length <= _countof(suffix) - 1u) {
        return false;
    }
    target[length - (_countof(suffix) - 1u)] = L'\0';
    if (wcscat_s(target, _countof(target), L".txt") != 0) {
        return false;
    }
    return MoveFileExW(source, target, MOVEFILE_WRITE_THROUGH) != 0;
}

bool dio_announcement_inbox_open(
    DioAnnouncementInbox *inbox,
    const wchar_t *directory) {
    if (inbox == NULL || directory == NULL || directory[0] == L'\0') {
        return false;
    }
    ZeroMemory(inbox, sizeof(*inbox));
    if (wcsncpy_s(
            inbox->directory,
            _countof(inbox->directory),
            directory,
            _TRUNCATE) != 0) {
        return false;
    }
    if (!CreateDirectoryW(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    if (!dio_recover_processing(directory)) {
        return false;
    }
    return true;
}

bool dio_announcement_schedule(
    const wchar_t *directory,
    unsigned int delay_seconds,
    const wchar_t *text) {
    wchar_t guid_text[40];
    wchar_t name[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t temporary[MAX_PATH] = L"";
    FILETIME current_time;
    ULARGE_INTEGER due;
    GUID guid;
    HANDLE file = INVALID_HANDLE_VALUE;
    char *utf8 = NULL;
    int utf8_size;
    DWORD written = 0u;
    bool have_nonspace = false;
    bool result = false;

    if (directory == NULL ||
        directory[0] == L'\0' ||
        text == NULL ||
        text[0] == L'\0' ||
        delay_seconds > DIO_ANNOUNCEMENT_MAX_DELAY_SECONDS) {
        return false;
    }
    for (const wchar_t *cursor = text; *cursor != L'\0'; ++cursor) {
        if (!iswspace(*cursor)) {
            have_nonspace = true;
            break;
        }
    }
    utf8_size = have_nonspace
                    ? WideCharToMultiByte(
                          CP_UTF8,
                          WC_ERR_INVALID_CHARS,
                          text,
                          -1,
                          NULL,
                          0,
                          NULL,
                          NULL)
                    : 0;
    if (utf8_size <= 1 ||
        utf8_size - 1 > DIO_ANNOUNCEMENT_MAX_BYTES ||
        (!CreateDirectoryW(directory, NULL) &&
         GetLastError() != ERROR_ALREADY_EXISTS) ||
        FAILED(CoCreateGuid(&guid)) ||
        StringFromGUID2(&guid, guid_text, (int)_countof(guid_text)) == 0) {
        return false;
    }
    utf8 = (char *)malloc((size_t)utf8_size);
    if (utf8 == NULL ||
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            utf8,
            utf8_size,
            NULL,
            NULL) != utf8_size) {
        goto cleanup;
    }

    GetSystemTimePreciseAsFileTime(&current_time);
    due.LowPart = current_time.dwLowDateTime;
    due.HighPart = current_time.dwHighDateTime;
    due.QuadPart +=
        (uint64_t)delay_seconds * UINT64_C(10000000);
    if (swprintf_s(
            name,
            _countof(name),
            L"%016llX-%ls.timer",
            (unsigned long long)due.QuadPart,
            guid_text) <= 0 ||
        !dio_path(target, _countof(target), directory, name) ||
        swprintf_s(
            temporary,
            _countof(temporary),
            L"%ls.tmp",
            target) <= 0) {
        goto cleanup;
    }
    file = CreateFileW(
        temporary,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE ||
        !WriteFile(
            file,
            utf8,
            (DWORD)(utf8_size - 1),
            &written,
            NULL) ||
        written != (DWORD)(utf8_size - 1) ||
        !FlushFileBuffers(file)) {
        goto cleanup;
    }
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    result = MoveFileExW(
                 temporary,
                 target,
                 MOVEFILE_WRITE_THROUGH) != 0;

cleanup:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (!result && temporary[0] != L'\0') {
        (void)DeleteFileW(temporary);
    }
    free(utf8);
    return result;
}

static bool dio_next_file(
    const DioAnnouncementInbox *inbox,
    wchar_t *path,
    size_t path_capacity) {
    wchar_t pattern[MAX_PATH];
    wchar_t selected[MAX_PATH] = L"";
    WIN32_FIND_DATAW data;
    HANDLE find;

    if (!dio_path(pattern, _countof(pattern), inbox->directory, L"*.txt")) {
        return false;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u &&
            (selected[0] == L'\0' || _wcsicmp(data.cFileName, selected) < 0)) {
            (void)wcsncpy_s(selected, _countof(selected), data.cFileName, _TRUNCATE);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return selected[0] != L'\0' &&
        dio_path(path, path_capacity, inbox->directory, selected);
}

static bool dio_read_utf8(
    const wchar_t *path,
    wchar_t *text,
    size_t text_capacity) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0u;
    char *bytes = NULL;
    int converted;
    bool result = false;

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
        size.QuadPart <= 0 ||
        size.QuadPart > DIO_ANNOUNCEMENT_MAX_BYTES) {
        goto cleanup;
    }
    bytes = (char *)malloc((size_t)size.QuadPart + 1u);
    if (bytes == NULL ||
        !ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL) ||
        read != (DWORD)size.QuadPart ||
        memchr(bytes, '\0', read) != NULL) {
        goto cleanup;
    }
    bytes[read] = '\0';
    converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes,
        (int)read,
        text,
        (int)(text_capacity - 1u));
    if (converted <= 0) {
        goto cleanup;
    }
    text[converted] = L'\0';
    while (converted > 0 &&
           (text[converted - 1] == L'\r' ||
            text[converted - 1] == L'\n' ||
            text[converted - 1] == L' ' ||
            text[converted - 1] == L'\t')) {
        text[--converted] = L'\0';
    }
    result = converted > 0;

cleanup:
    free(bytes);
    CloseHandle(file);
    return result;
}

bool dio_announcement_take(
    DioAnnouncementInbox *inbox,
    wchar_t *text,
    size_t text_capacity,
    wchar_t *receipt,
    size_t receipt_capacity) {
    wchar_t source[MAX_PATH];
    wchar_t processing[MAX_PATH];

    if (inbox == NULL ||
        text == NULL ||
        text_capacity < 2u ||
        receipt == NULL ||
        receipt_capacity < 2u ||
        !dio_release_due(inbox) ||
        !dio_next_file(inbox, source, _countof(source)) ||
        swprintf_s(
            processing,
            _countof(processing),
            L"%ls.processing",
            source) < 0 ||
        !MoveFileExW(source, processing, MOVEFILE_WRITE_THROUGH) ||
        wcsncpy_s(receipt, receipt_capacity, processing, _TRUNCATE) != 0) {
        return false;
    }
    if (!dio_read_utf8(processing, text, text_capacity)) {
        dio_announcement_finish(processing, false);
        return false;
    }
    return true;
}

void dio_announcement_finish(
    const wchar_t *receipt,
    bool spoken) {
    wchar_t failed[MAX_PATH];
    if (receipt == NULL || receipt[0] == L'\0') {
        return;
    }
    if (spoken) {
        (void)DeleteFileW(receipt);
        return;
    }
    if (swprintf_s(failed, _countof(failed), L"%ls.failed", receipt) > 0) {
        (void)MoveFileExW(
            receipt,
            failed,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
}
