#include <winsock2.h>

#include "tts.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <iphlpapi.h>
#include <winhttp.h>

#include "miniaudio_config.h"
#include "pcm_stream.h"
#include "yyjson.h"

#define DIO_PIPER_PORT 18767u
#define DIO_PIPER_SAMPLE_RATE 22050u
#define DIO_PIPER_MAX_PCM_BYTES \
    ((size_t)DIO_PIPER_SAMPLE_RATE * 120u * sizeof(int16_t))
#define DIO_PIPER_START_TIMEOUT_MS 120000u
#define DIO_PIPER_SYNTHESIS_TIMEOUT_MS 5000u

struct DioTtsServer {
    HANDLE job;
    HANDLE process;
    HANDLE startup_thread;
    volatile LONG ready;
    volatile LONG closing;
    volatile LONG request_active;
    volatile LONG restart_required;
    wchar_t bundle_directory[MAX_PATH];
    wchar_t state_directory[MAX_PATH];
};

typedef struct DioTtsBytes {
    unsigned char *data;
    size_t size;
    size_t capacity;
} DioTtsBytes;

static void dio_tts_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    (void)strncpy_s(destination, capacity, message, _TRUNCATE);
}

static bool dio_tts_is_cancelled(HANDLE cancel_event, HANDLE stop_event)
{
    return
        (cancel_event != NULL &&
         WaitForSingleObject(cancel_event, 0u) == WAIT_OBJECT_0) ||
        (stop_event != NULL &&
         WaitForSingleObject(stop_event, 0u) == WAIT_OBJECT_0);
}

static size_t dio_tts_quoted_length(const wchar_t *argument)
{
    size_t length = 2u;
    size_t slashes = 0u;
    size_t index;

    for (index = 0u; argument[index] != L'\0'; ++index) {
        if (argument[index] == L'\\') {
            ++slashes;
        } else if (argument[index] == L'"') {
            length += (slashes * 2u) + 2u;
            slashes = 0u;
        } else {
            length += slashes + 1u;
            slashes = 0u;
        }
    }
    return length + (slashes * 2u);
}

static wchar_t *dio_tts_command_line(
    const wchar_t *const *arguments,
    size_t argument_count)
{
    size_t capacity = 1u;
    size_t argument_index;
    wchar_t *command;
    size_t position = 0u;

    for (argument_index = 0u;
         argument_index < argument_count;
         ++argument_index) {
        const size_t quoted = dio_tts_quoted_length(arguments[argument_index]);
        if (capacity > SIZE_MAX - quoted - 1u) {
            return NULL;
        }
        capacity += quoted + 1u;
    }
    if (capacity > SIZE_MAX / sizeof(*command)) {
        return NULL;
    }
    command = (wchar_t *)malloc(capacity * sizeof(*command));
    if (command == NULL) {
        return NULL;
    }

    for (argument_index = 0u;
         argument_index < argument_count;
         ++argument_index) {
        const wchar_t *argument = arguments[argument_index];
        size_t slashes = 0u;
        size_t index;

        if (argument_index != 0u) {
            command[position++] = L' ';
        }
        command[position++] = L'"';
        for (index = 0u; argument[index] != L'\0'; ++index) {
            if (argument[index] == L'\\') {
                ++slashes;
                continue;
            }
            if (argument[index] == L'"') {
                size_t count;
                for (count = 0u; count < (slashes * 2u) + 1u; ++count) {
                    command[position++] = L'\\';
                }
                command[position++] = L'"';
            } else {
                while (slashes != 0u) {
                    command[position++] = L'\\';
                    --slashes;
                }
                command[position++] = argument[index];
            }
            slashes = 0u;
        }
        while (slashes != 0u) {
            command[position++] = L'\\';
            command[position++] = L'\\';
            --slashes;
        }
        command[position++] = L'"';
    }
    command[position] = L'\0';
    return command;
}

static bool dio_tts_join(
    wchar_t *output,
    size_t output_capacity,
    const wchar_t *directory,
    const wchar_t *relative)
{
    return
        output != NULL &&
        output_capacity != 0u &&
        directory != NULL &&
        directory[0] != L'\0' &&
        relative != NULL &&
        relative[0] != L'\0' &&
        _snwprintf_s(
            output,
            output_capacity,
            _TRUNCATE,
            L"%ls\\%ls",
            directory,
            relative) >= 0;
}

