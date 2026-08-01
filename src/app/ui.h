#ifndef DIO_VOICE_APP_UI_H
#define DIO_VOICE_APP_UI_H

#include <stdbool.h>

#include "app_support.h"
#include "tool_approval.h"
#include "views.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DIO_UI_EVENT_TEXT_CAP = DIO_VIEW_TEXT_CAP
};

typedef enum DioUiEventKind {
    DIO_UI_EVENT_STATE = 0,
    DIO_UI_EVENT_USER_TEXT,
    DIO_UI_EVENT_ASSISTANT_TEXT,
    DIO_UI_EVENT_ASSISTANT_DELTA,
    DIO_UI_EVENT_CHIP,
    DIO_UI_EVENT_ERROR,
    DIO_UI_EVENT_PROVIDER_REQUIRED,
    DIO_UI_EVENT_VAULT_REQUIRED,
    DIO_UI_EVENT_ANNOUNCEMENT,
    DIO_UI_EVENT_LEVEL,
    DIO_UI_EVENT_MODELS_DISCOVERED,
    DIO_UI_EVENT_MCP_STATUS,
    DIO_UI_EVENT_TOOL_APPROVAL_REQUIRED,
    DIO_UI_EVENT_CLEAR,
    DIO_UI_EVENT_SHOW,
    DIO_UI_EVENT_HIDE
} DioUiEventKind;

/*
 * Events are copied by dio_ui_post(), so callers may submit them from worker
 * threads and immediately reuse their storage.
 */
typedef struct DioUiEvent {
    DioUiEventKind kind;
    DioUiState state;
    DioChipKind chip;
    float value;
    unsigned long long generation;
    unsigned long long turn_id;
    unsigned long long request_id;
    bool truncated;
    wchar_t server[DIO_AGENT_MCP_NAME_CAP];
    wchar_t tool[129];
    wchar_t text[DIO_UI_EVENT_TEXT_CAP];
} DioUiEvent;

typedef enum DioUiCommandKind {
    DIO_UI_COMMAND_SET_PAUSED = 0,
    DIO_UI_COMMAND_PUSH_TO_TALK,
    DIO_UI_COMMAND_CANCEL,
    DIO_UI_COMMAND_SETTINGS_CHANGED,
    DIO_UI_COMMAND_DISCOVER_MODELS,
    DIO_UI_COMMAND_TOOL_APPROVAL,
    DIO_UI_COMMAND_EXIT
} DioUiCommandKind;

typedef struct DioUiCommand {
    DioUiCommandKind kind;
    bool enabled;
    DioSettings settings;
    const DioAgentProfile *profile;
    unsigned long long generation;
    unsigned long long turn_id;
    unsigned long long request_id;
    DioUiToolDecision tool_decision;
    wchar_t endpoint[DIO_AGENT_BASE_URL_CAP];
    wchar_t api_key[4096];
} DioUiCommand;

/*
 * Commands are delivered on the UI thread. The callback must return quickly;
 * long-running work belongs on an application worker.
 */
typedef void (*DioUiCommandCallback)(
    void *context,
    const DioUiCommand *command);

typedef struct DioUiOptions {
    HINSTANCE instance;
    const DioPaths *paths;
    const DioSettings *settings;
    const DioAgentProfile *profile;
    DioUiCommandCallback command;
    void *command_context;
} DioUiOptions;

typedef struct DioUi DioUi;

bool dio_ui_create(
    const DioUiOptions *options,
    DioUi **output);

/* Thread-safe until dio_ui_request_exit() begins shutdown. */
bool dio_ui_post(
    DioUi *ui,
    const DioUiEvent *event);

/* Thread-safe model catalog delivery; models is newline-delimited UTF-16. */
bool dio_ui_post_models(
    DioUi *ui,
    unsigned long long generation,
    const wchar_t *models);

/* Thread-safe. Event producers must stop before dio_ui_destroy(). */
void dio_ui_request_exit(DioUi *ui);

/*
 * Runs the Win32 message loop on the thread that called dio_ui_create().
 * ui_smoke cycles the required visual states and exits without touching
 * settings, microphone, or agent state.
 * settings_smoke captures the real settings HWND and exits without opening
 * the voice or agent runtimes.
 */
int dio_ui_run(
    DioUi *ui,
    bool ui_smoke,
    bool settings_smoke);

void dio_ui_destroy(DioUi *ui);

#ifdef __cplusplus
}
#endif

#endif
