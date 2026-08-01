#ifndef DIO_VOICE_AGENT_H
#define DIO_VOICE_AGENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DioAgent DioAgent;

typedef enum DioAgentResult {
    DIO_AGENT_OK = 0,
    DIO_AGENT_INVALID_ARGUMENT,
    DIO_AGENT_NOT_READY,
    DIO_AGENT_BUSY,
    DIO_AGENT_CLOSED,
    DIO_AGENT_OUT_OF_MEMORY,
    DIO_AGENT_PLATFORM_FAILURE
} DioAgentResult;

typedef enum DioAgentEventType {
    DIO_AGENT_EVENT_READY = 0,
    DIO_AGENT_EVENT_MODELS,
    DIO_AGENT_EVENT_MODELS_ERROR,
    DIO_AGENT_EVENT_MCP_STATUS,
    DIO_AGENT_EVENT_TOOL_APPROVAL_REQUIRED,
    DIO_AGENT_EVENT_ACCEPTED,
    DIO_AGENT_EVENT_TEXT_DELTA,
    DIO_AGENT_EVENT_AUDIO_PCM16,
    DIO_AGENT_EVENT_PROGRESS,
    DIO_AGENT_EVENT_COMPLETE,
    DIO_AGENT_EVENT_ERROR
} DioAgentEventType;

typedef struct DioAgentEvent {
    DioAgentEventType type;
    uint64_t turn_id;
    const char *text;
    size_t text_length;
    const int16_t *pcm16;
    size_t sample_count;
    /* ACCEPTED advertises PCM for this turn; AUDIO_PCM16 describes payload. */
    unsigned int sample_rate;
    unsigned long system_error;
    unsigned int http_status;
    bool cancelled;
    bool provider_configured;
    uint64_t request_id;
    const char *server_name;
    const char *tool_name;
    const char *arguments_json;
    size_t arguments_length;
    unsigned int mcp_configured;
    unsigned int mcp_available;
    unsigned int mcp_tools;
} DioAgentEvent;

/*
 * Events are serialized, but may arrive on a worker thread. Event text is
 * UTF-8 and remains valid only for the duration of the callback. Return
 * promptly and marshal work to the owner thread; both text and pcm16 remain
 * valid only for this callback. Do not call this API from inside it.
 */
typedef void (*DioAgentEventCallback)(
    void *context,
    const DioAgentEvent *event);

typedef struct DioAgentProviderConfig {
    const wchar_t *base_url;
    const wchar_t *api_key;
    const wchar_t *model;
    const wchar_t *reasoning_effort;
    const wchar_t *service_tier;
    const wchar_t *system_prompt;
} DioAgentProviderConfig;

typedef struct DioAgentMcpServerConfig {
    const wchar_t *name;
    const wchar_t *target;
    const wchar_t *arguments;
    const wchar_t *working_directory;
    const wchar_t *secret;
    const wchar_t *always_tools;
    bool stdio;
    bool enabled;
} DioAgentMcpServerConfig;

typedef enum DioAgentToolDecision {
    DIO_AGENT_TOOL_ONCE = 0,
    DIO_AGENT_TOOL_ALWAYS,
    DIO_AGENT_TOOL_DENY
} DioAgentToolDecision;

typedef struct DioAgentConfig {
    /* executable_path selects the bundled dio-harness process. */
    const wchar_t *executable_path;
    const wchar_t *working_directory;
    const DioAgentProviderConfig *provider;
    const DioAgentMcpServerConfig *mcp_servers;
    size_t mcp_server_count;
    DioAgentEventCallback callback;
    void *callback_context;
} DioAgentConfig;

/*
 * open() launches the selected persistent backend. READY is asynchronous and
 * is emitted only after its startup handshake completes.
 */
DioAgentResult dio_agent_open(
    const DioAgentConfig *config,
    DioAgent **output);

/*
 * Exactly one turn may be active. submit() never retries or resends a prompt
 * after an ambiguous transport failure.
 */
DioAgentResult dio_agent_submit(
    DioAgent *agent,
    const char *utf8_text,
    size_t text_length);

DioAgentResult dio_agent_list_models(
    DioAgent *agent,
    uint64_t request_id);

DioAgentResult dio_agent_approve_tool(
    DioAgent *agent,
    uint64_t turn_id,
    uint64_t request_id,
    DioAgentToolDecision decision);

DioAgentResult dio_agent_cancel(
    DioAgent *agent);

/*
 * close() stops the child, drains worker shutdown, and releases the agent.
 * No callback is made after close() returns.
 */
void dio_agent_close(
    DioAgent *agent);

#ifdef __cplusplus
}
#endif

#endif
