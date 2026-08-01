#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <dio_voice/agent.h>

#include "yyjson.h"

#define TEST_HEADER_SIZE 36u
#define TEST_MAX_PAYLOAD (1024u * 1024u)

typedef struct TestCapture {
    CRITICAL_SECTION lock;
    HANDLE changed;
    unsigned int ready;
    unsigned int accepted;
    unsigned int accepted_sample_rate;
    unsigned int deltas;
    unsigned int audio_chunks;
    size_t audio_samples;
    unsigned int complete;
    unsigned int errors;
    unsigned int error_http_status;
    char error_text[256];
    unsigned int mcp_status;
    unsigned int approvals;
    uint64_t approval_turn;
    uint64_t approval_request;
    char approval_server[65];
    char approval_tool[129];
    char approval_arguments[256];
    bool last_cancelled;
    char completed_text[256];
} TestCapture;

typedef enum TestWait {
    TEST_WAIT_READY = 0,
    TEST_WAIT_ACCEPTED,
    TEST_WAIT_APPROVAL,
    TEST_WAIT_COMPLETE,
    TEST_WAIT_ERROR
} TestWait;

static uint16_t test_read_u16(const unsigned char *input) {
    return
        (uint16_t)input[0] |
        ((uint16_t)input[1] << 8u);
}

static uint32_t test_read_u32(const unsigned char *input) {
    uint32_t value = 0u;
    for (unsigned int index = 0u; index < 4u; ++index) {
        value |= (uint32_t)input[index] << (index * 8u);
    }
    return value;
}

static uint64_t test_read_u64(const unsigned char *input) {
    uint64_t value = 0u;
    for (unsigned int index = 0u; index < 8u; ++index) {
        value |= (uint64_t)input[index] << (index * 8u);
    }
    return value;
}

static void test_write_u16(
    unsigned char *output,
    uint16_t value) {
    output[0] = (unsigned char)(value & 0xffu);
    output[1] = (unsigned char)((value >> 8u) & 0xffu);
}

