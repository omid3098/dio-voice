#ifndef DIO_VOICE_APP_SUPPORT_H
#define DIO_VOICE_APP_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

#define DIO_MODEL_DISCOVERY_TIMEOUT_MS 10000ull

typedef struct DioPaths {
    wchar_t root[MAX_PATH];
    wchar_t settings[MAX_PATH];
    wchar_t secrets[MAX_PATH];
    wchar_t models[MAX_PATH];
    wchar_t runtime[MAX_PATH];
    wchar_t workspace[MAX_PATH];
    wchar_t announce[MAX_PATH];
    wchar_t logs[MAX_PATH];
    wchar_t executable[MAX_PATH];
    wchar_t font[MAX_PATH];
} DioPaths;

typedef struct DioSettings {
    bool persian;
    bool reduced_motion;
    wchar_t microphone_name[256];
    wchar_t microphone_id[256];
    wchar_t model_dir[MAX_PATH];
    wchar_t runtime_dir[MAX_PATH];
    wchar_t announcement_dir[MAX_PATH];
    float wake_sensitivity;
    float vad_threshold;
    float vad_hysteresis;
    unsigned int command_silence_ms;
    unsigned int command_start_timeout_ms;
    unsigned int command_max_ms;
    unsigned int follow_up_ms;
} DioSettings;

enum {
    DIO_AGENT_BASE_URL_CAP = 2048,
    DIO_AGENT_MODEL_CAP = 256,
    DIO_AGENT_OPTION_CAP = 64,
    DIO_AGENT_SECRET_NAME_CAP = 64,
    DIO_AGENT_MCP_NAME_CAP = 128,
    DIO_AGENT_MCP_TARGET_CAP = 2048,
    DIO_AGENT_MCP_ARGUMENTS_CAP = 1024,
    DIO_AGENT_MCP_WORKING_DIRECTORY_CAP = 1024,
    DIO_AGENT_MCP_SECRET_VALUE_CAP = 4096,
    DIO_AGENT_MCP_ALWAYS_MAX = 128,
    DIO_AGENT_MCP_ALWAYS_CHARS = 65536,
    DIO_AGENT_MCP_MAX = 16
};

typedef struct DioMcpServer {
    bool enabled;
    bool stdio;
    bool secret_dirty;
    wchar_t name[DIO_AGENT_MCP_NAME_CAP];
    wchar_t target[DIO_AGENT_MCP_TARGET_CAP];
    wchar_t arguments[DIO_AGENT_MCP_ARGUMENTS_CAP];
    wchar_t working_directory[DIO_AGENT_MCP_WORKING_DIRECTORY_CAP];
    wchar_t secret_id[DIO_AGENT_SECRET_NAME_CAP];
    wchar_t *always_tools;
    wchar_t secret_value[DIO_AGENT_MCP_SECRET_VALUE_CAP];
} DioMcpServer;

typedef struct DioAgentProfile {
    wchar_t base_url[DIO_AGENT_BASE_URL_CAP];
    wchar_t model[DIO_AGENT_MODEL_CAP];
    wchar_t api_key_secret_id[DIO_AGENT_SECRET_NAME_CAP];
    wchar_t reasoning_effort[DIO_AGENT_OPTION_CAP];
    wchar_t service_tier[DIO_AGENT_OPTION_CAP];
    wchar_t *system_prompt;
    DioMcpServer mcp_servers[DIO_AGENT_MCP_MAX];
    size_t mcp_server_count;
} DioAgentProfile;

bool dio_paths_initialize(DioPaths *paths, bool isolated_data);
bool dio_settings_load(
    const DioPaths *paths,
    DioSettings *settings,
    wchar_t *error,
    size_t error_capacity);
bool dio_settings_save(
    const DioPaths *paths,
    const DioSettings *settings,
    wchar_t *error,
    size_t error_capacity);
void dio_agent_profile_init(DioAgentProfile *profile);
void dio_agent_profile_free(DioAgentProfile *profile);
bool dio_agent_profile_copy(
    DioAgentProfile *target,
    const DioAgentProfile *source);
bool dio_agent_profile_configured(const DioAgentProfile *profile);
bool dio_agent_profile_session_secrets_ready(
    const DioAgentProfile *profile,
    const wchar_t *provider_api_key);
bool dio_agent_profile_detach_api_key_secret(
    DioAgentProfile *profile,
    wchar_t *detached_id,
    size_t detached_capacity);
bool dio_model_discovery_deadline_reached(
    unsigned long long deadline,
    unsigned long long now);
bool dio_settings_load_all(
    const DioPaths *paths,
    DioSettings *settings,
    DioAgentProfile *profile,
    wchar_t *error,
    size_t error_capacity);
bool dio_settings_save_all(
    const DioPaths *paths,
    const DioSettings *settings,
    const DioAgentProfile *profile,
    wchar_t *error,
    size_t error_capacity);
void dio_metric(
    const DioPaths *paths,
    const char *event_name,
    unsigned long long duration_ms,
    unsigned long code);
bool dio_run_marker_start(
    const DioPaths *paths,
    bool *previous_unclean_exit);
bool dio_run_marker_clean(const DioPaths *paths);
int dio_utf8_to_wide(const char *source, wchar_t *target, size_t capacity);
int dio_wide_to_utf8(const wchar_t *source, char *target, size_t capacity);

#endif