static bool dio_tts_directory(const wchar_t *path)
{
    DWORD attributes;

    if (CreateDirectoryW(path, NULL)) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    attributes = GetFileAttributesW(path);
    return
        attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
}

static bool dio_tts_environment_name(
    const wchar_t *entry,
    const wchar_t *name)
{
    const size_t length = wcslen(name);
    return
        _wcsnicmp(entry, name, length) == 0 &&
        entry[length] == L'=';
}

static wchar_t *dio_tts_environment_entry(
    const wchar_t *name,
    const wchar_t *value)
{
    const size_t name_length = wcslen(name);
    const size_t value_length = wcslen(value);
    wchar_t *entry;

    if (name_length > SIZE_MAX - value_length - 2u) {
        return NULL;
    }
    entry = (wchar_t *)malloc(
        (name_length + value_length + 2u) * sizeof(*entry));
    if (entry != NULL) {
        (void)swprintf_s(
            entry,
            name_length + value_length + 2u,
            L"%ls=%ls",
            name,
            value);
    }
    return entry;
}

static int __cdecl dio_tts_environment_compare(
    const void *left,
    const void *right)
{
    return _wcsicmp(
        *(const wchar_t *const *)left,
        *(const wchar_t *const *)right);
}

static wchar_t *dio_tts_environment_block(
    const wchar_t *temporary)
{
    static const wchar_t *const names[] = {
        L"PYTHONUTF8",
        L"PYTHONUNBUFFERED",
        L"TEMP",
        L"TMP"};
    static const wchar_t *const fixed_values[] = {
        L"1",
        L"1"};
    LPWCH inherited = GetEnvironmentStringsW();
    const wchar_t *cursor;
    wchar_t **entries = NULL;
    size_t inherited_count = 0u;
    size_t count = 0u;
    size_t total = 1u;
    wchar_t *block = NULL;

    if (inherited == NULL) {
        return NULL;
    }
    for (cursor = inherited;
         *cursor != L'\0';
         cursor += wcslen(cursor) + 1u) {
        ++inherited_count;
    }
    entries = (wchar_t **)calloc(
        inherited_count + _countof(names),
        sizeof(*entries));
    if (entries == NULL) {
        (void)FreeEnvironmentStringsW(inherited);
        return NULL;
    }
    for (cursor = inherited;
         *cursor != L'\0';
         cursor += wcslen(cursor) + 1u) {
        bool replaced = false;
        for (size_t index = 0u;
             index < _countof(names);
             ++index) {
            if (dio_tts_environment_name(cursor, names[index])) {
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            entries[count] = _wcsdup(cursor);
            if (entries[count] == NULL) {
                goto cleanup;
            }
            ++count;
        }
    }
    for (size_t index = 0u;
         index < _countof(names);
         ++index) {
        entries[count] = dio_tts_environment_entry(
            names[index],
            index < _countof(fixed_values)
                ? fixed_values[index]
                : temporary);
        if (entries[count] == NULL) {
            goto cleanup;
        }
        ++count;
    }
    qsort(
        entries,
        count,
        sizeof(*entries),
        dio_tts_environment_compare);
    for (size_t index = 0u; index < count; ++index) {
        const size_t length = wcslen(entries[index]) + 1u;
        if (total > SIZE_MAX - length) {
            goto cleanup;
        }
        total += length;
    }
    if (total > SIZE_MAX / sizeof(*block)) {
        goto cleanup;
    }
    block = (wchar_t *)calloc(total, sizeof(*block));
    if (block != NULL) {
        size_t offset = 0u;
        for (size_t index = 0u; index < count; ++index) {
            const size_t length = wcslen(entries[index]) + 1u;
            (void)memcpy(
                block + offset,
                entries[index],
                length * sizeof(*block));
            offset += length;
        }
    }

cleanup:
    for (size_t index = 0u; index < count; ++index) {
        free(entries[index]);
    }
    free(entries);
    (void)FreeEnvironmentStringsW(inherited);
    return block;
}

static bool dio_tts_bytes_append(
    DioTtsBytes *bytes,
    const void *input,
    size_t input_size)
{
    size_t required;
    size_t capacity;
    unsigned char *expanded;

    if (bytes == NULL ||
        (input == NULL && input_size != 0u) ||
        bytes->size > DIO_PIPER_MAX_PCM_BYTES ||
        input_size > DIO_PIPER_MAX_PCM_BYTES - bytes->size) {
        return false;
    }
    required = bytes->size + input_size;
    if (required > bytes->capacity) {
        capacity = bytes->capacity == 0u ? 65536u : bytes->capacity;
        while (capacity < required) {
            if (capacity > DIO_PIPER_MAX_PCM_BYTES / 2u) {
                capacity = DIO_PIPER_MAX_PCM_BYTES;
                break;
            }
            capacity *= 2u;
        }
        expanded = (unsigned char *)realloc(bytes->data, capacity);
        if (expanded == NULL) {
            return false;
        }
        bytes->data = expanded;
        bytes->capacity = capacity;
    }
    if (input_size != 0u) {
        (void)memcpy(bytes->data + bytes->size, input, input_size);
        bytes->size = required;
    }
    return true;
}

static char *dio_tts_piper_json(
    const char *text,
    size_t text_length,
    size_t *json_size)
{
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root =
        document != NULL ? yyjson_mut_obj(document) : NULL;
    char *json = NULL;

    if (document != NULL) {
        yyjson_mut_doc_set_root(document, root);
    }
    if (text != NULL &&
        text_length != 0u &&
        root != NULL &&
        yyjson_mut_obj_add_strncpy(
            document,
            root,
            "input",
            text,
            text_length)) {
        json = yyjson_mut_write(
            document,
            YYJSON_WRITE_NOFLAG,
            json_size);
    }
    yyjson_mut_doc_free(document);
    return json;
}

static void dio_tts_http_close(
    HINTERNET request,
    HINTERNET connection,
    HINTERNET session)
{
    if (request != NULL) {
        (void)WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        (void)WinHttpCloseHandle(connection);
    }
    if (session != NULL) {
        (void)WinHttpCloseHandle(session);
    }
}

static DWORD dio_tts_piper_listener_pid(void)
{
    const DWORD network_port =
        ((DIO_PIPER_PORT & 0xffu) << 8u) |
        ((DIO_PIPER_PORT >> 8u) & 0xffu);
    ULONG bytes = 0u;
    MIB_TCPTABLE_OWNER_PID *table = NULL;
    DWORD owner = 0u;

    if (GetExtendedTcpTable(
            NULL,
            &bytes,
            FALSE,
            AF_INET,
            TCP_TABLE_OWNER_PID_LISTENER,
            0u) != ERROR_INSUFFICIENT_BUFFER ||
        bytes == 0u) {
        return 0u;
    }
    table = (MIB_TCPTABLE_OWNER_PID *)malloc(bytes);
    if (table == NULL ||
        GetExtendedTcpTable(
            table,
            &bytes,
            FALSE,
            AF_INET,
            TCP_TABLE_OWNER_PID_LISTENER,
            0u) != NO_ERROR) {
        free(table);
        return 0u;
    }
    for (DWORD index = 0u;
         index < table->dwNumEntries;
         ++index) {
        const MIB_TCPROW_OWNER_PID *row = &table->table[index];
        if (row->dwLocalPort == network_port &&
            row->dwLocalAddr == 0x0100007fu) {
            owner = row->dwOwningPid;
            break;
        }
    }
    free(table);
    return owner;
}

static bool dio_tts_piper_health(void)
{
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    DWORD status = 0u;
    DWORD status_size = sizeof(status);
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    bool healthy = false;

    session = WinHttpOpen(
        L"DIO Voice/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0u);
    if (session == NULL ||
        !WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000)) {
        goto cleanup;
    }
    connection = WinHttpConnect(
        session,
        L"127.0.0.1",
        (INTERNET_PORT)DIO_PIPER_PORT,
        0u);
    if (connection == NULL) {
        goto cleanup;
    }
    request = WinHttpOpenRequest(
        connection,
        L"GET",
        L"/health",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0u);
    if (request == NULL ||
        !WinHttpSetOption(
            request,
            WINHTTP_OPTION_DISABLE_FEATURE,
            &disabled,
            sizeof(disabled)) ||
        !WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0u,
            WINHTTP_NO_REQUEST_DATA,
            0u,
            0u,
            0u) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        goto cleanup;
    }
    healthy = status == 200u;

cleanup:
    dio_tts_http_close(request, connection, session);
    return healthy;
}