static void test_write_u32(
    unsigned char *output,
    uint32_t value) {
    for (unsigned int index = 0u; index < 4u; ++index) {
        output[index] =
            (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static void test_write_u64(
    unsigned char *output,
    uint64_t value) {
    for (unsigned int index = 0u; index < 8u; ++index) {
        output[index] =
            (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static bool test_read_exact(
    HANDLE pipe,
    void *buffer,
    size_t size) {
    unsigned char *output = (unsigned char *)buffer;
    size_t offset = 0u;
    while (offset < size) {
        DWORD received = 0u;
        if (!ReadFile(
                pipe,
                output + offset,
                (DWORD)(size - offset),
                &received,
                NULL) ||
            received == 0u) {
            return false;
        }
        offset += received;
    }
    return true;
}

static bool test_write_all(
    HANDLE pipe,
    const void *buffer,
    size_t size) {
    const unsigned char *input = (const unsigned char *)buffer;
    size_t offset = 0u;
    while (offset < size) {
        DWORD written = 0u;
        if (!WriteFile(
                pipe,
                input + offset,
                (DWORD)(size - offset),
                &written,
                NULL) ||
            written == 0u) {
            return false;
        }
        offset += written;
    }
    return true;
}

static bool test_send_frame(
    HANDLE pipe,
    uint64_t generation,
    uint64_t turn_id,
    uint32_t sequence,
    uint16_t type,
    const void *payload,
    size_t payload_size) {
    const size_t total = TEST_HEADER_SIZE + payload_size;
    unsigned char *frame = (unsigned char *)calloc(1u, total);
    if (frame == NULL) {
        return false;
    }
    (void)memcpy(frame, "DIOH", 4u);
    test_write_u16(frame + 4u, 1u);
    test_write_u16(frame + 6u, type);
    test_write_u64(frame + 12u, generation);
    test_write_u64(frame + 20u, turn_id);
    test_write_u32(frame + 28u, sequence);
    test_write_u32(frame + 32u, (uint32_t)payload_size);
    (void)memcpy(frame + TEST_HEADER_SIZE, payload, payload_size);
    const bool sent = test_write_all(pipe, frame, total);
    free(frame);
    return sent;
}

static bool test_send_json(
    HANDLE pipe,
    uint64_t generation,
    uint64_t turn_id,
    uint32_t sequence,
    const char *json) {
    return test_send_frame(
        pipe,
        generation,
        turn_id,
        sequence,
        1u,
        json,
        strlen(json));
}

static bool test_send_pcm16(
    HANDLE pipe,
    uint64_t generation,
    uint64_t turn_id,
    uint32_t sequence) {
    static const int16_t samples[] = {1, -2, 3, -4};
    return test_send_frame(
        pipe,
        generation,
        turn_id,
        sequence,
        3u,
        samples,
        sizeof(samples));
}

static char *test_read_json(
    HANDLE pipe,
    uint64_t *generation,
    uint64_t *turn_id,
    uint32_t *sequence) {
    unsigned char header[TEST_HEADER_SIZE];
    if (!test_read_exact(pipe, header, sizeof(header))) {
        return NULL;
    }
    const uint32_t payload_size = test_read_u32(header + 32u);
    if (memcmp(header, "DIOH", 4u) != 0 ||
        test_read_u16(header + 4u) != 1u ||
        test_read_u16(header + 6u) != 1u ||
        test_read_u32(header + 8u) != 0u ||
        payload_size == 0u ||
        payload_size > TEST_MAX_PAYLOAD) {
        return NULL;
    }
    char *json = (char *)malloc((size_t)payload_size + 1u);
    if (json == NULL ||
        !test_read_exact(pipe, json, payload_size)) {
        free(json);
        return NULL;
    }
    json[payload_size] = '\0';
    *generation = test_read_u64(header + 12u);
    *turn_id = test_read_u64(header + 20u);
    *sequence = test_read_u32(header + 28u);
    return json;
}

static HANDLE test_connect(const wchar_t *pipe_name) {
    const ULONGLONG deadline = GetTickCount64() + 5000u;
    for (;;) {
        HANDLE pipe = CreateFileW(
            pipe_name,
            GENERIC_READ | GENERIC_WRITE,
            0u,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        if (GetTickCount64() >= deadline) {
            return INVALID_HANDLE_VALUE;
        }
        (void)WaitNamedPipeW(pipe_name, 50u);
    }
}

static int test_fake_harness(const wchar_t *pipe_name) {
    HANDLE pipe = test_connect(pipe_name);
    if (pipe == INVALID_HANDLE_VALUE) {
        return 2;
    }
    uint32_t output_sequence = 1u;
    uint32_t input_sequence = 0u;
    uint64_t generation = 0u;
    uint64_t active_turn = 0u;
    uint64_t stale_turn = 0u;
    char prompt[256] = "";
    bool ready = false;
    int exit_code = 0;

    for (;;) {
        uint64_t frame_generation = 0u;
        uint64_t frame_turn = 0u;
        uint32_t frame_sequence = 0u;
        char *json = test_read_json(
            pipe,
            &frame_generation,
            &frame_turn,
            &frame_sequence);
        if (json == NULL) {
            break;
        }
        yyjson_doc *document =
            yyjson_read(json, strlen(json), YYJSON_READ_NOFLAG);
        yyjson_val *root =
            document != NULL
                ? yyjson_doc_get_root(document)
                : NULL;
        yyjson_val *type_value =
            yyjson_is_obj(root)
                ? yyjson_obj_get(root, "type")
                : NULL;
        const char *type =
            yyjson_is_str(type_value)
                ? yyjson_get_str(type_value)
                : NULL;
        bool valid =
            type != NULL &&
            frame_sequence > input_sequence &&
            (generation == 0u || frame_generation == generation);
        if (valid) {
            input_sequence = frame_sequence;
        }
        if (valid && strcmp(type, "hello") == 0) {
            yyjson_val *config = yyjson_obj_get(root, "config");
            yyjson_val *servers = yyjson_is_obj(config)
                ? yyjson_obj_get(config, "mcp_servers")
                : NULL;
            if (yyjson_is_arr(servers) && yyjson_arr_size(servers) != 0u) {
                yyjson_val *http = yyjson_arr_get(servers, 0u);
                yyjson_val *stdio = yyjson_arr_get(servers, 1u);
                yyjson_val *env = yyjson_is_obj(stdio)
                    ? yyjson_obj_get(stdio, "env")
                    : NULL;
                const char *bearer = yyjson_get_str(
                    yyjson_is_obj(http)
                        ? yyjson_obj_get(http, "bearer")
                        : NULL);
                const char *token = yyjson_get_str(
                    yyjson_is_obj(env)
                        ? yyjson_obj_get(env, "TOKEN")
                        : NULL);
                valid = yyjson_arr_size(servers) == 2u &&
                    bearer != NULL && strcmp(bearer, "bearer-test-secret") == 0 &&
                    token != NULL && strcmp(token, "env-secret") == 0;
            }
            generation = frame_generation;
            ready =
                valid && frame_turn == 0u &&
                test_send_json(
                    pipe,
                    generation,
                    0u,
                    output_sequence++,
                    "{\"type\":\"ready\",\"protocol\":2,"
                    "\"server\":\"fake-harness\","
                    "\"provider_configured\":false}");
            valid = ready;
        } else if (
            valid &&
            ready &&
            strcmp(type, "turn.start") == 0) {
            yyjson_val *text_value = yyjson_obj_get(root, "text");
            const char *text =
                yyjson_is_str(text_value)
                    ? yyjson_get_str(text_value)
                    : NULL;
            valid =
                active_turn == 0u &&
                frame_turn != 0u &&
                text != NULL &&
                strlen(text) < sizeof(prompt);
            if (valid) {
                (void)strcpy_s(prompt, sizeof(prompt), text);
                active_turn = frame_turn;
                valid = test_send_json(
                    pipe,
                    generation,
                    active_turn,
                    output_sequence++,
                    strcmp(prompt, "normal") == 0
                        ? "{\"type\":\"turn.accepted\","
                          "\"audio_pcm16_hz\":24000}"
                        : "{\"type\":\"turn.accepted\"}");
            }
            if (valid && stale_turn != 0u) {
                valid = test_send_json(
                    pipe,
                    generation,
                    stale_turn,
                    output_sequence++,
                    "{\"type\":\"text.delta\","
                    "\"text\":\"stale\"}");
                if (valid) {
                    valid = test_send_pcm16(
                        pipe,
                        generation,
                        stale_turn,
                        output_sequence++);
                }
                stale_turn = 0u;
            }
        } else if (
            valid &&
            strcmp(type, "turn.commit") == 0) {
            valid =
                active_turn == frame_turn &&
                active_turn != 0u;
            if (valid && strcmp(prompt, "trigger-error") == 0) {
                valid = test_send_json(
                    pipe,
                    generation,
                    active_turn,
                    output_sequence++,
                    "{\"type\":\"turn.error\","
                    "\"code\":\"provider_error\","
                    "\"http_status\":429,"
                    "\"message\":\"quota exceeded\"}");
                active_turn = 0u;
            } else if (valid && strcmp(prompt, "tool-approval") == 0) {
                valid =
                    test_send_json(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++,
                        "{\"type\":\"mcp.status\",\"configured\":2,"
                        "\"available\":2,\"tools\":3}") &&
                    test_send_json(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++,
                        "{\"type\":\"tool.approval.required\","
                        "\"request_id\":7,\"server\":\"calendar\","
                        "\"tool\":\"create_event\","
                        "\"arguments\":{\"title\":\"test\"}}");
            } else if (
                valid &&
                strcmp(prompt, "wait-for-cancel") != 0) {
                valid =
                    test_send_json(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++,
                        "{\"type\":\"text.delta\","
                        "\"text\":\"hello \"}") &&
                    test_send_pcm16(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++) &&
                    test_send_json(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++,
                        "{\"type\":\"text.delta\","
                        "\"text\":\"from harness.\"}") &&
                    test_send_json(
                        pipe,
                        generation,
                        active_turn,
                        output_sequence++,
                        "{\"type\":\"turn.done\","
                        "\"cancelled\":false,"
                        "\"text\":\"hello from harness.\"}");
                active_turn = 0u;
            }
        } else if (
            valid &&
            strcmp(type, "tool.approval") == 0) {
            yyjson_val *request = yyjson_obj_get(root, "request_id");
            const char *decision = yyjson_get_str(
                yyjson_obj_get(root, "decision"));
            valid = active_turn == frame_turn &&
                active_turn != 0u && yyjson_is_uint(request) &&
                yyjson_get_uint(request) == 7u && decision != NULL &&
                strcmp(decision, "always") == 0 &&
                test_send_json(
                    pipe,
                    generation,
                    active_turn,
                    output_sequence++,
                    "{\"type\":\"turn.done\",\"cancelled\":false,"
                    "\"text\":\"tool approved\"}");
            active_turn = 0u;
        } else if (
            valid &&
            strcmp(type, "turn.cancel") == 0) {
            valid =
                active_turn == frame_turn &&
                active_turn != 0u &&
                test_send_json(
                    pipe,
                    generation,
                    active_turn,
                    output_sequence++,
                    "{\"type\":\"turn.error\","
                    "\"code\":\"cancelled\"}");
            stale_turn = active_turn;
            active_turn = 0u;
        } else {
            valid = false;
        }
        yyjson_doc_free(document);
        free(json);
        if (!valid) {
            exit_code = 3;
            break;
        }
    }
    (void)CloseHandle(pipe);
    return exit_code;
}

static void test_callback(
    void *context,
    const DioAgentEvent *event) {
    TestCapture *capture = (TestCapture *)context;
    EnterCriticalSection(&capture->lock);
    switch (event->type) {
    case DIO_AGENT_EVENT_READY:
        ++capture->ready;
        break;
    case DIO_AGENT_EVENT_MCP_STATUS:
        if (event->mcp_configured != 2u || event->mcp_available != 2u ||
            event->mcp_tools != 3u) {
            ++capture->errors;
        } else {
            ++capture->mcp_status;
        }
        break;
    case DIO_AGENT_EVENT_TOOL_APPROVAL_REQUIRED:
        ++capture->approvals;
        capture->approval_turn = event->turn_id;
        capture->approval_request = event->request_id;
        (void)strcpy_s(
            capture->approval_server,
            sizeof(capture->approval_server),
            event->server_name != NULL ? event->server_name : "");
        (void)strcpy_s(
            capture->approval_tool,
            sizeof(capture->approval_tool),
            event->tool_name != NULL ? event->tool_name : "");
        (void)strcpy_s(
            capture->approval_arguments,
            sizeof(capture->approval_arguments),
            event->arguments_json != NULL ? event->arguments_json : "");
        break;
    case DIO_AGENT_EVENT_ACCEPTED:
        ++capture->accepted;
        capture->accepted_sample_rate = event->sample_rate;
        break;
    case DIO_AGENT_EVENT_TEXT_DELTA:
        ++capture->deltas;
        break;
    case DIO_AGENT_EVENT_AUDIO_PCM16:
        if (event->sample_rate != 24000u ||
            event->pcm16 == NULL ||
            event->sample_count != 4u ||
            event->pcm16[0] != 1 ||
            event->pcm16[1] != -2 ||
            event->pcm16[2] != 3 ||
            event->pcm16[3] != -4) {
            ++capture->errors;
        } else {
            ++capture->audio_chunks;
            capture->audio_samples += event->sample_count;
        }
        break;
    case DIO_AGENT_EVENT_COMPLETE:
        ++capture->complete;
        capture->last_cancelled = event->cancelled;
        capture->completed_text[0] = '\0';
        if (event->text != NULL) {
            const size_t length =
                event->text_length < sizeof(capture->completed_text) - 1u
                    ? event->text_length
                    : sizeof(capture->completed_text) - 1u;
            (void)memcpy(
                capture->completed_text,
                event->text,
                length);
            capture->completed_text[length] = '\0';
        }
        break;
    case DIO_AGENT_EVENT_ERROR:
        ++capture->errors;
        capture->error_http_status = event->http_status;
        if (event->text != NULL) {
            (void)strncpy_s(
                capture->error_text,
                sizeof(capture->error_text),
                event->text,
                event->text_length < sizeof(capture->error_text)
                    ? event->text_length
                    : sizeof(capture->error_text) - 1u);
        }
        break;
    case DIO_AGENT_EVENT_PROGRESS:
    default:
        break;
    }
    (void)SetEvent(capture->changed);
    LeaveCriticalSection(&capture->lock);
}

static void test_noop_callback(
    void *context,
    const DioAgentEvent *event) {
    (void)context;
    (void)event;
}

static unsigned int test_capture_count(
    const TestCapture *capture,
    TestWait wait) {
    switch (wait) {
    case TEST_WAIT_READY:
        return capture->ready;
    case TEST_WAIT_ACCEPTED:
        return capture->accepted;
    case TEST_WAIT_APPROVAL:
        return capture->approvals;
    case TEST_WAIT_COMPLETE:
        return capture->complete;
    case TEST_WAIT_ERROR:
        return capture->errors;
    default:
        return 0u;
    }
}

static bool test_wait(
    TestCapture *capture,
    TestWait wait,
    unsigned int expected) {
    const ULONGLONG deadline = GetTickCount64() + 5000u;
    for (;;) {
        EnterCriticalSection(&capture->lock);
        const bool reached =
            test_capture_count(capture, wait) >= expected;
        if (!reached) {
            (void)ResetEvent(capture->changed);
        }
        LeaveCriticalSection(&capture->lock);
        if (reached) {
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline ||
            WaitForSingleObject(
                capture->changed,
                (DWORD)(deadline - now)) != WAIT_OBJECT_0) {
            return false;
        }
    }
}

static bool test_capture_matches(
    TestCapture *capture,
    unsigned int accepted,
    unsigned int accepted_sample_rate,
    unsigned int deltas,
    unsigned int audio_chunks,
    size_t audio_samples,
    unsigned int complete,
    unsigned int errors,
    bool cancelled,
    const char *text) {
    EnterCriticalSection(&capture->lock);
    const bool matches =
        capture->accepted == accepted &&
        capture->accepted_sample_rate ==
            accepted_sample_rate &&
        capture->deltas == deltas &&
        capture->audio_chunks == audio_chunks &&
        capture->audio_samples == audio_samples &&
        capture->complete == complete &&
        capture->errors == errors &&
        capture->last_cancelled == cancelled &&
        strcmp(capture->completed_text, text) == 0;
    LeaveCriticalSection(&capture->lock);
    return matches;
}

int wmain(int argc, wchar_t **argv) {
    if (argc == 3 && wcscmp(argv[1], L"--pipe") == 0) {
        return test_fake_harness(argv[2]);
    }
    if (argc != 1) {
        return 2;
    }
    TestCapture capture;
    (void)memset(&capture, 0, sizeof(capture));
    InitializeCriticalSection(&capture.lock);
    capture.changed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (capture.changed == NULL) {
        DeleteCriticalSection(&capture.lock);
        return 1;
    }

    const size_t too_long_length = 32768u;
    wchar_t *too_long =
        (wchar_t *)malloc((too_long_length + 1u) * sizeof(*too_long));
    bool valid = too_long != NULL;
    if (too_long != NULL) {
        wmemset(too_long, L'a', too_long_length);
        too_long[too_long_length] = L'\0';
        const DioAgentConfig invalid_config = {
            .executable_path = too_long,
            .callback = test_noop_callback};
        DioAgent *invalid = NULL;
        valid =
            dio_agent_open(
                &invalid_config,
                &invalid) == DIO_AGENT_INVALID_ARGUMENT &&
            invalid == NULL;
        free(too_long);
    }

    const DioAgentConfig immediate_config = {
        .executable_path = argv[0],
        .callback = test_noop_callback};
    DioAgent *immediate = NULL;
    valid =
        valid &&
        dio_agent_open(
            &immediate_config,
            &immediate) == DIO_AGENT_OK;
    dio_agent_close(immediate);

    const DioAgentMcpServerConfig mcp_servers[] = {
        {
            .name = L"calendar",
            .target = L"https://example.invalid/mcp",
            .secret = L"bearer-test-secret",
            .always_tools = L"read_events",
            .enabled = true},
        {
            .name = L"local",
            .target = L"C:\\server.exe",
            .arguments = L"--flag value",
            .working_directory = L"C:\\work",
            .secret = L"TOKEN=env-secret\r\nMODE=prod",
            .always_tools = L"lookup",
            .stdio = true,
            .enabled = true}};
    const DioAgentConfig config = {
        .executable_path = argv[0],
        .mcp_servers = mcp_servers,
        .mcp_server_count = _countof(mcp_servers),
        .callback = test_callback,
        .callback_context = &capture};
    DioAgent *agent = NULL;
    valid =
        valid &&
        dio_agent_open(&config, &agent) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_READY, 1u);

    static const char normal[] = "normal";
    const DioAgentResult normal_submit = dio_agent_submit(
        agent,
        normal,
        sizeof(normal) - 1u);
    valid =
        valid &&
        normal_submit == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_COMPLETE, 1u) &&
        test_capture_matches(
            &capture,
            1u,
            24000u,
            2u,
            1u,
            4u,
            1u,
            0u,
            false,
            "hello from harness.");

    static const char tool_approval[] = "tool-approval";
    valid = valid &&
        dio_agent_submit(
            agent,
            tool_approval,
            sizeof(tool_approval) - 1u) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_APPROVAL, 1u);
    EnterCriticalSection(&capture.lock);
    const uint64_t approval_turn = capture.approval_turn;
    const uint64_t approval_request = capture.approval_request;
    valid = valid && capture.mcp_status == 1u &&
        strcmp(capture.approval_server, "calendar") == 0 &&
        strcmp(capture.approval_tool, "create_event") == 0 &&
        strcmp(capture.approval_arguments, "{\"title\":\"test\"}") == 0;
    LeaveCriticalSection(&capture.lock);
    valid = valid &&
        dio_agent_approve_tool(
            agent,
            approval_turn,
            approval_request,
            DIO_AGENT_TOOL_ALWAYS) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_COMPLETE, 2u);

    static const char error[] = "trigger-error";
    valid =
        valid &&
        dio_agent_submit(
            agent,
            error,
            sizeof(error) - 1u) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_ERROR, 1u);
    EnterCriticalSection(&capture.lock);
    valid = valid && capture.error_http_status == 429u &&
        strcmp(capture.error_text, "HTTP 429: quota exceeded") == 0;
    LeaveCriticalSection(&capture.lock);

    static const char cancel[] = "wait-for-cancel";
    valid =
        valid &&
        dio_agent_submit(
            agent,
            cancel,
            sizeof(cancel) - 1u) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_ACCEPTED, 3u) &&
        dio_agent_cancel(agent) == DIO_AGENT_OK &&
        dio_agent_cancel(agent) == DIO_AGENT_BUSY &&
        test_wait(&capture, TEST_WAIT_COMPLETE, 3u) &&
        test_capture_matches(
            &capture,
            4u,
            0u,
            2u,
            1u,
            4u,
            3u,
            1u,
            true,
            "");

    valid =
        valid &&
        dio_agent_submit(
            agent,
            normal,
            sizeof(normal) - 1u) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_COMPLETE, 4u) &&
        test_capture_matches(
            &capture,
            5u,
            24000u,
            4u,
            2u,
            8u,
            4u,
            1u,
            false,
            "hello from harness.");

    static const char unexpected_audio[] = "unexpected-audio";
    valid =
        valid &&
        dio_agent_submit(
            agent,
            unexpected_audio,
            sizeof(unexpected_audio) - 1u) == DIO_AGENT_OK &&
        test_wait(&capture, TEST_WAIT_ERROR, 2u);

    dio_agent_close(agent);
    (void)CloseHandle(capture.changed);
    DeleteCriticalSection(&capture.lock);
    if (!valid) {
        fwprintf(stderr, L"Harness agent smoke failed.\n");
        return 1;
    }
    (void)printf("dio_agent_harness: passed\n");
    return 0;
}
