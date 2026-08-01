#ifndef DIO_VOICE_SETTINGS_VIEW_H
#define DIO_VOICE_SETTINGS_VIEW_H

#include <stdbool.h>

#include <cui/cui_win32.h>

typedef enum DioSettingsControl {
    DIO_SETTINGS_CONTROL_LOCALE = 0,
    DIO_SETTINGS_CONTROL_MICROPHONE,
    DIO_SETTINGS_CONTROL_SILENCE,
    DIO_SETTINGS_CONTROL_FOLLOW_UP_ENABLED,
    DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS,
    DIO_SETTINGS_CONTROL_REDUCED_MOTION,
    DIO_SETTINGS_CONTROL_CANCEL,
    DIO_SETTINGS_CONTROL_SAVE,
    DIO_SETTINGS_CONTROL_COUNT
} DioSettingsControl;

typedef enum DioSettingsControlState {
    DIO_SETTINGS_CONTROL_STATE_NONE = 0,
    DIO_SETTINGS_CONTROL_STATE_FOCUSED = 1u << 0u,
    DIO_SETTINGS_CONTROL_STATE_DISABLED = 1u << 1u,
    DIO_SETTINGS_CONTROL_STATE_INVALID = 1u << 2u
} DioSettingsControlState;

typedef struct DioSettingsView DioSettingsView;

typedef enum DioSettingsPage {
    DIO_SETTINGS_PAGE_GENERAL = 0,
    DIO_SETTINGS_PAGE_MODEL,
    DIO_SETTINGS_PAGE_SYSTEM_PROMPT,
    DIO_SETTINGS_PAGE_TOOLS,
    DIO_SETTINGS_PAGE_COUNT
} DioSettingsPage;

bool dio_settings_view_create(
    CuiWin32Context *graphics,
    DioSettingsView **output);
void dio_settings_view_destroy(
    DioSettingsView *view);
bool dio_settings_view_prepare(
    DioSettingsView *view,
    bool persian,
    float width,
    float viewport_height,
    float scroll_y);
float dio_settings_view_scroll_max(
    float viewport_height);
CuiRect dio_settings_view_scroll_viewport(
    const DioSettingsView *view);
CuiResult dio_settings_view_draw(
    DioSettingsView *view);
CuiRect dio_settings_view_control_bounds(
    const DioSettingsView *view,
    DioSettingsControl control);
bool dio_settings_view_control_visible(
    const DioSettingsView *view,
    DioSettingsControl control);
void dio_settings_view_set_control_state(
    DioSettingsView *view,
    DioSettingsControl control,
    unsigned int state);
void dio_settings_view_set_page(
    DioSettingsView *view,
    DioSettingsPage page);
const CuiWin32TextLayout *dio_settings_view_button_label(
    const DioSettingsView *view,
    DioSettingsControl control);
bool dio_settings_view_set_choice_text(
    DioSettingsView *view,
    DioSettingsControl control,
    const wchar_t *text);

#endif