static DioTtsResult dio_tts_piper_post(
    DioTtsServer *server,
    const char *json,
    size_t json_size,
    unsigned int timeout_ms,
    HANDLE cancel_event,
    HANDLE stop_event,
    int16_t **samples,
    size_t *sample_count,
    char *error_text,
    size_t error_text_capacity)
{
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    DioTtsBytes pcm = {0};
    wchar_t content_type[64] = L"";
    DWORD content_type_size = sizeof(content_type);
    DWORD status = 0u;
    DWORD status_size = sizeof(status);
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    bool request_owned = false;
    DioTtsResult result = DIO_TTS_FAILED;

    if (server == NULL ||
        json == NULL ||
        json_size == 0u ||
        json_size > MAXDWORD ||
        samples == NULL ||
        sample_count == NULL ||
        InterlockedCompareExchange(&server->closing, 0, 0) != 0) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper is not available");
        return DIO_TTS_FAILED;
    }
    *samples = NULL;
    *sample_count = 0u;
    if (dio_tts_is_cancelled(cancel_event, stop_event)) {
        return DIO_TTS_CANCELLED;
    }

    session = WinHttpOpen(
        L"DIO Voice/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0u);
    if (session == NULL ||
        !WinHttpSetTimeouts(
            session,
            (int)timeout_ms,
            (int)timeout_ms,
            (int)timeout_ms,
            (int)timeout_ms)) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not open Piper HTTP session");
        goto cleanup;
    }
    connection = WinHttpConnect(
        session,
        L"127.0.0.1",
        (INTERNET_PORT)DIO_PIPER_PORT,
        0u);
    request =
        connection != NULL
            ? WinHttpOpenRequest(
                  connection,
                  L"POST",
                  L"/v1/audio/speech",
                  NULL,
                  WINHTTP_NO_REFERER,
                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                  0u)
            : NULL;
    if (connection == NULL || request == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not create Piper request");
        goto cleanup;
    }

    if (InterlockedCompareExchange(
            &server->request_active,
            1,
            0) != 0) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper is busy");
        goto cleanup;
    }
    request_owned = true;
    if (dio_tts_is_cancelled(cancel_event, stop_event)) {
        result = DIO_TTS_CANCELLED;
        goto cleanup;
    }
    if (!WinHttpSetOption(
            request,
            WINHTTP_OPTION_DISABLE_FEATURE,
            &disabled,
            sizeof(disabled)) ||
        !WinHttpSendRequest(
            request,
            L"Content-Type: application/json\r\n",
            (DWORD)-1L,
            (LPVOID)json,
            (DWORD)json_size,
            (DWORD)json_size,
            0u) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        !WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CONTENT_TYPE,
            WINHTTP_HEADER_NAME_BY_INDEX,
            content_type,
            &content_type_size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 200u ||
        _wcsnicmp(content_type, L"audio/pcm", 9u) != 0) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper synthesis failed");
        goto cleanup;
    }

    for (;;) {
        unsigned char buffer[65536];
        DWORD available = 0u;
        DWORD read = 0u;
        DWORD requested;

        if (dio_tts_is_cancelled(cancel_event, stop_event)) {
            result = DIO_TTS_CANCELLED;
            goto cleanup;
        }
        if (!WinHttpQueryDataAvailable(request, &available)) {
            dio_tts_error(
                error_text,
                error_text_capacity,
                "could not read Piper audio");
            goto cleanup;
        }
        if (available == 0u) {
            break;
        }
        requested =
            available < sizeof(buffer)
                ? available
                : (DWORD)sizeof(buffer);
        if (!WinHttpReadData(
                request,
                buffer,
                requested,
                &read) ||
            read == 0u ||
            !dio_tts_bytes_append(&pcm, buffer, read)) {
            dio_tts_error(
                error_text,
                error_text_capacity,
                "Piper returned invalid audio");
            goto cleanup;
        }
    }
    if (pcm.size == 0u ||
        (pcm.size & 1u) != 0u) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper returned invalid PCM");
        goto cleanup;
    }
    *samples = (int16_t *)pcm.data;
    *sample_count = pcm.size / sizeof(int16_t);
    pcm.data = NULL;
    result = DIO_TTS_SUCCEEDED;

