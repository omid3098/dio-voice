#ifndef DIO_VOICE_APP_TOOL_APPROVAL_H
#define DIO_VOICE_APP_TOOL_APPROVAL_H

#include <stdbool.h>

typedef enum DioUiToolDecision {
    DIO_UI_TOOL_ONCE = 0,
    DIO_UI_TOOL_ALWAYS,
    DIO_UI_TOOL_DENY
} DioUiToolDecision;

enum {
    DIO_UI_TOOL_APPROVAL_ONCE_BUTTON = 6101,
    DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON = 6102,
    DIO_UI_TOOL_APPROVAL_DENY_BUTTON = 6103,
    DIO_UI_TOOL_APPROVAL_DEFAULT_BUTTON =
        DIO_UI_TOOL_APPROVAL_DENY_BUTTON
};

static inline DioUiToolDecision dio_ui_resolve_tool_approval(
    bool dialog_succeeded,
    int selected_button,
    bool always_saved) {
    if (!dialog_succeeded) {
        return DIO_UI_TOOL_DENY;
    }
    if (selected_button == DIO_UI_TOOL_APPROVAL_ONCE_BUTTON) {
        return DIO_UI_TOOL_ONCE;
    }
    if (selected_button == DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON &&
        always_saved) {
        return DIO_UI_TOOL_ALWAYS;
    }
    return DIO_UI_TOOL_DENY;
}

#endif
