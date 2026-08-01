#include "announcement.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

int wmain(void) {
    wchar_t temporary[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t input[MAX_PATH];
    wchar_t receipt[MAX_PATH];
    wchar_t text[256];
    DioAnnouncementInbox inbox;
    HANDLE file;
    bool taken = false;
    DWORD written = 0u;
    const char payload[] = "\xDB\x8C\xD8\xA7\xD8\xAF\xD8\xA2\xD9\x88\xD8\xB1\xDB\x8C\n";

    assert(GetTempPathW((DWORD)_countof(temporary), temporary) != 0u);
    assert(swprintf_s(
        directory,
        _countof(directory),
        L"%lsdio-voice-announcement-%lu",
        temporary,
        GetCurrentProcessId()) > 0);
    assert(CreateDirectoryW(directory, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS);
    assert(swprintf_s(
        input,
        _countof(input),
        L"%ls\\0000.txt.processing",
        directory) > 0);
    file = CreateFileW(
        input,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(
        file,
        payload,
        (DWORD)(sizeof(payload) - 1u),
        &written,
        NULL));
    assert(written == sizeof(payload) - 1u);
    CloseHandle(file);

    assert(dio_announcement_inbox_open(&inbox, directory));
    assert(dio_announcement_take(
        &inbox,
        text,
        _countof(text),
        receipt,
        _countof(receipt)));
    assert(wcscmp(text, L"\u06cc\u0627\u062f\u0622\u0648\u0631\u06cc") == 0);
    assert(GetFileAttributesW(receipt) != INVALID_FILE_ATTRIBUTES);
    dio_announcement_finish(receipt, true);
    assert(GetFileAttributesW(receipt) == INVALID_FILE_ATTRIBUTES);

    assert(swprintf_s(
        input,
        _countof(input),
        L"%ls\\0001.txt",
        directory) > 0);
    file = CreateFileW(
        input,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    assert(file != INVALID_HANDLE_VALUE);
    written = 0u;
    assert(WriteFile(
        file,
        payload,
        (DWORD)(sizeof(payload) - 1u),
        &written,
        NULL));
    assert(written == sizeof(payload) - 1u);
    CloseHandle(file);
    assert(dio_announcement_take(
        &inbox,
        text,
        _countof(text),
        receipt,
        _countof(receipt)));
    dio_announcement_finish(receipt, true);

    assert(!dio_announcement_schedule(directory, 1u, L" \t"));
    assert(!dio_announcement_schedule(
        directory,
        24u * 60u * 60u + 1u,
        L"too late"));
    assert(dio_announcement_schedule(
        directory,
        0u,
        L"timer done"));
    assert(dio_announcement_take(
        &inbox,
        text,
        _countof(text),
        receipt,
        _countof(receipt)));
    assert(wcscmp(text, L"timer done") == 0);
    dio_announcement_finish(receipt, true);
    assert(dio_announcement_schedule(
        directory,
        1u,
        L"\u063a\u0630\u0627 \u0631\u0627 "
        L"\u062e\u0627\u0645\u0648\u0634 \u06a9\u0646"));
    assert(dio_announcement_inbox_open(&inbox, directory));
    assert(!dio_announcement_take(
        &inbox,
        text,
        _countof(text),
        receipt,
        _countof(receipt)));
    const ULONGLONG deadline = GetTickCount64() + 3000u;
    do {
        Sleep(20u);
        taken = dio_announcement_take(
            &inbox,
            text,
            _countof(text),
            receipt,
            _countof(receipt));
    } while (!taken && GetTickCount64() < deadline);
    assert(taken);
    assert(wcscmp(
        text,
        L"\u063a\u0630\u0627 \u0631\u0627 "
        L"\u062e\u0627\u0645\u0648\u0634 \u06a9\u0646") == 0);
    dio_announcement_finish(receipt, true);
    assert(!dio_announcement_take(
        &inbox,
        text,
        _countof(text),
        receipt,
        _countof(receipt)));

    assert(RemoveDirectoryW(directory));
    return 0;
}