cleanup:
    if (dio_tts_is_cancelled(cancel_event, stop_event)) {
        result = DIO_TTS_CANCELLED;
    }
    free(pcm.data);
    dio_tts_http_close(request, connection, session);
    if (request_owned) {
        (void)InterlockedExchange(&server->request_active, 0);
    }
    return result;
}

void dio_tts_server_cancel(DioTtsServer *server)
{
    if (server == NULL ||
        InterlockedCompareExchange(
            &server->request_active,
            0,
            0) == 0 ||
        InterlockedCompareExchange(
            &server->ready,
            0,
            0) == 0) {
        return;
    }
    (void)InterlockedExchange(&server->ready, 0);
    if (server->job != NULL) {
        (void)TerminateJobObject(server->job, ERROR_CANCELLED);
    } else if (server->process != NULL) {
        (void)TerminateProcess(server->process, ERROR_CANCELLED);
    }
    (void)InterlockedExchange(&server->restart_required, 1);
}

static void dio_tts_server_stop_process(DioTtsServer *server)
{
    (void)InterlockedExchange(&server->ready, 0);
    if (server->job != NULL) {
        (void)TerminateJobObject(server->job, 0u);
    } else if (server->process != NULL) {
        (void)TerminateProcess(server->process, 0u);
    }
    if (server->startup_thread != NULL) {
        (void)WaitForSingleObject(
            server->startup_thread,
            INFINITE);
        (void)CloseHandle(server->startup_thread);
        server->startup_thread = NULL;
    }
    if (server->process != NULL) {
        (void)WaitForSingleObject(server->process, 5000u);
        (void)CloseHandle(server->process);
        server->process = NULL;
    }
    if (server->job != NULL) {
        (void)CloseHandle(server->job);
        server->job = NULL;
    }
}

void dio_tts_server_close(DioTtsServer *server)
{
    if (server == NULL) {
        return;
    }
    (void)InterlockedExchange(&server->closing, 1);
    dio_tts_server_stop_process(server);
    free(server);
}

static DWORD WINAPI dio_tts_server_startup(void *context)
{
    static const char warmup[] =
        "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85\xD8\x8C "
        "\xD8\xA2\xD9\x85\xD8\xA7\xD8\xAF\xD9\x87\xE2\x80\x8C"
        "\xD8\xA7\xD9\x85.";
    DioTtsServer *server = (DioTtsServer *)context;
    const DWORD process_id = GetProcessId(server->process);
    const ULONGLONG deadline =
        GetTickCount64() + DIO_PIPER_START_TIMEOUT_MS;
    size_t json_size = 0u;
    char *json = NULL;
    int16_t *samples = NULL;
    size_t sample_count = 0u;
    bool ready = false;

    while (InterlockedCompareExchange(
               &server->closing,
               0,
               0) == 0) {
        if (WaitForSingleObject(
                server->process,
                0u) == WAIT_OBJECT_0 ||
            GetTickCount64() >= deadline) {
            goto cleanup;
        }
        if (dio_tts_piper_listener_pid() == process_id &&
            dio_tts_piper_health()) {
            break;
        }
        Sleep(250u);
    }
    if (InterlockedCompareExchange(
            &server->closing,
            0,
            0) != 0) {
        goto cleanup;
    }
    json = dio_tts_piper_json(
        warmup,
        sizeof(warmup) - 1u,
        &json_size);
    if (json != NULL &&
        dio_tts_piper_post(
            server,
            json,
            json_size,
            DIO_PIPER_SYNTHESIS_TIMEOUT_MS,
            NULL,
            NULL,
            &samples,
            &sample_count,
            NULL,
            0u) == DIO_TTS_SUCCEEDED &&
        sample_count != 0u &&
        InterlockedCompareExchange(
            &server->closing,
            0,
            0) == 0 &&
        WaitForSingleObject(
            server->process,
            0u) == WAIT_TIMEOUT &&
        dio_tts_piper_listener_pid() == process_id) {
        (void)InterlockedExchange(&server->ready, 1);
        ready = true;
    }

cleanup:
    free(samples);
    if (json != NULL) {
        SecureZeroMemory(json, json_size);
        free(json);
    }
    if (!ready &&
        InterlockedCompareExchange(
            &server->closing,
            0,
            0) == 0) {
        if (server->job != NULL) {
            (void)TerminateJobObject(
                server->job,
                ERROR_NOT_READY);
        } else if (server->process != NULL) {
            (void)TerminateProcess(
                server->process,
                ERROR_NOT_READY);
        }
        (void)InterlockedExchange(
            &server->restart_required,
            1);
    }
    return 0u;
}

static bool dio_tts_server_launch(
    DioTtsServer *server,
    char *error_text,
    size_t error_text_capacity)
{
    const wchar_t *bundle_directory = server->bundle_directory;
    const wchar_t *state_directory = server->state_directory;
    wchar_t executable[MAX_PATH];
    wchar_t server_script[MAX_PATH];
    wchar_t model[MAX_PATH];
    wchar_t model_config[MAX_PATH];
    wchar_t temporary[MAX_PATH];
    wchar_t stdout_path[MAX_PATH];
    wchar_t stderr_path[MAX_PATH];
    wchar_t *command = NULL;
    wchar_t *environment = NULL;
    SECURITY_ATTRIBUTES security = {
        sizeof(security),
        NULL,
        TRUE};
    HANDLE null_file = INVALID_HANDLE_VALUE;
    HANDLE stdout_file = INVALID_HANDLE_VALUE;
    HANDLE stderr_file = INVALID_HANDLE_VALUE;
    HANDLE inherited_handles[3];
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION process;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    SIZE_T attribute_bytes = 0u;
    bool attributes_ready = false;
    bool opened = false;

    (void)memset(&limits, 0, sizeof(limits));
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&process, 0, sizeof(process));
    startup.StartupInfo.cb = sizeof(startup);
    if (bundle_directory == NULL ||
        state_directory == NULL ||
        !dio_tts_join(
            executable,
            _countof(executable),
            bundle_directory,
            L"python\\python.exe") ||
        !dio_tts_join(
            server_script,
            _countof(server_script),
            bundle_directory,
            L"app\\server.py") ||
        !dio_tts_join(
            model,
            _countof(model),
            bundle_directory,
            L"models\\mana\\fa_IR-mana-medium.onnx") ||
        !dio_tts_join(
            model_config,
            _countof(model_config),
            bundle_directory,
            L"models\\mana\\fa_IR-mana-medium.onnx.json") ||
        !dio_tts_directory(state_directory) ||
        !dio_tts_join(
            temporary,
            _countof(temporary),
            state_directory,
            L"tmp") ||
        !dio_tts_directory(temporary) ||
        !dio_tts_join(
            stdout_path,
            _countof(stdout_path),
            temporary,
            L"server.stdout.log") ||
        !dio_tts_join(
            stderr_path,
            _countof(stderr_path),
            temporary,
            L"server.stderr.log")) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper paths are invalid");
        goto cleanup;
    }
    if (GetFileAttributesW(executable) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(server_script) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(model) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(model_config) == INVALID_FILE_ATTRIBUTES) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper bundle is incomplete");
        goto cleanup;
    }
    if (dio_tts_piper_listener_pid() != 0u) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper port is owned by another process");
        goto cleanup;
    }

    {
        const wchar_t *arguments[] = {
            executable,
            L"-I",
            L"-X",
            L"utf8",
            server_script,
            L"--host",
            L"127.0.0.1",
            L"--port",
            L"18767",
            L"--model",
            model};
        command = dio_tts_command_line(
            arguments,
            _countof(arguments));
    }
    if (command == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not build Piper command");
        goto cleanup;
    }
    null_file = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    stdout_file = CreateFileW(
        stdout_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    stderr_file = CreateFileW(
        stderr_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (null_file == INVALID_HANDLE_VALUE ||
        stdout_file == INVALID_HANDLE_VALUE ||
        stderr_file == INVALID_HANDLE_VALUE) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not open Piper logs");
        goto cleanup;
    }
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_file;
    startup.StartupInfo.hStdOutput = stdout_file;
    startup.StartupInfo.hStdError = stderr_file;
    inherited_handles[0] = null_file;
    inherited_handles[1] = stdout_file;
    inherited_handles[2] = stderr_file;
    (void)InitializeProcThreadAttributeList(
        NULL,
        1u,
        0u,
        &attribute_bytes);
    if (attribute_bytes == 0u) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not size Piper handle list");
        goto cleanup;
    }
    attributes =
        (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attribute_bytes);
    if (attributes == NULL ||
        !InitializeProcThreadAttributeList(
            attributes,
            1u,
            0u,
            &attribute_bytes)) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not initialize Piper handle list");
        goto cleanup;
    }
    attributes_ready = true;
    if (!UpdateProcThreadAttribute(
            attributes,
            0u,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited_handles,
            sizeof(inherited_handles),
            NULL,
            NULL)) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not configure Piper handles");
        goto cleanup;
    }
    startup.lpAttributeList = attributes;

    server->job = CreateJobObjectW(NULL, NULL);
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (server->job == NULL ||
        !SetInformationJobObject(
            server->job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not create Piper job");
        goto cleanup;
    }
    environment = dio_tts_environment_block(temporary);
    if (environment == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not build Piper environment");
        goto cleanup;
    }
    if (!CreateProcessW(
            executable,
            command,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW |
                CREATE_SUSPENDED |
                CREATE_UNICODE_ENVIRONMENT |
                EXTENDED_STARTUPINFO_PRESENT,
            environment,
            bundle_directory,
            &startup.StartupInfo,
            &process)) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not launch Piper");
        goto cleanup;
    }
    server->process = process.hProcess;
    process.hProcess = NULL;
    if (!AssignProcessToJobObject(
            server->job,
            server->process)) {
        (void)TerminateProcess(
            server->process,
            ERROR_ACCESS_DENIED);
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not contain Piper");
        goto cleanup;
    }
    if (ResumeThread(process.hThread) == (DWORD)-1) {
        (void)TerminateJobObject(
            server->job,
            ERROR_PROCESS_ABORTED);
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not start Piper");
        goto cleanup;
    }
    server->startup_thread = CreateThread(
        NULL,
        0u,
        dio_tts_server_startup,
        server,
        0u,
        NULL);
    if (server->startup_thread == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not warm Piper");
        goto cleanup;
    }
    opened = true;

cleanup:
    if (attributes_ready) {
        DeleteProcThreadAttributeList(attributes);
    }
    free(attributes);
    if (process.hThread != NULL) {
        (void)CloseHandle(process.hThread);
    }
    if (process.hProcess != NULL) {
        (void)CloseHandle(process.hProcess);
    }
    if (stderr_file != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(stderr_file);
    }
    if (stdout_file != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(stdout_file);
    }
    if (null_file != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(null_file);
    }
    free(environment);
    free(command);
    if (!opened) {
        dio_tts_server_stop_process(server);
    }
    return opened;
}

DioTtsServer *dio_tts_server_open(
    const wchar_t *bundle_directory,
    const wchar_t *state_directory,
    char *error_text,
    size_t error_text_capacity)
{
    DioTtsServer *server;

    if (bundle_directory == NULL ||
        state_directory == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper paths are invalid");
        return NULL;
    }
    server = (DioTtsServer *)calloc(1u, sizeof(*server));
    if (server == NULL ||
        wcscpy_s(
            server->bundle_directory,
            _countof(server->bundle_directory),
            bundle_directory) != 0 ||
        wcscpy_s(
            server->state_directory,
            _countof(server->state_directory),
            state_directory) != 0 ||
        !dio_tts_server_launch(
            server,
            error_text,
            error_text_capacity)) {
        dio_tts_server_close(server);
        return NULL;
    }
    return server;
}

static void dio_tts_server_restart(
    DioTtsServer *server)
{
    if (server == NULL ||
        InterlockedCompareExchange(
            &server->closing,
            0,
            0) != 0) {
        return;
    }
    dio_tts_server_stop_process(server);
    (void)InterlockedExchange(
        &server->restart_required,
        0);
    if (!dio_tts_server_launch(server, NULL, 0u)) {
        (void)InterlockedExchange(
            &server->restart_required,
            1);
    }
}

static DioTtsResult dio_tts_speak_piper(
    DioTtsServer *server,
    const char *utf8_text,
    size_t text_length,
    HANDLE cancel_event,
    HANDLE stop_event,
    DioTtsStartedCallback started_callback,
    void *started_context,
    char *error_text,
    size_t error_text_capacity)
{
    size_t json_size = 0u;
    char *json = NULL;
    int16_t *samples = NULL;
    size_t sample_count = 0u;
    DioPcmStream *stream = NULL;
    DioTtsResult result = DIO_TTS_FAILED;

    if (server == NULL ||
        InterlockedCompareExchange(&server->ready, 0, 0) == 0 ||
        server->process == NULL ||
        WaitForSingleObject(server->process, 0u) != WAIT_TIMEOUT) {
        if (server != NULL &&
            InterlockedCompareExchange(
                &server->ready,
                0,
                0) != 0) {
            (void)InterlockedExchange(&server->ready, 0);
            (void)InterlockedExchange(
                &server->restart_required,
                1);
        }
        dio_tts_error(
            error_text,
            error_text_capacity,
            "Piper is not available");
        return DIO_TTS_FAILED;
    }
    json = dio_tts_piper_json(
        utf8_text,
        text_length,
        &json_size);
    if (json == NULL) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not prepare Piper request");
        goto cleanup;
    }
    result = dio_tts_piper_post(
        server,
        json,
        json_size,
        DIO_PIPER_SYNTHESIS_TIMEOUT_MS,
        cancel_event,
        stop_event,
        &samples,
        &sample_count,
        error_text,
        error_text_capacity);
    if (result == DIO_TTS_FAILED) {
        (void)InterlockedExchange(&server->ready, 0);
        (void)InterlockedExchange(
            &server->restart_required,
            1);
        if (server->job != NULL) {
            (void)TerminateJobObject(
                server->job,
                ERROR_NOT_READY);
        }
    }
    if (result != DIO_TTS_SUCCEEDED) {
        goto cleanup;
    }
    stream = dio_pcm_stream_create(
        DIO_PIPER_SAMPLE_RATE);
    if (stream == NULL ||
        !dio_pcm_stream_write(
            stream,
            samples,
            sample_count) ||
        !dio_pcm_stream_finish(stream)) {
        dio_tts_error(
            error_text,
            error_text_capacity,
            "could not prepare Piper playback");
        result = DIO_TTS_FAILED;
        goto cleanup;
    }
    result = dio_pcm_stream_play(
        stream,
        false,
        cancel_event,
        stop_event,
        started_callback,
        started_context,
        error_text,
        error_text_capacity);

cleanup:
    dio_pcm_stream_destroy(stream);
    free(samples);
    if (json != NULL) {
        SecureZeroMemory(json, json_size);
        free(json);
    }
    return result;
}

DioTtsResult dio_tts_speak_text(
    const DioTtsConfig *config,
    const char *utf8_text,
    size_t text_length,
    HANDLE cancel_event,
    HANDLE stop_event,
    DioTtsStartedCallback started_callback,
    void *started_context,
    char *error_text,
    size_t error_text_capacity)
{
    DioTtsResult result;

    if (config == NULL ||
        config->server == NULL ||
        utf8_text == NULL ||
        text_length == 0u) {
        dio_tts_error(error_text, error_text_capacity, "TTS is not configured");
        return DIO_TTS_FAILED;
    }
    result = dio_tts_speak_piper(
        config->server,
        utf8_text,
        text_length,
        cancel_event,
        stop_event,
        started_callback,
        started_context,
        error_text,
        error_text_capacity);
    if (InterlockedCompareExchange(
            &config->server->restart_required,
            0,
            0) != 0) {
        dio_tts_server_restart(config->server);
    }
    return result;
}
