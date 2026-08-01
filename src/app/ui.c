#define COBJMACROS
#include "ui.h"
#include "resource.h"
#include "settings_view.h"
#include "vault.h"

#include <commctrl.h>
#include <windowsx.h>
#include <winhttp.h>
#include <cui/cui_icons_default.h>
#include <dwmapi.h>
#include <limits.h>
#include <math.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <oleacc.h>
#include <propvarutil.h>
#include <propsys.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <UIAutomationClient.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <uxtheme.h>
#include <wchar.h>

DEFINE_GUID(
    IID_IMMDeviceEnumerator,
    0xa95664d2,
    0x9614,
    0x4f35,
    0xa7,
    0x46,
    0xde,
    0x8d,
    0xb6,
    0x36,
    0x17,
    0xe6);
DEFINE_GUID(
    CLSID_MMDeviceEnumerator,
    0xbcde0395,
    0xe52f,
    0x467c,
    0x8e,
    0x3d,
    0xc4,
    0x57,
    0x92,
    0x91,
    0x69,
    0x2e);

enum {
    DIO_OVERLAY_WIDTH_DIP = 380,
    DIO_OVERLAY_MIN_HEIGHT_DIP = 72,
    DIO_OVERLAY_MAX_HEIGHT_DIP = 340,
    DIO_OVERLAY_MARGIN_DIP = 16,
    DIO_OVERLAY_HEADER_DIP = 52,
    DIO_SETTINGS_PREFERRED_HEIGHT_DIP = 660,
    DIO_SETTINGS_WINDOW_MARGIN_DIP = 16,
    DIO_SETTINGS_SCROLL_LINE_DIP = 48,
    DIO_TRANSCRIPT_SCROLL_LINE_DIP = 20,
    DIO_EVENT_QUEUE_CAP = 64,
    DIO_SETTINGS_MICROPHONE_CAP = 32,
    DIO_ACCESSIBILITY_PROBE_TEXT_CAP = 4096,
    DIO_TRAY_LOADING_FRAME_COUNT = 12,
    DIO_TRAY_LOADING_INTERVAL_MS = 80,
    DIO_TIMER_ANIMATION = 1,
    DIO_TIMER_HIDE = 2,
    DIO_TIMER_SMOKE = 3,
    DIO_TIMER_SETTINGS_SMOKE = 4,
    DIO_TIMER_TRAY_MENU_SMOKE = 5,
    DIO_TIMER_SETTINGS_MENU_SMOKE = 6,
    DIO_TIMER_TRAY_LOADING = 7,
    DIO_TIMER_MODEL_DISCOVERY = 8,
    DIO_BUTTON_HOVER_SUBCLASS = 1,
    DIO_EDIT_BEHAVIOR_SUBCLASS = 2,
    DIO_CHECKBOX_VISUAL_SUBCLASS = 3,
    DIO_CLOSE_VISUAL_SUBCLASS = 4,
    DIO_TRANSCRIPT_SUBCLASS = 5,
    DIO_CLOSE_ID = 100,
    DIO_STATUS_ID = 101,
    DIO_MESSAGES_ID = 102,
    DIO_PROVIDER_BUTTON_ID = 103,
    DIO_TRAY_SHOW = 200,
    DIO_TRAY_PAUSE = 201,
    DIO_TRAY_PTT = 202,
    DIO_TRAY_STOP = 203,
    DIO_TRAY_SETTINGS = 204,
    DIO_TRAY_EXIT = 205,
    DIO_SETTINGS_LOCALE = 300,
    DIO_SETTINGS_MICROPHONE = 301,
    DIO_SETTINGS_REDUCED_MOTION = 303,
    DIO_SETTINGS_SILENCE = 304,
    DIO_SETTINGS_FOLLOW_UP = 305,
    DIO_SETTINGS_FOLLOW_UP_ENABLED = 307,
    DIO_SETTINGS_TABS = 320,
    DIO_SETTINGS_BASE_URL = 321,
    DIO_SETTINGS_API_KEY = 322,
    DIO_SETTINGS_MODEL = 323,
    DIO_SETTINGS_REASONING = 324,
    DIO_SETTINGS_SERVICE_TIER = 325,
    DIO_SETTINGS_VAULT_PASSWORD = 326,
    DIO_SETTINGS_VAULT_ACTION = 327,
    DIO_SETTINGS_VAULT_CHANGE = 328,
    DIO_SETTINGS_VAULT_RESET = 329,
    DIO_SETTINGS_SYSTEM_PROMPT = 330,
    DIO_SETTINGS_VAULT_CONFIRM = 331,
    DIO_SETTINGS_PROMPT_RESET = 332,
    DIO_SETTINGS_MCP_LIST = 340,
    DIO_SETTINGS_MCP_NAME = 341,
    DIO_SETTINGS_MCP_URL = 342,
    DIO_SETTINGS_MCP_TOKEN = 343,
    DIO_SETTINGS_MCP_ENABLED = 344,
    DIO_SETTINGS_MCP_ADD = 345,
    DIO_SETTINGS_MCP_REMOVE = 346,
    DIO_SETTINGS_MCP_TRANSPORT = 347,
    DIO_SETTINGS_MCP_ARGUMENTS = 348,
    DIO_SETTINGS_MCP_CWD = 349,
    DIO_SETTINGS_MCP_ENVIRONMENT = 350
};

enum {
    DIO_WM_EVENTS = WM_APP + 0x31,
    DIO_WM_TRAY = WM_APP + 0x32,
    DIO_WM_EXIT = WM_APP + 0x33,
    DIO_WM_TRANSCRIPT_QUERY = WM_APP + 0x34,
    DIO_WM_TRANSCRIPT_SCROLL = WM_APP + 0x35,
    DIO_WM_TRANSCRIPT_PERCENT = WM_APP + 0x36,
    DIO_WM_MODELS = WM_APP + 0x37,
    DIO_WM_MODELS_READY = WM_APP + 0x38,
    DIO_WM_VAULT_REQUIRED = WM_APP + 0x39
};

typedef struct DioTranscriptScrollInfo {
    BOOL vertically_scrollable;
    double vertical_percent;
    double vertical_view_size;
} DioTranscriptScrollInfo;

static const wchar_t DIO_OVERLAY_CLASS[] = L"DioVoiceOverlayWindow";
static const wchar_t DIO_SETTINGS_CLASS[] = L"DioVoiceSettingsWindow";
static const wchar_t DIO_SETTINGS_BASE_TOP[] = L"DioSettingsBaseTop";
static const wchar_t DIO_SETTINGS_BASE_BOTTOM[] = L"DioSettingsBaseBottom";
static const HRESULT DIO_E_ARGUMENT_OUT_OF_RANGE =
    (HRESULT)0x80131502L;

typedef struct DioSettingsDialog {
    struct DioUi *ui;
    HWND window;
    HWND locale;
    HWND microphone;
    HWND silence;
    HWND follow_up_enabled;
    HWND follow_up;
    HWND reduced_motion;
    HWND save;
    HWND cancel;
    HWND tabs;
    HWND base_url;
    HWND api_key;
    HWND model;
    HWND reasoning;
    HWND service_tier;
    HWND model_status;
    HWND vault_password;
    HWND vault_confirm;
    HWND vault_action;
    HWND vault_change;
    HWND vault_reset;
    HWND system_prompt;
    HWND prompt_reset;
    HWND mcp_list;
    HWND mcp_transport;
    HWND mcp_name;
    HWND mcp_url;
    HWND mcp_arguments;
    HWND mcp_cwd;
    HWND mcp_token;
    HWND mcp_environment;
    HWND mcp_enabled;
    HWND mcp_add;
    HWND mcp_remove;
    HWND mcp_target_label;
    HWND mcp_arguments_label;
    HWND mcp_cwd_label;
    HWND mcp_secret_label;
    HWND page_labels[16];
    DioSettingsPage page_label_pages[16];
    size_t page_label_count;
    wchar_t microphones
        [DIO_SETTINGS_MICROPHONE_CAP][256];
    wchar_t microphone_ids
        [DIO_SETTINGS_MICROPHONE_CAP][256];
    size_t microphone_count;
    size_t microphone_index;
    int locale_index;
    int mcp_index;
    DioSettingsPage page;
    DioAgentProfile profile;
    unsigned long long discovery_generation;
    UINT default_button_id;
    IAccPropServices *accessibility;
    HFONT font;
    HBRUSH field_brush;
    HBRUSH background_brush;
    CuiWin32Context *graphics;
    DioSettingsView *view;
    UINT dpi;
    float scale;
    int scroll_y;
    int scroll_max;
    int wheel_remainder;
    unsigned int invalid_controls;
    bool high_contrast;
    bool applying_appearance;
    bool menu_smoke_ok;
    bool normalizing_edit;
    bool secret_dirty;
    bool changing_endpoint;
    bool discovery_anonymous;
    bool changing_mcp;
    bool mcp_secret_dirty;
    bool smoke_matrix_base;
    bool smoke_matrix_scroll;
    bool smoke_matrix_page_keys;
    bool smoke_matrix_compact_tab;
    bool smoke_matrix_validation;
    bool smoke_matrix_other_pages;
    wchar_t discovery_endpoint[DIO_AGENT_BASE_URL_CAP];
    wchar_t detached_api_key_secret_id[DIO_AGENT_SECRET_NAME_CAP];
} DioSettingsDialog;

typedef struct DioMenuItem {
    MSAAMENUINFO accessibility;
    const struct DioUi *ui;
    const wchar_t *text;
    HFONT font;
    UINT dpi;
    bool rtl;
    bool separator;
    bool checked;
} DioMenuItem;

typedef struct DioCloseProvider DioCloseProvider;
typedef struct DioTranscriptProvider {
    IRawElementProviderSimple simple;
    IScrollProvider scroll;
    volatile LONG references;
    HWND window;
} DioTranscriptProvider;

static void dio_settings_invalid_accessibility(
    DioSettingsDialog *dialog,
    HWND control,
    bool invalid);
static HBRUSH dio_prepare_menu(
    HMENU menu,
    const struct DioUi *ui,
    bool high_contrast);
static bool dio_append_menu_item(
    HMENU menu,
    DioMenuItem *item,
    const struct DioUi *ui,
    UINT command,
    const wchar_t *text,
    HFONT font,
    UINT dpi,
    bool rtl,
    bool separator,
    bool checked,
    bool owner_draw);
static bool dio_measure_menu_item(
    MEASUREITEMSTRUCT *measure);
static bool dio_draw_menu_item(
    const DRAWITEMSTRUCT *draw);
static bool dio_menu_char(
    WPARAM wparam,
    LPARAM lparam,
    LRESULT *result);
static void dio_draw_settings_checkbox(
    DioSettingsDialog *dialog,
    HWND checkbox,
    HDC dc);
static bool dio_normalize_single_line(
    wchar_t *text);
static void dio_show_settings(DioUi *ui, bool vault_required);

struct DioUi {
    HINSTANCE instance;
    DWORD thread_id;
    HWND window;
    HWND close_button;
    HWND provider_button;
    HWND status_semantic;
    HWND messages_semantic;
    DioCloseProvider *close_provider;
    DioTranscriptProvider *transcript_provider;
    CuiTheme theme;
    CuiWin32Context *graphics;
    CuiWin32TextLayout *provider_label;
    DioView *view;
    DioViewModel model;
    bool user_message_open;
    bool pending_audio_output;
    wchar_t last_error_text[DIO_UI_EVENT_TEXT_CAP];
    DioPaths paths;
    DioSettings settings;
    DioAgentProfile profile;
    DioVault vault;
    DioUiCommandCallback command;
    void *command_context;
    CRITICAL_SECTION event_lock;
    bool event_lock_initialized;
    DioUiEvent events[DIO_EVENT_QUEUE_CAP];
    size_t event_head;
    size_t event_count;
    wchar_t *pending_models;
    unsigned long long pending_models_generation;
    int transcript_wheel_remainder;
    float transcript_drag_offset;
    DioTranscriptScrollInfo transcript_uia_info;
    UINT dpi;
    float scale;
    bool high_contrast;
    bool paused;
    bool provider_required;
    bool tray_added;
    bool tray_menu_active;
    bool tray_menu_smoke_ok;
    bool overlay_chrome_ok;
    bool animation_timer;
    bool outside_input_registered;
    bool interaction_suppressed;
    bool semantic_message_pending;
    bool semantic_message_notify;
    bool transcript_dragging;
    bool transcript_uia_info_valid;
    unsigned int close_paint_count;
    CuiResult close_paint_result;
    HWND settings_window;
    bool font_loaded;
    bool smoke;
    bool ui_smoke_ok;
    bool settings_smoke;
    bool settings_smoke_ok;
    unsigned int smoke_step;
    UINT taskbar_created;
    NOTIFYICONDATAW tray;
    HICON tray_icons[DIO_UI_ERROR + 1];
    HICON tray_loading_frames[
        DIO_TRAY_LOADING_FRAME_COUNT - 1];
    unsigned int tray_loading_frame;
    bool tray_loading_timer;
    ULONGLONG last_mouse_tray_toggle;
    volatile LONG closing;
};

static LRESULT CALLBACK dio_overlay_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);
static LRESULT CALLBACK dio_transcript_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference);
static LRESULT CALLBACK dio_settings_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

static bool dio_fit_settings_rect(
    RECT *rect,
    UINT dpi);

static int dio_px(const DioUi *ui, float dip) {
    return (int)lroundf(dip * ui->scale);
}

static void dio_transcript_scroll_info(
    const DioUi *ui,
    DioTranscriptScrollInfo *info) {
    const float maximum = ui->view != NULL
        ? dio_view_scroll_max(ui->view)
        : 0.0f;
    const float viewport = ui->view != NULL
        ? dio_view_scroll_page(ui->view) + 40.0f
        : 0.0f;
    info->vertically_scrollable =
        maximum > 0.0f ? TRUE : FALSE;
    info->vertical_percent = maximum > 0.0f
        ? 100.0 *
            (double)dio_view_scroll_y(ui->view) /
            (double)maximum
        : UIA_ScrollPatternNoScroll;
    info->vertical_view_size = maximum > 0.0f
        ? 100.0 * (double)viewport /
            ((double)viewport + (double)maximum)
        : 100.0;
}

static void dio_notify_transcript_scroll(
    DioUi *ui) {
    DioTranscriptScrollInfo current;
    DioTranscriptScrollInfo previous;
    VARIANT old_value;
    VARIANT new_value;
    const bool had_previous =
        ui->transcript_uia_info_valid;

    dio_transcript_scroll_info(ui, &current);
    previous = ui->transcript_uia_info;
    ui->transcript_uia_info = current;
    ui->transcript_uia_info_valid = true;
    if (!had_previous ||
        ui->transcript_provider == NULL ||
        !UiaClientsAreListening()) {
        return;
    }
    VariantInit(&old_value);
    VariantInit(&new_value);
    if (fabs(
            previous.vertical_percent -
            current.vertical_percent) > 0.01) {
        V_VT(&old_value) = VT_R8;
        V_R8(&old_value) = previous.vertical_percent;
        V_VT(&new_value) = VT_R8;
        V_R8(&new_value) = current.vertical_percent;
        (void)UiaRaiseAutomationPropertyChangedEvent(
            &ui->transcript_provider->simple,
            UIA_ScrollVerticalScrollPercentPropertyId,
            old_value,
            new_value);
    }
    if (fabs(
            previous.vertical_view_size -
            current.vertical_view_size) > 0.01) {
        V_VT(&old_value) = VT_R8;
        V_R8(&old_value) = previous.vertical_view_size;
        V_VT(&new_value) = VT_R8;
        V_R8(&new_value) = current.vertical_view_size;
        (void)UiaRaiseAutomationPropertyChangedEvent(
            &ui->transcript_provider->simple,
            UIA_ScrollVerticalViewSizePropertyId,
            old_value,
            new_value);
    }
    if (previous.vertically_scrollable !=
        current.vertically_scrollable) {
        V_VT(&old_value) = VT_BOOL;
        V_BOOL(&old_value) =
            previous.vertically_scrollable
                ? VARIANT_TRUE
                : VARIANT_FALSE;
        V_VT(&new_value) = VT_BOOL;
        V_BOOL(&new_value) =
            current.vertically_scrollable
                ? VARIANT_TRUE
                : VARIANT_FALSE;
        (void)UiaRaiseAutomationPropertyChangedEvent(
            &ui->transcript_provider->simple,
            UIA_ScrollVerticallyScrollablePropertyId,
            old_value,
            new_value);
    }
}

static const wchar_t *dio_status_text(DioUiState state, bool persian) {
    static const wchar_t *const english[] = {
        L"Starting\u2026",
        L"Waiting for wake word",
        L"Listening\u2026",
        L"Follow-up listening\u2026",
        L"Thinking\u2026",
        L"Speaking",
        L"Reminder",
        L"Listening paused",
        L"Error"
    };
    static const wchar_t *const farsi[] = {
        L"\u062f\u0631 \u062d\u0627\u0644 \u0631\u0627\u0647\u200c\u0627\u0646\u062f\u0627\u0632\u06cc\u2026",
        L"\u0645\u0646\u062a\u0638\u0631 \u0648\u0627\u0698\u0647\u0654 \u0628\u06cc\u062f\u0627\u0631\u0628\u0627\u0634",
        L"\u062f\u0631 \u062d\u0627\u0644 \u0634\u0646\u06cc\u062f\u0646\u2026",
        L"\u0645\u0646\u062a\u0638\u0631 \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc\u2026",
        L"\u062f\u0631 \u062d\u0627\u0644 \u0641\u06a9\u0631\u2026",
        L"\u062f\u0631 \u062d\u0627\u0644 \u067e\u0627\u0633\u062e\u200c\u06af\u0648\u06cc\u06cc",
        L"\u06cc\u0627\u062f\u0622\u0648\u0631\u06cc",
        L"\u0634\u0646\u06cc\u062f\u0646 \u0645\u062a\u0648\u0642\u0641 \u0627\u0633\u062a",
        L"\u062e\u0637\u0627"
    };
    if (state < DIO_UI_LOADING || state > DIO_UI_ERROR) {
        state = DIO_UI_ERROR;
    }
    return persian ? farsi[state] : english[state];
}

static const wchar_t *dio_role_text(
    const DioViewMessage *message,
    bool persian) {
    if (persian) {
        switch (message->kind) {
        case DIO_MESSAGE_USER:
            return
                (message->flags &
                 DIO_MESSAGE_ACCEPTED) != 0u
                    ? L"\u0634\u0645\u0627\u060c \u0648\u0631\u0648\u062f\u06cc \u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646\u060c \u062f\u0631\u06cc\u0627\u0641\u062a\u200c\u0634\u062f\u0647"
                    : L"\u0634\u0645\u0627\u060c \u0648\u0631\u0648\u062f\u06cc \u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646";
        case DIO_MESSAGE_ASSISTANT:
            return
                (message->flags &
                 DIO_MESSAGE_AUDIO_OUTPUT) != 0u
                    ? L"\u062f\u0633\u062a\u06cc\u0627\u0631\u060c \u067e\u0627\u0633\u062e \u0635\u0648\u062a\u06cc"
                    : L"\u062f\u0633\u062a\u06cc\u0627\u0631";
        case DIO_MESSAGE_ANNOUNCEMENT:
            return L"\u06cc\u0627\u062f\u0622\u0648\u0631\u06cc";
        case DIO_MESSAGE_ERROR:
        default:
            return L"\u062e\u0637\u0627";
        }
    }
    switch (message->kind) {
    case DIO_MESSAGE_USER:
        return
            (message->flags &
             DIO_MESSAGE_ACCEPTED) != 0u
                ? L"You, microphone input, received"
                : L"You, microphone input";
    case DIO_MESSAGE_ASSISTANT:
        return
            (message->flags &
             DIO_MESSAGE_AUDIO_OUTPUT) != 0u
                ? L"Assistant, voice output"
                : L"Assistant";
    case DIO_MESSAGE_ANNOUNCEMENT:
        return L"Reminder";
    case DIO_MESSAGE_ERROR:
    default:
        return L"Error";
    }
}

static bool dio_active_state(DioUiState state) {
    return
        state == DIO_UI_LOADING ||
        state == DIO_UI_LISTENING ||
        state == DIO_UI_FOLLOW_UP ||
        state == DIO_UI_THINKING ||
        state == DIO_UI_SPEAKING;
}

static bool dio_can_cancel(DioUiState state) {
    return
        state == DIO_UI_LISTENING ||
        state == DIO_UI_FOLLOW_UP ||
        state == DIO_UI_THINKING ||
        state == DIO_UI_SPEAKING;
}

static bool dio_system_motion_enabled(void) {
    BOOL enabled = TRUE;
    ANIMATIONINFO animation;

    animation.cbSize = sizeof(animation);
    if (!SystemParametersInfoW(
            SPI_GETCLIENTAREAANIMATION,
            0u,
            &enabled,
            0u)) {
        enabled = TRUE;
    }
    if (SystemParametersInfoW(
            SPI_GETANIMATION,
            sizeof(animation),
            &animation,
            0u) &&
        animation.iMinAnimate == 0) {
        enabled = FALSE;
    }
    return enabled != FALSE;
}

static bool dio_motion_enabled(const DioUi *ui) {
    return
        !ui->settings.reduced_motion &&
        dio_system_motion_enabled();
}

static void dio_copy_text(
    wchar_t *target,
    size_t capacity,
    const wchar_t *text) {
    if (target == NULL || capacity == 0u) {
        return;
    }
    (void)wcsncpy_s(
        target,
        capacity,
        text != NULL ? text : L"",
        _TRUNCATE);
}

static bool dio_update_provider_label(DioUi *ui) {
    const wchar_t *text = ui->settings.persian
        ? L"\u062a\u0646\u0638\u06cc\u0645 \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647"
        : L"Configure provider";
    CuiTextLayoutDesc desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.text = text;
    desc.length = wcslen(text);
    desc.max_width = 180.0f;
    desc.max_height = 28.0f;
    desc.font_size = 13.0f;
    desc.line_height = 18.0f;
    desc.weight = CUI_WEIGHT_SEMIBOLD;
    desc.align = CUI_TEXT_CENTER;
    desc.wrap = CUI_TEXT_NO_WRAP;
    desc.direction = ui->settings.persian
        ? CUI_TEXT_RTL
        : CUI_TEXT_LTR;
    return ui->provider_label != NULL
        ? cui_win32_text_layout_update(
            ui->provider_label,
            &desc) == CUI_OK
        : cui_win32_text_layout_create(
            ui->graphics,
            &desc,
            &ui->provider_label) == CUI_OK;
}

static void dio_set_status(DioUi *ui) {
    dio_copy_text(
        ui->model.status,
        _countof(ui->model.status),
        dio_status_text(ui->model.state, ui->settings.persian));
}

static void dio_emit_command(
    DioUi *ui,
    DioUiCommandKind kind,
    bool enabled) {
    DioUiCommand command;
    if (ui->command == NULL) {
        return;
    }
    ZeroMemory(&command, sizeof(command));
    command.kind = kind;
    command.enabled = enabled;
    if (kind == DIO_UI_COMMAND_SETTINGS_CHANGED) {
        size_t index;
        command.settings = ui->settings;
        command.profile = &ui->profile;
        if (ui->vault.unlocked &&
            ui->profile.api_key_secret_id[0] != L'\0') {
            const wchar_t *secret = dio_vault_get(
                &ui->vault,
                ui->profile.api_key_secret_id);
            if (secret != NULL) {
                (void)wcsncpy_s(
                    command.api_key,
                    _countof(command.api_key),
                    secret,
                    _TRUNCATE);
            }
        }
        for (index = 0u; index < ui->profile.mcp_server_count; ++index) {
            DioMcpServer *server = &ui->profile.mcp_servers[index];
            SecureZeroMemory(server->secret_value, sizeof(server->secret_value));
            if (ui->vault.unlocked && server->secret_id[0] != L'\0') {
                const wchar_t *secret = dio_vault_get(
                    &ui->vault,
                    server->secret_id);
                if (secret != NULL) {
                    (void)wcsncpy_s(
                        server->secret_value,
                        _countof(server->secret_value),
                        secret,
                        _TRUNCATE);
                }
            }
        }
    }
    ui->command(ui->command_context, &command);
    SecureZeroMemory(command.api_key, sizeof(command.api_key));
    if (kind == DIO_UI_COMMAND_SETTINGS_CHANGED) {
        size_t index;
        for (index = 0u; index < ui->profile.mcp_server_count; ++index) {
            SecureZeroMemory(
                ui->profile.mcp_servers[index].secret_value,
                sizeof(ui->profile.mcp_servers[index].secret_value));
        }
    }
}

static bool dio_register_overlay_class(HINSTANCE instance) {
    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = dio_overlay_proc;
    window_class.hInstance = instance;
    window_class.hIcon = (HICON)LoadImageW(
        instance,
        MAKEINTRESOURCEW(DIO_ICON_APP),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_SHARED);
    window_class.hIconSm = (HICON)LoadImageW(
        instance,
        MAKEINTRESOURCEW(DIO_ICON_APP),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED);
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = DIO_OVERLAY_CLASS;
    if (RegisterClassExW(&window_class) != 0u) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static bool dio_register_settings_class(HINSTANCE instance) {
    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = dio_settings_proc;
    window_class.hInstance = instance;
    window_class.hIcon = (HICON)LoadImageW(
        instance,
        MAKEINTRESOURCEW(DIO_ICON_APP),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_SHARED);
    window_class.hIconSm = (HICON)LoadImageW(
        instance,
        MAKEINTRESOURCEW(DIO_ICON_APP),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED);
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = DIO_SETTINGS_CLASS;
    if (RegisterClassExW(&window_class) != 0u) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static float dio_measure_text_height(
    DioUi *ui,
    const wchar_t *text,
    float font_size,
    bool rtl,
    float width) {
    HDC device_context;
    HFONT font;
    HFONT old_font;
    RECT bounds;
    int height;

    if (text == NULL || text[0] == L'\0') {
        return 0.0f;
    }
    device_context = GetDC(ui->window);
    if (device_context == NULL) {
        return font_size * 1.5f;
    }
    font = CreateFontW(
        -dio_px(ui, font_size),
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
        DEFAULT_PITCH,
        ui->settings.persian ? L"Vazirmatn" : L"Segoe UI");
    old_font = font != NULL
        ? (HFONT)SelectObject(device_context, font)
        : NULL;
    bounds.left = 0;
    bounds.top = 0;
    bounds.right = dio_px(ui, width);
    bounds.bottom = 0;
    height = DrawTextW(
        device_context,
        text,
        -1,
        &bounds,
            DT_CALCRECT |
            DT_WORDBREAK |
            DT_NOPREFIX |
            (rtl ? DT_RTLREADING : 0u));
    if (old_font != NULL) {
        (void)SelectObject(device_context, old_font);
    }
    DeleteObject(font);
    ReleaseDC(ui->window, device_context);
    return height > 0
        ? (float)height / ui->scale
        : font_size * 1.5f;
}

static float dio_overlay_height(const DioUi *ui_const) {
    DioUi *ui = (DioUi *)ui_const;
    float y = (float)DIO_OVERLAY_HEADER_DIP + 4.0f;
    size_t index;

    for (index = 0u;
         index < ui->model.message_count;
         ++index) {
        const DioViewMessage *message = &ui->model.messages[index];
        const float font_size =
            (message->kind == DIO_MESSAGE_ASSISTANT ? 14.0f : 13.0f) +
            (ui->model.rtl ? 0.5f : 0.0f);
        float height =
            dio_measure_text_height(
                ui,
                message->text,
                font_size,
                ui->model.rtl,
                dio_view_message_text_width(
                    (float)DIO_OVERLAY_WIDTH_DIP,
                    message->kind)) +
            12.0f;
        if (height < 34.0f) {
            height = 34.0f;
        }
        y += height;
    }
    y += ui->model.content_bottom_inset;
    y += 12.0f;
    if (y < (float)DIO_OVERLAY_MIN_HEIGHT_DIP) {
        y = (float)DIO_OVERLAY_MIN_HEIGHT_DIP;
    }
    if (y > (float)DIO_OVERLAY_MAX_HEIGHT_DIP) {
        y = (float)DIO_OVERLAY_MAX_HEIGHT_DIP;
    }
    return y;
}

static bool dio_apply_overlay_chrome(DioUi *ui) {
    const DWM_WINDOW_CORNER_PREFERENCE corner =
        DWMWCP_ROUND;
    const COLORREF border = ui->high_contrast
        ? DWMWA_COLOR_DEFAULT
        : DWMWA_COLOR_NONE;

    (void)SetWindowRgn(ui->window, NULL, TRUE);
    return
        SUCCEEDED(DwmSetWindowAttribute(
            ui->window,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner,
            sizeof(corner))) &&
        SUCCEEDED(DwmSetWindowAttribute(
            ui->window,
            DWMWA_BORDER_COLOR,
            &border,
            sizeof(border)));
}

static wchar_t *dio_transcript_semantic_text(
    const DioUi *ui) {
    const wchar_t *empty = ui->settings.persian
        ? L"گفت‌وگو"
        : L"Conversation";
    size_t required = 1u;
    size_t used = 0u;
    size_t index;
    wchar_t *text;

    if (ui->model.message_count == 0u) {
        required += wcslen(empty);
    } else {
        for (index = 0u;
             index < ui->model.message_count;
             ++index) {
            const wchar_t *body =
                ui->model.messages[index].text != NULL
                    ? ui->model.messages[index].text
                    : L"";
            const wchar_t *role = dio_role_text(
                &ui->model.messages[index],
                ui->settings.persian);
            const size_t separator = index > 0u ? 2u : 0u;
            const size_t role_length = wcslen(role);
            const size_t body_length = wcslen(body);
            if (separator > (size_t)INT_MAX - required) {
                return NULL;
            }
            required += separator;
            if (role_length > (size_t)INT_MAX - required) {
                return NULL;
            }
            required += role_length;
            if (2u > (size_t)INT_MAX - required) {
                return NULL;
            }
            required += 2u;
            if (body_length > (size_t)INT_MAX - required) {
                return NULL;
            }
            required += body_length;
        }
    }
    text = (wchar_t *)malloc(
        required * sizeof(*text));
    if (text == NULL) {
        return NULL;
    }
    if (ui->model.message_count == 0u) {
        const size_t length = wcslen(empty);
        (void)memcpy(
            text,
            empty,
            length * sizeof(*text));
        used = length;
    } else {
        for (index = 0u;
             index < ui->model.message_count;
             ++index) {
            const wchar_t *body =
                ui->model.messages[index].text != NULL
                    ? ui->model.messages[index].text
                    : L"";
            const wchar_t *role = dio_role_text(
                &ui->model.messages[index],
                ui->settings.persian);
            const size_t role_length = wcslen(role);
            const size_t body_length = wcslen(body);
            if (index > 0u) {
                text[used++] = L'\r';
                text[used++] = L'\n';
            }
            (void)memcpy(
                text + used,
                role,
                role_length * sizeof(*text));
            used += role_length;
            text[used++] = L':';
            text[used++] = L' ';
            (void)memcpy(
                text + used,
                body,
                body_length * sizeof(*text));
            used += body_length;
        }
    }
    text[used] = L'\0';
    return text;
}

static void dio_update_semantics(DioUi *ui) {
    wchar_t *text = dio_transcript_semantic_text(ui);
    wchar_t *transcript_current = NULL;
    wchar_t current[128];
    wchar_t close_text[128];
    wchar_t provider_text[128];
    LONG_PTR message_style;
    int current_length;

    current[0] = L'\0';
    (void)GetWindowTextW(
        ui->status_semantic,
        current,
        (int)_countof(current));
    if (wcscmp(current, ui->model.status) != 0) {
        (void)SetWindowTextW(
            ui->status_semantic,
            ui->model.status);
        NotifyWinEvent(
            EVENT_OBJECT_NAMECHANGE,
            ui->status_semantic,
            OBJID_CLIENT,
            CHILDID_SELF);
    }
    message_style = GetWindowLongPtrW(
        ui->messages_semantic,
        GWL_STYLE);
    if (ui->model.message_count == 0u) {
        if (GetFocus() == ui->messages_semantic) {
            (void)SetFocus(ui->close_button);
        }
        message_style &= ~(LONG_PTR)WS_TABSTOP;
    } else {
        message_style |= WS_TABSTOP;
    }
    (void)SetWindowLongPtrW(
        ui->messages_semantic,
        GWL_STYLE,
        message_style);
    if (text != NULL) {
        current_length = GetWindowTextLengthW(
            ui->messages_semantic);
        transcript_current = (wchar_t *)calloc(
            (size_t)current_length + 1u,
            sizeof(*transcript_current));
        if (transcript_current != NULL) {
            (void)GetWindowTextW(
                ui->messages_semantic,
                transcript_current,
                current_length + 1);
        }
        if (transcript_current == NULL ||
            wcscmp(transcript_current, text) != 0) {
            (void)SetWindowTextW(
                ui->messages_semantic,
                text);
        }
    }
    if (text != NULL &&
        ui->semantic_message_notify &&
        ui->semantic_message_pending) {
        NotifyWinEvent(
            EVENT_OBJECT_NAMECHANGE,
            ui->messages_semantic,
            OBJID_CLIENT,
            CHILDID_SELF);
        NotifyWinEvent(
            EVENT_OBJECT_LIVEREGIONCHANGED,
            ui->messages_semantic,
            OBJID_CLIENT,
            CHILDID_SELF);
        ui->semantic_message_pending = false;
    }
    if (text != NULL) {
        ui->semantic_message_notify = false;
    }
    free(transcript_current);
    free(text);
    dio_copy_text(
        close_text,
        _countof(close_text),
        ui->settings.persian
            ? L"\u067e\u0646\u0647\u0627\u0646\u200c\u06a9\u0631\u062f\u0646 \u06af\u0641\u062a\u200c\u0648\u06af\u0648"
            : L"Hide conversation");
    current[0] = L'\0';
    (void)GetWindowTextW(
        ui->close_button,
        current,
        (int)_countof(current));
    if (wcscmp(current, close_text) != 0) {
        (void)SetWindowTextW(
            ui->close_button,
            close_text);
        NotifyWinEvent(
            EVENT_OBJECT_NAMECHANGE,
            ui->close_button,
            OBJID_CLIENT,
            CHILDID_SELF);
    }
    dio_copy_text(
        provider_text,
        _countof(provider_text),
        ui->settings.persian
            ? L"\u062a\u0646\u0638\u06cc\u0645 \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647"
            : L"Configure provider");
    current[0] = L'\0';
    (void)GetWindowTextW(
        ui->provider_button,
        current,
        (int)_countof(current));
    if (wcscmp(current, provider_text) != 0) {
        (void)SetWindowTextW(ui->provider_button, provider_text);
        NotifyWinEvent(
            EVENT_OBJECT_NAMECHANGE,
            ui->provider_button,
            OBJID_CLIENT,
            CHILDID_SELF);
    }
    (void)dio_update_provider_label(ui);
}

static void dio_prepare_view(DioUi *ui) {
    RECT client;
    CuiRect close;
    const UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
    if (ui->graphics == NULL ||
        ui->view == NULL ||
        !GetClientRect(ui->window, &client) ||
        client.right <= 0 ||
        client.bottom <= 0) {
        return;
    }
    cui_win32_context_resize(
        ui->graphics,
        (unsigned int)client.right,
        (unsigned int)client.bottom);
    if (!dio_view_prepare(
            ui->view,
            &ui->model,
            (float)client.right / ui->scale,
            (float)client.bottom / ui->scale)) {
        return;
    }
    dio_notify_transcript_scroll(ui);
    close = dio_view_close_bounds(ui->view);
    (void)SetWindowPos(
        ui->close_button,
        NULL,
        dio_px(ui, close.x),
        dio_px(ui, close.y),
        dio_px(ui, close.width),
        dio_px(ui, close.height),
        flags);
    (void)SetWindowPos(
        ui->status_semantic,
        NULL,
        dio_px(ui, ui->model.rtl ? 92.0f : 28.0f),
        dio_px(ui, 12.0f),
        dio_px(ui, 220.0f),
        dio_px(ui, 30.0f),
        flags);
    (void)SetWindowPos(
        ui->messages_semantic,
        NULL,
        dio_px(ui, 8.0f),
        dio_px(ui, (float)DIO_OVERLAY_HEADER_DIP),
        client.right - dio_px(ui, 16.0f),
        client.bottom - dio_px(
            ui,
            ui->provider_required ? 108.0f : 60.0f),
        flags);
    (void)SetWindowPos(
        ui->provider_button,
        NULL,
        (client.right - dio_px(ui, 196.0f)) / 2,
        client.bottom - dio_px(ui, 52.0f),
        dio_px(ui, 196.0f),
        dio_px(ui, 36.0f),
        flags);
}

static bool dio_scroll_transcript_to(
    DioUi *ui,
    float position) {
    if (ui->view == NULL ||
        !dio_view_scroll_to(ui->view, position)) {
        return false;
    }
    dio_notify_transcript_scroll(ui);
    (void)KillTimer(ui->window, DIO_TIMER_HIDE);
    InvalidateRect(ui->window, NULL, FALSE);
    return true;
}

static bool dio_scroll_transcript_key(
    DioUi *ui,
    WPARAM key) {
    const float current = dio_view_scroll_y(ui->view);
    float position;
    switch (key) {
    case VK_UP:
        position = current -
            (float)DIO_TRANSCRIPT_SCROLL_LINE_DIP;
        break;
    case VK_DOWN:
        position = current +
            (float)DIO_TRANSCRIPT_SCROLL_LINE_DIP;
        break;
    case VK_PRIOR:
        position = current -
            dio_view_scroll_page(ui->view);
        break;
    case VK_NEXT:
        position = current +
            dio_view_scroll_page(ui->view);
        break;
    case VK_HOME:
        position = 0.0f;
        break;
    case VK_END:
        position = dio_view_scroll_max(ui->view);
        break;
    default:
        return false;
    }
    (void)dio_scroll_transcript_to(ui, position);
    return true;
}

static bool dio_transcript_reading(
    const DioUi *ui) {
    return
        ui->model.transcript_focused ||
        ui->transcript_dragging ||
        GetCapture() == ui->messages_semantic ||
        dio_view_scroll_y(ui->view) <
            dio_view_scroll_max(ui->view) - 0.5f;
}

static bool dio_point_over_transcript(
    const DioUi *ui,
    LPARAM lparam) {
    POINT point = {
        GET_X_LPARAM(lparam),
        GET_Y_LPARAM(lparam)};
    RECT bounds;
    return
        ui->messages_semantic != NULL &&
        ScreenToClient(
            ui->messages_semantic,
            &point) &&
        GetClientRect(
            ui->messages_semantic,
            &bounds) &&
        PtInRect(&bounds, point) != FALSE;
}

static bool dio_scroll_transcript_wheel(
    DioUi *ui,
    WPARAM wparam,
    LPARAM lparam) {
    UINT lines = 3u;
    float amount;
    if (dio_view_scroll_max(ui->view) <= 0.0f ||
        !dio_point_over_transcript(ui, lparam)) {
        return false;
    }
    if (!SystemParametersInfoW(
            SPI_GETWHEELSCROLLLINES,
            0u,
            &lines,
            0u)) {
        lines = 3u;
    }
    if (lines == 0u) {
        return true;
    }
    amount = lines == WHEEL_PAGESCROLL
        ? dio_view_scroll_page(ui->view)
        : (float)lines *
            (float)DIO_TRANSCRIPT_SCROLL_LINE_DIP;
    ui->transcript_wheel_remainder +=
        GET_WHEEL_DELTA_WPARAM(wparam);
    while (ui->transcript_wheel_remainder >=
           WHEEL_DELTA) {
        (void)dio_scroll_transcript_to(
            ui,
            dio_view_scroll_y(ui->view) - amount);
        ui->transcript_wheel_remainder -=
            WHEEL_DELTA;
    }
    while (ui->transcript_wheel_remainder <=
           -WHEEL_DELTA) {
        (void)dio_scroll_transcript_to(
            ui,
            dio_view_scroll_y(ui->view) + amount);
        ui->transcript_wheel_remainder +=
            WHEEL_DELTA;
    }
    return true;
}

static void dio_place_overlay_on_monitor(
    DioUi *ui,
    HMONITOR handle) {
    MONITORINFO monitor;
    RECT window_rect;
    int width;
    int height;
    int margin;
    int available_width;
    int available_height;
    int x;
    int y;

    width = dio_px(ui, (float)DIO_OVERLAY_WIDTH_DIP);
    height = dio_px(ui, dio_overlay_height(ui));
    margin = dio_px(ui, (float)DIO_OVERLAY_MARGIN_DIP);
    if (handle == NULL) {
        handle = MonitorFromWindow(
            ui->window,
            MONITOR_DEFAULTTONEAREST);
    }
    ZeroMemory(&monitor, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(handle, &monitor)) {
        return;
    }
    available_width =
        monitor.rcWork.right -
        monitor.rcWork.left -
        margin * 2;
    available_height =
        monitor.rcWork.bottom -
        monitor.rcWork.top -
        margin * 2;
    if (available_width > 0 && width > available_width) {
        width = available_width;
    }
    if (available_height > 0 &&
        height > available_height) {
        height = available_height;
    }
    x = monitor.rcWork.right - margin - width;
    y = monitor.rcWork.bottom - margin - height;
    if (!GetWindowRect(ui->window, &window_rect) ||
        window_rect.left != x ||
        window_rect.top != y ||
        window_rect.right - window_rect.left != width ||
        window_rect.bottom - window_rect.top != height) {
        (void)SetWindowPos(
            ui->window,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    } else {
        dio_prepare_view(ui);
    }
}

static void dio_place_overlay(DioUi *ui) {
    dio_place_overlay_on_monitor(ui, NULL);
}

static void dio_sync_animation_timer(DioUi *ui) {
    const bool motion = dio_motion_enabled(ui);
    const bool wanted =
        IsWindowVisible(ui->window) &&
        dio_active_state(ui->model.state) &&
        motion &&
        !ui->smoke;
    if (wanted && !ui->animation_timer) {
        ui->animation_timer =
            SetTimer(ui->window, DIO_TIMER_ANIMATION, 16u, NULL) != 0u;
    } else if (!wanted && ui->animation_timer) {
        (void)KillTimer(ui->window, DIO_TIMER_ANIMATION);
        ui->animation_timer = false;
        ui->model.phase = 0.0f;
    }
    if (!motion) {
        ui->model.phase = 0.0f;
        ui->model.level = 0.0f;
    }
}

static bool dio_create_tray_icons(DioUi *ui) {
    static const int resource_ids[] = {
        DIO_ICON_TRAY_LOADING,
        DIO_ICON_TRAY_WAITING,
        DIO_ICON_TRAY_LISTENING,
        DIO_ICON_TRAY_FOLLOW_UP,
        DIO_ICON_TRAY_THINKING,
        DIO_ICON_TRAY_SPEAKING,
        DIO_ICON_TRAY_REMINDER,
        DIO_ICON_TRAY_PAUSED,
        DIO_ICON_TRAY_ERROR};
    static const int loading_resource_ids[
        DIO_TRAY_LOADING_FRAME_COUNT - 1] = {
        DIO_ICON_TRAY_LOADING_01,
        DIO_ICON_TRAY_LOADING_02,
        DIO_ICON_TRAY_LOADING_03,
        DIO_ICON_TRAY_LOADING_04,
        DIO_ICON_TRAY_LOADING_05,
        DIO_ICON_TRAY_LOADING_06,
        DIO_ICON_TRAY_LOADING_07,
        DIO_ICON_TRAY_LOADING_08,
        DIO_ICON_TRAY_LOADING_09,
        DIO_ICON_TRAY_LOADING_10,
        DIO_ICON_TRAY_LOADING_11};
    const int size = GetSystemMetricsForDpi(
        SM_CXSMICON,
        ui->dpi);
    HICON icons[DIO_UI_ERROR + 1] = {0};
    HICON loading_frames[
        DIO_TRAY_LOADING_FRAME_COUNT - 1] = {0};
    size_t index;
    for (index = 0u; index < _countof(icons); ++index) {
        icons[index] = (HICON)LoadImageW(
            ui->instance,
            MAKEINTRESOURCEW(resource_ids[index]),
            IMAGE_ICON,
            size,
            size,
            LR_DEFAULTCOLOR);
        if (icons[index] == NULL) {
            while (index > 0u) {
                --index;
                DestroyIcon(icons[index]);
            }
            return false;
        }
    }
    for (index = 0u;
         index < _countof(loading_frames);
         ++index) {
        loading_frames[index] = (HICON)LoadImageW(
            ui->instance,
            MAKEINTRESOURCEW(loading_resource_ids[index]),
            IMAGE_ICON,
            size,
            size,
            LR_DEFAULTCOLOR);
        if (loading_frames[index] == NULL) {
            while (index > 0u) {
                --index;
                DestroyIcon(loading_frames[index]);
            }
            for (index = 0u;
                 index < _countof(icons);
                 ++index) {
                DestroyIcon(icons[index]);
            }
            return false;
        }
    }
    for (index = 0u;
         index < _countof(ui->tray_icons);
         ++index) {
        if (ui->tray_icons[index] != NULL) {
            DestroyIcon(ui->tray_icons[index]);
        }
        ui->tray_icons[index] = icons[index];
    }
    for (index = 0u;
         index < _countof(ui->tray_loading_frames);
         ++index) {
        if (ui->tray_loading_frames[index] != NULL) {
            DestroyIcon(ui->tray_loading_frames[index]);
        }
        ui->tray_loading_frames[index] =
            loading_frames[index];
    }
    return true;
}

static HICON dio_tray_icon(const DioUi *ui) {
    HICON icon = NULL;
    if (ui->model.state == DIO_UI_LOADING &&
        ui->tray_loading_frame > 0u &&
        ui->tray_loading_frame <
            DIO_TRAY_LOADING_FRAME_COUNT) {
        icon = ui->tray_loading_frames[
            ui->tray_loading_frame - 1u];
    }
    if (icon == NULL &&
        ui->model.state >= DIO_UI_LOADING &&
        ui->model.state <= DIO_UI_ERROR) {
        icon = ui->tray_icons[ui->model.state];
    }
    return icon != NULL ? icon : LoadIconW(NULL, IDI_APPLICATION);
}

static void dio_destroy_tray_icons(DioUi *ui) {
    size_t index;
    for (index = 0u; index < _countof(ui->tray_icons); ++index) {
        if (ui->tray_icons[index] != NULL) {
            DestroyIcon(ui->tray_icons[index]);
            ui->tray_icons[index] = NULL;
        }
    }
    for (index = 0u;
         index < _countof(ui->tray_loading_frames);
         ++index) {
        if (ui->tray_loading_frames[index] != NULL) {
            DestroyIcon(ui->tray_loading_frames[index]);
            ui->tray_loading_frames[index] = NULL;
        }
    }
}

static void dio_update_tray_icon(DioUi *ui) {
    if (!ui->tray_added) {
        return;
    }
    ui->tray.hIcon = dio_tray_icon(ui);
    ui->tray.uFlags = NIF_ICON;
    (void)Shell_NotifyIconW(NIM_MODIFY, &ui->tray);
}

static void dio_update_tray_tip(DioUi *ui) {
    wchar_t tip[_countof(ui->tray.szTip)];
    if (!ui->tray_added) {
        return;
    }
    if (swprintf_s(
            tip,
            _countof(tip),
            L"DIO Voice \u2014 %ls",
            ui->model.status) < 0) {
        dio_copy_text(tip, _countof(tip), L"DIO Voice");
    }
    dio_copy_text(ui->tray.szTip, _countof(ui->tray.szTip), tip);
    ui->tray.hIcon = dio_tray_icon(ui);
    ui->tray.uFlags = NIF_ICON | NIF_TIP;
    (void)Shell_NotifyIconW(NIM_MODIFY, &ui->tray);
}

static bool dio_tray_add(DioUi *ui) {
    ZeroMemory(&ui->tray, sizeof(ui->tray));
    ui->tray.cbSize = (DWORD)sizeof(ui->tray);
    ui->tray.hWnd = ui->window;
    ui->tray.uID = 1u;
    ui->tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    ui->tray.uCallbackMessage = DIO_WM_TRAY;
    ui->tray.hIcon = dio_tray_icon(ui);
    dio_copy_text(ui->tray.szTip, _countof(ui->tray.szTip), L"DIO Voice");
    ui->tray_added = Shell_NotifyIconW(NIM_ADD, &ui->tray) != FALSE;
    if (ui->tray_added) {
        ui->tray.uVersion = NOTIFYICON_VERSION_4;
        (void)Shell_NotifyIconW(NIM_SETVERSION, &ui->tray);
        dio_update_tray_tip(ui);
    }
    return ui->tray_added;
}

static void dio_tray_remove(DioUi *ui) {
    if (ui->window != NULL) {
        (void)KillTimer(
            ui->window,
            DIO_TIMER_TRAY_LOADING);
    }
    ui->tray_loading_timer = false;
    ui->tray_loading_frame = 0u;
    if (ui->tray_added) {
        (void)Shell_NotifyIconW(NIM_DELETE, &ui->tray);
        ui->tray_added = false;
    }
}

static void dio_sync_tray_loading_timer(DioUi *ui) {
    const bool wanted =
        ui->tray_added &&
        ui->model.state == DIO_UI_LOADING &&
        dio_motion_enabled(ui) &&
        !ui->smoke;

    if (!wanted) {
        if (ui->tray_loading_timer) {
            (void)KillTimer(
                ui->window,
                DIO_TIMER_TRAY_LOADING);
            ui->tray_loading_timer = false;
        }
        if (ui->tray_loading_frame != 0u) {
            ui->tray_loading_frame = 0u;
            if (ui->model.state == DIO_UI_LOADING) {
                dio_update_tray_icon(ui);
            }
        }
        return;
    }
    if (!ui->tray_loading_timer) {
        ui->tray_loading_frame = 0u;
        dio_update_tray_icon(ui);
        ui->tray_loading_timer =
            SetTimer(
                ui->window,
                DIO_TIMER_TRAY_LOADING,
                DIO_TRAY_LOADING_INTERVAL_MS,
                NULL) != 0u;
    }
}

static bool dio_set_outside_input(
    DioUi *ui,
    bool wanted) {
    RAWINPUTDEVICE device;
    if (ui->outside_input_registered == wanted) {
        return true;
    }
    ZeroMemory(&device, sizeof(device));
    device.usUsagePage = 0x01u;
    device.usUsage = 0x02u;
    device.dwFlags = wanted
        ? RIDEV_INPUTSINK
        : RIDEV_REMOVE;
    device.hwndTarget = wanted ? ui->window : NULL;
    if (!RegisterRawInputDevices(
            &device,
            1u,
            sizeof(device))) {
        return false;
    }
    ui->outside_input_registered = wanted;
    return true;
}

static bool dio_sync_outside_input(DioUi *ui) {
    const bool wanted =
        IsWindowVisible(ui->window) &&
        !ui->smoke &&
        !ui->tray_menu_active &&
        InterlockedCompareExchange(
            &ui->closing,
            0,
            0) == 0;
    return dio_set_outside_input(ui, wanted);
}

static void dio_show_overlay(DioUi *ui, bool activate) {
    const bool focus = activate && !ui->smoke;
    UINT flags =
        SWP_NOMOVE |
        SWP_NOSIZE |
        SWP_NOOWNERZORDER |
        SWP_SHOWWINDOW;
    if (!activate && ui->interaction_suppressed) {
        return;
    }
    if (activate) {
        ui->interaction_suppressed = false;
    }
    if (!focus) {
        flags |= SWP_NOACTIVATE;
    }
    dio_place_overlay(ui);
    (void)SetWindowPos(
        ui->window,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        flags);
    if (focus) {
        (void)SetForegroundWindow(ui->window);
        (void)SetActiveWindow(ui->window);
        (void)SetFocus(ui->close_button);
    }
    dio_sync_animation_timer(ui);
    (void)dio_sync_outside_input(ui);
    InvalidateRect(ui->window, NULL, FALSE);
}

static void dio_show_overlay_for_change(
    DioUi *ui,
    bool geometry) {
    if (geometry || !IsWindowVisible(ui->window)) {
        dio_show_overlay(ui, false);
    }
}

static void dio_hide_overlay(DioUi *ui, bool user_dismissed) {
    if (user_dismissed) {
        ui->interaction_suppressed = true;
    }
    (void)KillTimer(ui->window, DIO_TIMER_HIDE);
    ShowWindow(ui->window, SW_HIDE);
    dio_sync_animation_timer(ui);
    (void)dio_sync_outside_input(ui);
}

static bool dio_tray_icon_bounds(
    const DioUi *ui,
    RECT *bounds) {
    NOTIFYICONIDENTIFIER identifier;
    if (!ui->tray_added || bounds == NULL) {
        return false;
    }
    ZeroMemory(&identifier, sizeof(identifier));
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = ui->window;
    identifier.uID = ui->tray.uID;
    return SUCCEEDED(Shell_NotifyIconGetRect(
        &identifier,
        bounds));
}

static bool dio_point_in_tray_icon(
    const DioUi *ui,
    POINT point) {
    RECT bounds;
    return
        dio_tray_icon_bounds(ui, &bounds) &&
        PtInRect(&bounds, point) != FALSE;
}

static bool dio_point_is_outside_overlay(
    const DioUi *ui,
    POINT point) {
    RECT bounds;
    return
        GetWindowRect(ui->window, &bounds) &&
        PtInRect(&bounds, point) == FALSE &&
        !dio_point_in_tray_icon(ui, point);
}

static void dio_handle_pointer_press(
    DioUi *ui,
    POINT point) {
    if (dio_point_is_outside_overlay(ui, point)) {
        dio_hide_overlay(ui, true);
    }
}

static void dio_handle_raw_input(
    DioUi *ui,
    HRAWINPUT handle) {
    RAWINPUT input;
    UINT size = sizeof(input);
    const USHORT pressed =
        RI_MOUSE_LEFT_BUTTON_DOWN |
        RI_MOUSE_RIGHT_BUTTON_DOWN |
        RI_MOUSE_MIDDLE_BUTTON_DOWN |
        RI_MOUSE_BUTTON_4_DOWN |
        RI_MOUSE_BUTTON_5_DOWN;
    if (GetRawInputData(
            handle,
            RID_INPUT,
            &input,
            &size,
            sizeof(RAWINPUTHEADER)) == sizeof(input) &&
        input.header.dwType == RIM_TYPEMOUSE &&
        (input.data.mouse.usButtonFlags & pressed) != 0u) {
        POINT point;
        if (GetCursorPos(&point)) {
            dio_handle_pointer_press(ui, point);
        }
    }
}

static void dio_refresh(
    DioUi *ui,
    bool geometry,
    bool layout) {
    dio_set_status(ui);
    if (geometry) {
        dio_update_semantics(ui);
        dio_place_overlay(ui);
        dio_update_tray_tip(ui);
    } else if (layout) {
        dio_prepare_view(ui);
    }
    dio_sync_animation_timer(ui);
    dio_sync_tray_loading_timer(ui);
    InvalidateRect(ui->window, NULL, FALSE);
}

static bool dio_reserve_messages(
    DioViewModel *model,
    size_t needed) {
    DioViewMessage *replacement;
    size_t capacity = model->message_capacity > 0u
        ? model->message_capacity
        : DIO_VIEW_INITIAL_MESSAGES;
    if (needed <= model->message_capacity) {
        return true;
    }
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            return false;
        }
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / sizeof(*replacement)) {
        return false;
    }
    replacement = (DioViewMessage *)realloc(
        model->messages,
        capacity * sizeof(*replacement));
    if (replacement == NULL) {
        return false;
    }
    ZeroMemory(
        replacement + model->message_capacity,
        (capacity - model->message_capacity) *
            sizeof(*replacement));
    model->messages = replacement;
    model->message_capacity = capacity;
    return true;
}

static bool dio_message_reserve_text(
    DioViewMessage *message,
    size_t needed) {
    wchar_t *replacement;
    size_t capacity = message->text_capacity > 0u
        ? message->text_capacity
        : 128u;
    if (needed <= message->text_capacity) {
        return true;
    }
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2u) {
            return false;
        }
        capacity *= 2u;
    }
    if (capacity > (size_t)-1 / sizeof(*replacement)) {
        return false;
    }
    replacement = (wchar_t *)realloc(
        message->text,
        capacity * sizeof(*replacement));
    if (replacement == NULL) {
        return false;
    }
    message->text = replacement;
    message->text_capacity = capacity;
    return true;
}

static bool dio_message_set_text(
    DioViewMessage *message,
    const wchar_t *text) {
    const wchar_t *source = text != NULL ? text : L"";
    const size_t length = wcslen(source);
    if (!dio_message_reserve_text(
            message,
            length + 1u)) {
        return false;
    }
    (void)memcpy(
        message->text,
        source,
        (length + 1u) * sizeof(source[0]));
    message->text_length = length;
    return true;
}

static bool dio_message_append_text(
    DioViewMessage *message,
    const wchar_t *text) {
    const wchar_t *source = text != NULL ? text : L"";
    const size_t length = wcslen(source);
    if (length > (size_t)-1 - message->text_length - 1u ||
        !dio_message_reserve_text(
            message,
            message->text_length + length + 1u)) {
        return false;
    }
    (void)memcpy(
        message->text + message->text_length,
        source,
        (length + 1u) * sizeof(source[0]));
    message->text_length += length;
    return true;
}

static void dio_clear_messages(DioViewModel *model) {
    size_t index;
    for (index = 0u; index < model->message_count; ++index) {
        free(model->messages[index].text);
        ZeroMemory(
            &model->messages[index],
            sizeof(model->messages[index]));
    }
    model->message_count = 0u;
}

static bool dio_add_message(
    DioUi *ui,
    DioMessageKind kind,
    const wchar_t *text,
    bool update_last,
    bool append) {
    DioViewMessage *message;
    bool added = false;
    if (update_last &&
        ui->model.message_count > 0u &&
        ui->model.messages[ui->model.message_count - 1u].kind == kind) {
        message = &ui->model.messages[ui->model.message_count - 1u];
    } else {
        if (!dio_reserve_messages(
                &ui->model,
                ui->model.message_count + 1u)) {
            return false;
        }
        added = true;
        message = &ui->model.messages[ui->model.message_count++];
        ZeroMemory(message, sizeof(*message));
        message->kind = kind;
    }
    if (!(append
            ? dio_message_append_text(message, text)
            : dio_message_set_text(message, text))) {
        if (added) {
            ui->model.message_count -= 1u;
            ZeroMemory(message, sizeof(*message));
        }
        return false;
    }
    if (kind == DIO_MESSAGE_USER) {
        message->flags |= DIO_MESSAGE_MIC_INPUT;
    } else if (
        kind == DIO_MESSAGE_ASSISTANT &&
        ui->pending_audio_output) {
        message->flags |= DIO_MESSAGE_AUDIO_OUTPUT;
        ui->pending_audio_output = false;
    }
    return added;
}

static DioViewMessage *dio_latest_turn_assistant(
    DioUi *ui) {
    size_t index = ui->model.message_count;
    while (index > 0u) {
        DioViewMessage *message;
        --index;
        message = &ui->model.messages[index];
        if (message->kind == DIO_MESSAGE_ASSISTANT) {
            return message;
        }
        if (message->kind == DIO_MESSAGE_USER ||
            message->kind == DIO_MESSAGE_ANNOUNCEMENT ||
            message->kind == DIO_MESSAGE_ERROR) {
            break;
        }
    }
    return NULL;
}

static bool dio_attach_chip(
    DioUi *ui,
    DioChipKind kind,
    bool present) {
    DioViewMessage *message;
    unsigned int flag;

    if (kind == DIO_CHIP_AGENT) {
        return false;
    }
    if (kind == DIO_CHIP_ACK) {
        message = ui->model.message_count > 0u
            ? &ui->model.messages[
                  ui->model.message_count - 1u]
            : NULL;
        if (message == NULL ||
            message->kind != DIO_MESSAGE_USER) {
            return false;
        }
        if (present) {
            ui->user_message_open = false;
        }
        flag = DIO_MESSAGE_ACCEPTED;
    } else {
        message =
            (ui->model.state == DIO_UI_ANNOUNCEMENT ||
             ui->model.state == DIO_UI_ERROR)
                ? NULL
                : dio_latest_turn_assistant(ui);
        flag = DIO_MESSAGE_AUDIO_OUTPUT;
        if (message == NULL) {
            const bool changed =
                ui->pending_audio_output != present;
            ui->pending_audio_output = present;
            return changed;
        }
        ui->pending_audio_output = false;
    }
    if (message == NULL ||
        (((message->flags & flag) != 0u) == present)) {
        return false;
    }
    if (present) {
        message->flags |= flag;
    } else {
        message->flags &= ~flag;
    }
    return true;
}

static bool dio_remember_tool_always(
    DioUi *ui,
    const wchar_t *server_name,
    const wchar_t *tool_name) {
    wchar_t error[256];
    size_t index;
    if (server_name == NULL || tool_name == NULL || tool_name[0] == L'\0' ||
        wcschr(tool_name, L'\r') != NULL || wcschr(tool_name, L'\n') != NULL) {
        return false;
    }
    for (index = 0u; index < ui->profile.mcp_server_count; ++index) {
        DioMcpServer *server = &ui->profile.mcp_servers[index];
        const wchar_t *line;
        if (wcscmp(server->name, server_name) != 0) {
            continue;
        }
        line = server->always_tools != NULL ? server->always_tools : L"";
        while (*line != L'\0') {
            const wchar_t *end = wcschr(line, L'\n');
            const size_t length = end != NULL
                ? (size_t)(end - line)
                : wcslen(line);
            if (wcslen(tool_name) == length &&
                wcsncmp(line, tool_name, length) == 0) {
                return true;
            }
            line = end != NULL ? end + 1 : line + length;
        }
        const size_t old_length = server->always_tools != NULL
            ? wcslen(server->always_tools)
            : 0u;
        const size_t tool_length = wcslen(tool_name);
        if (old_length + tool_length + 2u >= DIO_AGENT_MCP_ALWAYS_CHARS) {
            return false;
        }
        wchar_t *updated = (wchar_t *)malloc(
            (old_length + tool_length + 2u) * sizeof(*updated));
        if (updated == NULL) {
            return false;
        }
        if (old_length != 0u) {
            memcpy(updated, server->always_tools, old_length * sizeof(*updated));
            updated[old_length] = L'\n';
        }
        memcpy(
            updated + old_length + (old_length != 0u ? 1u : 0u),
            tool_name,
            (tool_length + 1u) * sizeof(*updated));
        wchar_t *previous = server->always_tools;
        server->always_tools = updated;
        const bool saved = dio_settings_save_all(
            &ui->paths,
            &ui->settings,
            &ui->profile,
            error,
            _countof(error));
        if (!saved) {
            server->always_tools = previous;
            free(updated);
            return false;
        }
        free(previous);
        return true;
    }
    return false;
}

static void dio_request_tool_approval(
    DioUi *ui,
    const DioUiEvent *event) {
    const bool fa = ui->settings.persian;
    wchar_t content[DIO_UI_EVENT_TEXT_CAP + 512];
    int selected = 0;
    HRESULT dialog_result = E_ABORT;
    bool always_saved = false;
    DioUiCommand command;
    (void)swprintf_s(
        content,
        _countof(content),
        fa
            ? L"\u0633\u0631\u0648\u0631: %ls\n\u0627\u0628\u0632\u0627\u0631: %ls\n\n\u0648\u0631\u0648\u062f\u06cc:\n%ls%ls"
            : L"Server: %ls\nTool: %ls\n\nArguments:\n%ls%ls",
        event->server,
        event->tool,
        event->text,
        event->truncated
            ? (fa ? L"\n\n[\u0646\u0645\u0627\u06cc\u0634 \u0646\u0627\u0642\u0635 \u0627\u0633\u062a]" : L"\n\n[Display truncated]")
            : L"");
    if (!ui->settings_smoke) {
        const TASKDIALOG_BUTTON buttons[] = {
            {
                DIO_UI_TOOL_APPROVAL_ONCE_BUTTON,
                fa ? L"\u0641\u0642\u0637 \u0627\u06cc\u0646 \u0628\u0627\u0631"
                   : L"Allow &once"
            },
            {
                DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON,
                fa ? L"\u0647\u0645\u06cc\u0634\u0647 \u0645\u062c\u0627\u0632"
                   : L"Allow &always"
            },
            {
                DIO_UI_TOOL_APPROVAL_DENY_BUTTON,
                fa ? L"\u0631\u062f \u06a9\u0631\u062f\u0646"
                   : L"&Deny"
            }
        };
        TASKDIALOGCONFIG dialog;
        ZeroMemory(&dialog, sizeof(dialog));
        dialog.cbSize = sizeof(dialog);
        dialog.hwndParent = ui->window;
        dialog.hInstance = ui->instance;
        dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                         TDF_SIZE_TO_CONTENT |
                         (fa ? TDF_RTL_LAYOUT : 0u);
        dialog.pszWindowTitle = fa
            ? L"\u062a\u0623\u06cc\u06cc\u062f \u0627\u0628\u0632\u0627\u0631 MCP"
            : L"Approve MCP tool";
        dialog.pszMainIcon = TD_WARNING_ICON;
        dialog.pszMainInstruction = fa
            ? L"\u0627\u06cc\u0646 \u0627\u0628\u0632\u0627\u0631 \u0627\u062c\u0627\u0632\u0647\u0654 \u0627\u062c\u0631\u0627 \u0645\u06cc\u200c\u062e\u0648\u0627\u0647\u062f"
            : L"This tool is requesting permission to run";
        dialog.pszContent = content;
        dialog.cButtons = (UINT)_countof(buttons);
        dialog.pButtons = buttons;
        dialog.nDefaultButton = DIO_UI_TOOL_APPROVAL_DEFAULT_BUTTON;
        dialog_result = TaskDialogIndirect(
            &dialog,
            &selected,
            NULL,
            NULL);
    }
    if (SUCCEEDED(dialog_result) &&
        selected == DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON) {
        always_saved = dio_remember_tool_always(
            ui,
            event->server,
            event->tool);
    }
    const DioUiToolDecision decision = dio_ui_resolve_tool_approval(
        SUCCEEDED(dialog_result),
        selected,
        always_saved);
    if (SUCCEEDED(dialog_result) &&
        selected == DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON &&
        !always_saved) {
        MessageBoxW(
            ui->window,
            fa
                ? L"\u0630\u062e\u06cc\u0631\u0647\u0654 \u0645\u062c\u0648\u0632 \u062f\u0627\u0626\u0645\u06cc \u0645\u0645\u06a9\u0646 \u0646\u0628\u0648\u062f\u061b \u062f\u0631\u062e\u0648\u0627\u0633\u062a \u0631\u062f \u0634\u062f."
                : L"The permanent permission could not be saved; the request was denied.",
            L"MCP",
            MB_OK | MB_ICONWARNING);
    }
    ZeroMemory(&command, sizeof(command));
    command.kind = DIO_UI_COMMAND_TOOL_APPROVAL;
    command.turn_id = event->turn_id;
    command.request_id = event->request_id;
    command.tool_decision = decision;
    if (ui->command != NULL) {
        ui->command(ui->command_context, &command);
    }
    SecureZeroMemory(content, sizeof(content));
}

static void dio_apply_event(DioUi *ui, const DioUiEvent *event) {
    bool geometry = true;
    bool layout = false;
    bool semantics = false;
    switch (event->kind) {
    case DIO_UI_EVENT_STATE: {
        const DioUiState previous_state = ui->model.state;
        geometry = false;
        layout = true;
        ui->model.state = event->state;
        if (ui->provider_required) {
            ui->provider_required = false;
            ui->model.content_bottom_inset = 0.0f;
            ShowWindow(ui->provider_button, SW_HIDE);
        }
        if (event->state != previous_state) {
            ui->user_message_open = false;
        }
        if (event->state != DIO_UI_ERROR) {
            ui->last_error_text[0] = L'\0';
        }
        if (event->state == DIO_UI_MUTED) {
            ui->paused = true;
        }
        if (ui->semantic_message_pending &&
            (event->state == DIO_UI_SPEAKING ||
             event->state == DIO_UI_FOLLOW_UP ||
             event->state == DIO_UI_IDLE ||
             event->state == DIO_UI_ERROR)) {
            ui->semantic_message_notify = true;
        }
        if ((event->state == DIO_UI_LISTENING &&
             previous_state != DIO_UI_LISTENING) ||
            (event->state == DIO_UI_ERROR &&
             previous_state != DIO_UI_ERROR)) {
            ui->interaction_suppressed = false;
        }
        if (event->state == DIO_UI_IDLE) {
            (void)KillTimer(ui->window, DIO_TIMER_HIDE);
            if (IsWindowVisible(ui->window) &&
                !ui->tray_menu_active &&
                !dio_transcript_reading(ui)) {
                (void)SetTimer(
                    ui->window,
                    DIO_TIMER_HIDE,
                    4000u,
                    NULL);
            }
        } else {
            (void)KillTimer(ui->window, DIO_TIMER_HIDE);
            if (!IsWindowVisible(ui->window)) {
                dio_show_overlay(ui, false);
            }
        }
        dio_set_status(ui);
        dio_update_semantics(ui);
        dio_update_tray_tip(ui);
        break;
    }
    case DIO_UI_EVENT_USER_TEXT:
        ui->pending_audio_output = false;
        geometry = dio_add_message(
            ui,
            DIO_MESSAGE_USER,
            event->text,
            ui->user_message_open,
            false);
        layout = true;
        ui->user_message_open = true;
        semantics = true;
        ui->semantic_message_pending = true;
        ui->semantic_message_notify = true;
        dio_show_overlay_for_change(ui, geometry);
        break;
    case DIO_UI_EVENT_ASSISTANT_TEXT:
        ui->user_message_open = false;
        geometry = dio_add_message(
            ui,
            DIO_MESSAGE_ASSISTANT,
            event->text,
            true,
            false);
        layout = true;
        semantics = true;
        ui->semantic_message_pending = true;
        dio_show_overlay_for_change(ui, geometry);
        break;
    case DIO_UI_EVENT_ASSISTANT_DELTA:
        ui->user_message_open = false;
        geometry = dio_add_message(
            ui,
            DIO_MESSAGE_ASSISTANT,
            event->text,
            true,
            true);
        layout = true;
        semantics = true;
        ui->semantic_message_pending = true;
        dio_show_overlay_for_change(ui, geometry);
        break;
    case DIO_UI_EVENT_CHIP:
        semantics = dio_attach_chip(
            ui,
            event->chip,
            event->text[0] != L'\0');
        geometry = false;
        dio_show_overlay_for_change(ui, geometry);
        break;
    case DIO_UI_EVENT_ERROR:
    case DIO_UI_EVENT_VAULT_REQUIRED:
    case DIO_UI_EVENT_PROVIDER_REQUIRED: {
        ui->provider_required =
            event->kind == DIO_UI_EVENT_PROVIDER_REQUIRED;
        ui->model.content_bottom_inset =
            ui->provider_required ? 52.0f : 0.0f;
        ShowWindow(
            ui->provider_button,
            ui->provider_required ? SW_SHOW : SW_HIDE);
        const wchar_t *error_text =
            event->text[0] != L'\0'
                ? event->text
                : (ui->settings.persian
                    ? L"\u062e\u0637\u0627\u06cc \u0646\u0627\u0634\u0646\u0627\u062e\u062a\u0647"
                    : L"Unknown error");
        const bool fresh_error =
            ui->model.state != DIO_UI_ERROR ||
            ui->last_error_text[0] == L'\0' ||
            wcscmp(
                ui->last_error_text,
                error_text) != 0;
        if (fresh_error) {
            ui->interaction_suppressed = false;
        }
        dio_copy_text(
            ui->last_error_text,
            _countof(ui->last_error_text),
            error_text);
        ui->pending_audio_output = false;
        ui->user_message_open = false;
        ui->model.state = DIO_UI_ERROR;
        if (fresh_error) {
            geometry = dio_add_message(
                ui,
                DIO_MESSAGE_ERROR,
                error_text,
                false,
                false);
            layout = true;
            semantics = true;
            ui->semantic_message_pending = true;
            ui->semantic_message_notify = true;
        } else {
            geometry = false;
        }
        dio_show_overlay_for_change(ui, geometry);
        if (event->kind == DIO_UI_EVENT_VAULT_REQUIRED) {
            (void)PostMessageW(
                ui->window,
                DIO_WM_VAULT_REQUIRED,
                0u,
                0u);
        }
        break;
    }
    case DIO_UI_EVENT_ANNOUNCEMENT:
        ui->interaction_suppressed = false;
        ui->last_error_text[0] = L'\0';
        ui->pending_audio_output = false;
        ui->user_message_open = false;
        ui->model.state = DIO_UI_ANNOUNCEMENT;
        geometry = dio_add_message(
            ui,
            DIO_MESSAGE_ANNOUNCEMENT,
            event->text,
            false,
            false);
        layout = true;
        semantics = true;
        ui->semantic_message_pending = true;
        ui->semantic_message_notify = true;
        dio_show_overlay(ui, false);
        break;
    case DIO_UI_EVENT_LEVEL:
        if (!dio_motion_enabled(ui)) {
            ui->model.level = 0.0f;
            return;
        }
        ui->model.level = event->value < 0.0f
            ? 0.0f
            : event->value > 1.0f
                ? 1.0f
                : event->value;
        geometry = false;
        break;
    case DIO_UI_EVENT_MODELS_DISCOVERED:
        if (IsWindow(ui->settings_window)) {
            (void)SendMessageW(
                ui->settings_window,
                DIO_WM_MODELS,
                (WPARAM)event->generation,
                (LPARAM)event->text);
        }
        return;
    case DIO_UI_EVENT_MCP_STATUS:
        dio_copy_text(
            ui->model.status,
            _countof(ui->model.status),
            event->text);
        geometry = false;
        dio_update_semantics(ui);
        break;
    case DIO_UI_EVENT_TOOL_APPROVAL_REQUIRED:
        dio_request_tool_approval(ui, event);
        return;
    case DIO_UI_EVENT_CLEAR:
        dio_clear_messages(&ui->model);
        ui->transcript_wheel_remainder = 0;
        ui->transcript_dragging = false;
        dio_view_set_scroll_hot(ui->view, false);
        if (GetCapture() == ui->messages_semantic) {
            (void)ReleaseCapture();
        }
        ui->user_message_open = false;
        ui->pending_audio_output = false;
        ui->semantic_message_pending = false;
        ui->semantic_message_notify = false;
        break;
    case DIO_UI_EVENT_SHOW:
        ui->interaction_suppressed = false;
        dio_show_overlay(ui, false);
        geometry = false;
        break;
    case DIO_UI_EVENT_HIDE:
        dio_hide_overlay(ui, false);
        geometry = false;
        break;
    default:
        return;
    }
    if (semantics && !geometry) {
        dio_update_semantics(ui);
    }
    dio_refresh(ui, geometry, layout);
}

static bool dio_event_replaceable(DioUiEventKind kind) {
    return
        kind == DIO_UI_EVENT_STATE ||
        kind == DIO_UI_EVENT_USER_TEXT ||
        kind == DIO_UI_EVENT_ASSISTANT_TEXT ||
        kind == DIO_UI_EVENT_LEVEL;
}

static bool dio_valid_event(const DioUiEvent *event) {
    if (event == NULL ||
        event->kind < DIO_UI_EVENT_STATE ||
        event->kind > DIO_UI_EVENT_HIDE) {
        return false;
    }
    if (event->kind == DIO_UI_EVENT_STATE &&
        (event->state < DIO_UI_LOADING || event->state > DIO_UI_ERROR)) {
        return false;
    }
    if (event->kind == DIO_UI_EVENT_CHIP &&
        (event->chip < DIO_CHIP_ACK || event->chip > DIO_CHIP_TTS)) {
        return false;
    }
    if (event->kind == DIO_UI_EVENT_TOOL_APPROVAL_REQUIRED &&
        (event->turn_id == 0u || event->request_id == 0u ||
         event->server[0] == L'\0' || event->tool[0] == L'\0')) {
        return false;
    }
    return
        event->kind != DIO_UI_EVENT_LEVEL ||
        (isfinite(event->value) != 0);
}

bool dio_ui_post(DioUi *ui, const DioUiEvent *event) {
    DioUiEvent copy;
    size_t offset;
    bool stored = false;

    if (ui == NULL ||
        !dio_valid_event(event) ||
        InterlockedCompareExchange(&ui->closing, 0, 0) != 0) {
        return false;
    }
    copy = *event;
    copy.text[_countof(copy.text) - 1u] = L'\0';
    EnterCriticalSection(&ui->event_lock);
    if (dio_event_replaceable(copy.kind)) {
        for (offset = ui->event_count; offset > 0u; --offset) {
            const size_t index =
                (ui->event_head + offset - 1u) % DIO_EVENT_QUEUE_CAP;
            if ((copy.kind == DIO_UI_EVENT_USER_TEXT ||
                 copy.kind == DIO_UI_EVENT_ASSISTANT_TEXT) &&
                ui->events[index].kind == DIO_UI_EVENT_CLEAR) {
                break;
            }
            if (ui->events[index].kind == copy.kind) {
                ui->events[index] = copy;
                stored = true;
                break;
            }
        }
    } else if (
        copy.kind == DIO_UI_EVENT_ASSISTANT_DELTA &&
        ui->event_count > 0u) {
        const size_t index =
            (ui->event_head + ui->event_count - 1u) %
                DIO_EVENT_QUEUE_CAP;
        if (ui->events[index].kind ==
            DIO_UI_EVENT_ASSISTANT_DELTA) {
            const size_t current =
                wcslen(ui->events[index].text);
            const size_t added = wcslen(copy.text);
            if (added < _countof(copy.text) - current) {
                (void)memcpy(
                    ui->events[index].text + current,
                    copy.text,
                    (added + 1u) * sizeof(copy.text[0]));
                stored = true;
            }
        }
    } else if (copy.kind == DIO_UI_EVENT_CHIP) {
        for (offset = ui->event_count; offset > 0u; --offset) {
            const size_t index =
                (ui->event_head + offset - 1u) % DIO_EVENT_QUEUE_CAP;
            if (ui->events[index].kind == DIO_UI_EVENT_CLEAR) {
                break;
            }
            if (ui->events[index].kind == DIO_UI_EVENT_CHIP &&
                ui->events[index].chip == copy.chip) {
                ui->events[index] = copy;
                stored = true;
                break;
            }
        }
    }
    if (!stored && ui->event_count < DIO_EVENT_QUEUE_CAP) {
        const size_t tail =
            (ui->event_head + ui->event_count) % DIO_EVENT_QUEUE_CAP;
        ui->events[tail] = copy;
        ui->event_count += 1u;
        stored = true;
    }
    LeaveCriticalSection(&ui->event_lock);
    if (stored) {
        (void)PostMessageW(ui->window, DIO_WM_EVENTS, 0u, 0);
    }
    return stored;
}

bool dio_ui_post_models(
    DioUi *ui,
    unsigned long long generation,
    const wchar_t *models) {
    if (ui == NULL || generation == 0u || models == NULL ||
        InterlockedCompareExchange(&ui->closing, 0, 0) != 0) {
        return false;
    }
    const size_t length = wcslen(models);
    if (length > 256u * 1024u) {
        return false;
    }
    wchar_t *copy = (wchar_t *)malloc((length + 1u) * sizeof(*copy));
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, models, (length + 1u) * sizeof(*copy));
    EnterCriticalSection(&ui->event_lock);
    free(ui->pending_models);
    ui->pending_models = copy;
    ui->pending_models_generation = generation;
    LeaveCriticalSection(&ui->event_lock);
    return PostMessageW(ui->window, DIO_WM_MODELS_READY, 0u, 0u) != 0;
}

static void dio_drain_events(DioUi *ui) {
    for (;;) {
        DioUiEvent event;
        bool available = false;
        EnterCriticalSection(&ui->event_lock);
        if (ui->event_count > 0u) {
            event = ui->events[ui->event_head];
            ui->event_head =
                (ui->event_head + 1u) % DIO_EVENT_QUEUE_CAP;
            ui->event_count -= 1u;
            available = true;
        }
        LeaveCriticalSection(&ui->event_lock);
        if (!available) {
            break;
        }
        dio_apply_event(ui, &event);
    }
}

static bool dio_accept_mouse_tray_toggle(
    DioUi *ui,
    ULONGLONG now) {
    const ULONGLONG previous =
        ui->last_mouse_tray_toggle;
    if (previous != 0u &&
        now >= previous &&
        now - previous <=
            (ULONGLONG)GetDoubleClickTime()) {
        return false;
    }
    ui->last_mouse_tray_toggle = now;
    return true;
}

static void dio_menu_command(DioUi *ui, UINT command) {
    switch (command) {
    case DIO_TRAY_SHOW:
        if (IsWindowVisible(ui->window)) {
            dio_hide_overlay(ui, true);
        } else {
            dio_show_overlay(ui, true);
        }
        break;
    case DIO_TRAY_PAUSE:
        ui->paused = !ui->paused;
        ui->last_error_text[0] = L'\0';
        ui->model.state = ui->paused ? DIO_UI_MUTED : DIO_UI_IDLE;
        dio_refresh(ui, true, true);
        dio_emit_command(ui, DIO_UI_COMMAND_SET_PAUSED, ui->paused);
        break;
    case DIO_TRAY_PTT:
        if (ui->provider_required) {
            (void)PostMessageW(
                ui->window,
                WM_COMMAND,
                DIO_TRAY_SETTINGS,
                0);
            break;
        }
        if (ui->paused) {
            ui->paused = false;
            dio_emit_command(ui, DIO_UI_COMMAND_SET_PAUSED, false);
        }
        ui->last_error_text[0] = L'\0';
        ui->model.state = DIO_UI_LISTENING;
        ui->interaction_suppressed = false;
        dio_show_overlay(ui, true);
        dio_refresh(ui, true, true);
        dio_emit_command(ui, DIO_UI_COMMAND_PUSH_TO_TALK, true);
        break;
    case DIO_TRAY_STOP:
        if (dio_can_cancel(ui->model.state)) {
            dio_emit_command(ui, DIO_UI_COMMAND_CANCEL, true);
        }
        break;
    case DIO_TRAY_SETTINGS:
        (void)PostMessageW(ui->window, WM_COMMAND, DIO_TRAY_SETTINGS, 0);
        break;
    case DIO_TRAY_EXIT:
        dio_emit_command(ui, DIO_UI_COMMAND_EXIT, true);
        dio_ui_request_exit(ui);
        break;
    default:
        break;
    }
}

static void dio_show_tray_menu(DioUi *ui) {
    HMENU menu;
    HBRUSH menu_background;
    HFONT menu_font;
    DioMenuItem items[7];
    size_t item_count = 0u;
    POINT cursor;
    UINT selected;
    const bool fa = ui->settings.persian;
    const bool was_visible =
        IsWindowVisible(ui->window) != FALSE;

    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    menu_background = dio_prepare_menu(
        menu,
        ui,
        ui->high_contrast);
    menu_font = CreateFontW(
        -MulDiv(14, (int)ui->dpi, 96),
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
        DEFAULT_PITCH,
        fa ? L"Vazirmatn" : L"Segoe UI");
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        DIO_TRAY_SHOW,
        was_visible
            ? (fa
                ? L"\u067e\u0646\u0647\u0627\u0646\u200c\u06a9\u0631\u062f\u0646 \u06af\u0641\u062a\u200c\u0648\u06af\u0648"
                : L"Hide conversation")
            : (fa
                ? L"\u0646\u0645\u0627\u06cc\u0634 \u06af\u0641\u062a\u200c\u0648\u06af\u0648"
                : L"Show conversation"),
        menu_font,
        ui->dpi,
        fa,
        false,
        false,
        menu_background != NULL);
    if (ui->provider_required) {
        (void)EnableMenuItem(
            menu,
            DIO_TRAY_PTT,
            MF_BYCOMMAND | MF_GRAYED);
    }
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        DIO_TRAY_PTT,
        fa ? L"\u06af\u0641\u062a\u200c\u0648\u06af\u0648\u06cc \u0641\u0648\u0631\u06cc" : L"Push-to-talk",
        menu_font,
        ui->dpi,
        fa,
        false,
        false,
        menu_background != NULL);
    if (dio_can_cancel(ui->model.state)) {
        (void)dio_append_menu_item(
            menu,
            &items[item_count++],
            ui,
            DIO_TRAY_STOP,
            fa ? L"\u062a\u0648\u0642\u0641" : L"Stop",
            menu_font,
            ui->dpi,
            fa,
            false,
            false,
            menu_background != NULL);
    }
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        DIO_TRAY_PAUSE,
        ui->paused
            ? (fa ? L"\u0627\u062f\u0627\u0645\u0647\u0654 \u0634\u0646\u06cc\u062f\u0646" : L"Resume listening")
            : (fa ? L"\u062a\u0648\u0642\u0641 \u0634\u0646\u06cc\u062f\u0646" : L"Pause listening"),
        menu_font,
        ui->dpi,
        fa,
        false,
        ui->paused,
        menu_background != NULL);
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        0u,
        NULL,
        menu_font,
        ui->dpi,
        fa,
        true,
        false,
        menu_background != NULL);
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        DIO_TRAY_SETTINGS,
        fa ? L"\u062a\u0646\u0638\u06cc\u0645\u0627\u062a" : L"Settings",
        menu_font,
        ui->dpi,
        fa,
        false,
        false,
        menu_background != NULL);
    (void)dio_append_menu_item(
        menu,
        &items[item_count++],
        ui,
        DIO_TRAY_EXIT,
        fa ? L"\u062e\u0631\u0648\u062c" : L"Exit",
        menu_font,
        ui->dpi,
        fa,
        false,
        false,
        menu_background != NULL);
    if (ui->smoke) {
        LRESULT menu_char_result = 0;
        const bool duplicate_initials =
            item_count == _countof(items) &&
            HiliteMenuItem(
                ui->window,
                menu,
                DIO_TRAY_STOP,
                MF_BYCOMMAND | MF_HILITE) != FALSE;
        ui->tray_menu_smoke_ok =
            menu_background == NULL ||
            (duplicate_initials &&
             items[2].text != NULL &&
             dio_menu_char(
                 items[2].text[0],
                 (LPARAM)menu,
                 &menu_char_result) &&
             LOWORD(menu_char_result) ==
                 item_count - 2u &&
             HIWORD(menu_char_result) == MNC_SELECT);
        (void)HiliteMenuItem(
            ui->window,
            menu,
            DIO_TRAY_STOP,
            MF_BYCOMMAND | MF_UNHILITE);
    }
    if (!GetCursorPos(&cursor)) {
        DestroyMenu(menu);
        if (menu_background != NULL) {
            DeleteObject(menu_background);
        }
        if (menu_font != NULL) {
            DeleteObject(menu_font);
        }
        return;
    }
    {
        RECT tray_bounds;
        if (dio_tray_icon_bounds(ui, &tray_bounds)) {
            cursor.x =
                (tray_bounds.left + tray_bounds.right) / 2;
            cursor.y =
                (tray_bounds.top + tray_bounds.bottom) / 2;
        } else if (ui->smoke) {
            ui->tray_menu_smoke_ok = false;
        }
    }
    ui->tray_menu_active = true;
    (void)KillTimer(ui->window, DIO_TIMER_HIDE);
    if (!dio_set_outside_input(ui, false)) {
        ui->tray_menu_active = false;
        if (was_visible &&
            ui->model.state == DIO_UI_IDLE) {
            (void)SetTimer(
                ui->window,
                DIO_TIMER_HIDE,
                4000u,
                NULL);
        }
        DestroyMenu(menu);
        if (menu_background != NULL) {
            DeleteObject(menu_background);
        }
        if (menu_font != NULL) {
            DeleteObject(menu_font);
        }
        return;
    }
    SetForegroundWindow(ui->window);
    if (ui->smoke) {
        (void)HiliteMenuItem(
            ui->window,
            menu,
            DIO_TRAY_SHOW,
            MF_BYCOMMAND | MF_HILITE);
    }
    selected = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY |
            TPM_WORKAREA |
            (fa
                ? TPM_RIGHTALIGN |
                    (menu_background == NULL
                        ? TPM_LAYOUTRTL
                        : 0u)
                : 0u),
        cursor.x,
        cursor.y,
        ui->window,
        NULL);
    DestroyMenu(menu);
    if (menu_background != NULL) {
        DeleteObject(menu_background);
    }
    if (menu_font != NULL) {
        DeleteObject(menu_font);
    }
    ui->tray_menu_active = false;
    if (!IsWindow(ui->window)) {
        return;
    }
    if (ui->tray_added) {
        (void)Shell_NotifyIconW(
            NIM_SETFOCUS,
            &ui->tray);
    }
    if (selected == DIO_TRAY_SHOW) {
        if (was_visible) {
            dio_hide_overlay(ui, true);
        } else {
            dio_show_overlay(ui, true);
        }
    } else if (selected != 0u) {
        dio_menu_command(ui, selected);
    }
    if (IsWindowVisible(ui->window) &&
        ui->model.state == DIO_UI_IDLE &&
        !dio_transcript_reading(ui)) {
        (void)SetTimer(
            ui->window,
            DIO_TIMER_HIDE,
            4000u,
            NULL);
    }
    (void)dio_sync_outside_input(ui);
    (void)PostMessageW(ui->window, WM_NULL, 0u, 0);
}

static void dio_set_appearance(
    DioUi *ui,
    bool high_contrast) {
    ui->high_contrast = high_contrast;
    if (ui->graphics != NULL) {
        cui_win32_context_set_appearance(
            ui->graphics,
            ui->scale,
            ui->high_contrast);
    }
    cui_win32_apply_window_theme(
        ui->window,
        &ui->theme,
        ui->high_contrast);
    ui->overlay_chrome_ok =
        dio_apply_overlay_chrome(ui);
    dio_sync_animation_timer(ui);
    InvalidateRect(ui->window, NULL, FALSE);
    InvalidateRect(ui->close_button, NULL, FALSE);
    InvalidateRect(ui->provider_button, NULL, FALSE);
}

static void dio_apply_appearance(DioUi *ui) {
    dio_set_appearance(
        ui,
        cui_win32_high_contrast());
}

static HFONT dio_settings_font(
    DioSettingsDialog *dialog) {
    return CreateFontW(
        -MulDiv(14, (int)dialog->dpi, 96),
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
        DEFAULT_PITCH,
        dialog->ui->settings.persian ? L"Vazirmatn" : L"Segoe UI");
}

static int dio_dialog_px(
    const DioSettingsDialog *dialog,
    int dip) {
    return MulDiv(dip, (int)dialog->dpi, 96);
}

static LRESULT CALLBACK dio_button_hover_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference) {
    if (message == WM_MOUSEMOVE && reference == 0u) {
        TRACKMOUSEEVENT tracking;
        ZeroMemory(&tracking, sizeof(tracking));
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        if (TrackMouseEvent(&tracking)) {
            (void)SetWindowSubclass(
                window,
                dio_button_hover_proc,
                subclass_id,
                1u);
            InvalidateRect(window, NULL, FALSE);
        }
    } else if (message == WM_MOUSELEAVE &&
               reference != 0u) {
        (void)SetWindowSubclass(
            window,
            dio_button_hover_proc,
            subclass_id,
            0u);
        InvalidateRect(window, NULL, FALSE);
    } else if (message == WM_NCDESTROY) {
        (void)RemoveWindowSubclass(
            window,
            dio_button_hover_proc,
            subclass_id);
    }
    return DefSubclassProc(
        window,
        message,
        wparam,
        lparam);
}

static bool dio_button_is_hot(HWND button) {
    DWORD_PTR reference = 0u;
    return GetWindowSubclass(
               button,
               dio_button_hover_proc,
               DIO_BUTTON_HOVER_SUBCLASS,
               &reference) &&
        reference != 0u;
}

static LRESULT CALLBACK dio_checkbox_visual_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference) {
    DioSettingsDialog *dialog =
        (DioSettingsDialog *)reference;

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        dio_draw_settings_checkbox(
            dialog,
            window,
            paint.hdc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        dio_draw_settings_checkbox(
            dialog,
            window,
            (HDC)wparam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case BM_SETCHECK:
    case BM_SETSTATE:
    case WM_ENABLE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(
            window,
            message,
            wparam,
            lparam);
        InvalidateRect(window, NULL, FALSE);
        return result;
    }
    case WM_NCDESTROY:
        (void)RemoveWindowSubclass(
            window,
            dio_checkbox_visual_proc,
            subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(
        window,
        message,
        wparam,
        lparam);
}

static LRESULT CALLBACK dio_edit_behavior_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference) {
    (void)reference;
    if (message == WM_GETDLGCODE) {
        LRESULT result = DefSubclassProc(
            window,
            message,
            wparam,
            lparam);
        if (wparam == VK_TAB ||
            wparam == VK_RETURN) {
            result &=
                ~(DLGC_WANTTAB |
                  DLGC_WANTALLKEYS |
                  DLGC_WANTMESSAGE);
        }
        return result;
    }
    if (message == WM_SETTEXT) {
        const LRESULT result = DefSubclassProc(
            window,
            message,
            wparam,
            lparam);
        (void)SendMessageW(
            GetParent(window),
            WM_COMMAND,
            MAKEWPARAM(
                GetDlgCtrlID(window),
                EN_CHANGE),
            (LPARAM)window);
        return result;
    }
    if (message == WM_NCDESTROY) {
        (void)RemoveWindowSubclass(
            window,
            dio_edit_behavior_proc,
            subclass_id);
    }
    return DefSubclassProc(
        window,
        message,
        wparam,
        lparam);
}

static void dio_center_settings_edit(
    DioSettingsDialog *dialog,
    HWND edit) {
    RECT formatting;
    TEXTMETRICW metrics;
    HDC dc;
    HGDIOBJ previous = NULL;
    const int padding =
        dio_dialog_px(dialog, 8);

    if (!GetClientRect(edit, &formatting)) {
        return;
    }
    dc = GetDC(edit);
    if (dc == NULL) {
        return;
    }
    if (dialog->font != NULL) {
        previous = SelectObject(dc, dialog->font);
    }
    if (GetTextMetricsW(dc, &metrics)) {
        const int content_height =
            metrics.tmHeight < formatting.bottom
                ? metrics.tmHeight
                : formatting.bottom;
        const int top =
            (formatting.bottom - content_height) / 2;
        formatting.left = padding;
        formatting.top = top;
        formatting.right -= padding;
        formatting.bottom = top + content_height;
        (void)SendMessageW(
            edit,
            EM_SETRECTNP,
            0u,
            (LPARAM)&formatting);
    }
    if (previous != NULL) {
        (void)SelectObject(dc, previous);
    }
    ReleaseDC(edit, dc);
}

static HWND dio_dialog_control(
    DioSettingsDialog *dialog,
    DWORD extended_style,
    const wchar_t *class_name,
    const wchar_t *text,
    DWORD style,
    int id) {
    HWND control = CreateWindowExW(
        extended_style,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        0,
        0,
        0,
        0,
        dialog->window,
        (HMENU)(INT_PTR)id,
        dialog->ui->instance,
        NULL);
    if (control != NULL && dialog->font != NULL) {
        (void)SendMessageW(
            control,
            WM_SETFONT,
            (WPARAM)dialog->font,
            TRUE);
    }
    if (control != NULL) {
        const bool edit =
            _wcsicmp(class_name, L"EDIT") == 0 &&
            (style & ES_WANTRETURN) == 0u;
        const bool checkbox =
            _wcsicmp(class_name, L"BUTTON") == 0 &&
            (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
        const bool hover =
            _wcsicmp(class_name, L"BUTTON") == 0 &&
            ((style & BS_TYPEMASK) == BS_OWNERDRAW ||
             checkbox);
        (void)SetWindowTheme(
            control,
            dialog->high_contrast
                ? L""
                : L"DarkMode_Explorer",
            NULL);
        if ((hover &&
             !SetWindowSubclass(
                 control,
                 dio_button_hover_proc,
                 DIO_BUTTON_HOVER_SUBCLASS,
                 0u)) ||
            (checkbox &&
             !SetWindowSubclass(
                 control,
                 dio_checkbox_visual_proc,
                 DIO_CHECKBOX_VISUAL_SUBCLASS,
                 (DWORD_PTR)dialog)) ||
            (edit &&
             !SetWindowSubclass(
                 control,
                 dio_edit_behavior_proc,
                 DIO_EDIT_BEHAVIOR_SUBCLASS,
                 (DWORD_PTR)dialog))) {
            DestroyWindow(control);
            control = NULL;
        }
    }
    return control;
}

static HWND dio_settings_label(
    DioSettingsDialog *dialog,
    DioSettingsPage page,
    const wchar_t *text) {
    HWND label;
    if (dialog->page_label_count >=
        _countof(dialog->page_labels)) {
        return NULL;
    }
    label = dio_dialog_control(
        dialog,
        dialog->ui->settings.persian
            ? WS_EX_RTLREADING
            : 0u,
        L"STATIC",
        text,
        (dialog->ui->settings.persian ? SS_RIGHT : SS_LEFT),
        -1);
    if (label != NULL) {
        dialog->page_labels[dialog->page_label_count] = label;
        dialog->page_label_pages[dialog->page_label_count] = page;
        dialog->page_label_count += 1u;
    }
    return label;
}

static void dio_combo_add(HWND combo, const wchar_t *text) {
    (void)SendMessageW(combo, CB_ADDSTRING, 0u, (LPARAM)text);
}

static void dio_move_settings_control(
    DioSettingsDialog *dialog,
    HWND control,
    DioSettingsControl slot,
    bool edit) {
    const CuiRect bounds =
        dio_settings_view_control_bounds(
            dialog->view,
            slot);
    const int inset =
        edit ? dio_dialog_px(dialog, 2) : 0;
    unsigned int state =
        DIO_SETTINGS_CONTROL_STATE_NONE;
    if (GetFocus() == control) {
        state |= DIO_SETTINGS_CONTROL_STATE_FOCUSED;
    }
    if (!IsWindowEnabled(control)) {
        state |= DIO_SETTINGS_CONTROL_STATE_DISABLED;
    }
    if ((dialog->invalid_controls &
         (1u << (unsigned int)slot)) != 0u) {
        state |= DIO_SETTINGS_CONTROL_STATE_INVALID;
    }
    dio_settings_view_set_control_state(
        dialog->view,
        slot,
        state);
    if (dio_settings_view_control_visible(
            dialog->view,
            slot)) {
        (void)MoveWindow(
            control,
            dio_dialog_px(dialog, (int)bounds.x) + inset,
            dio_dialog_px(dialog, (int)bounds.y) + inset,
            dio_dialog_px(dialog, (int)bounds.width) - inset * 2,
            dio_dialog_px(dialog, (int)bounds.height) - inset * 2,
            TRUE);
    } else {
        (void)MoveWindow(
            control,
            -32768,
            -32768,
            dio_dialog_px(dialog, (int)bounds.width) - inset * 2,
            dio_dialog_px(dialog, (int)bounds.height) - inset * 2,
            FALSE);
    }
    if (edit) {
        dio_center_settings_edit(
            dialog,
            control);
    }
    ShowWindow(control, SW_SHOWNOACTIVATE);
}

static void dio_settings_place(
    DioSettingsDialog *dialog,
    HWND control,
    int x,
    int y,
    int width,
    int height,
    bool visible) {
    const int base_y = y;
    if (control == NULL) {
        return;
    }
    if (dialog->page != DIO_SETTINGS_PAGE_GENERAL &&
        control != dialog->tabs) {
        RECT client;
        const int visible_height =
            control == dialog->model ||
            control == dialog->reasoning ||
            control == dialog->service_tier ||
            control == dialog->mcp_transport
                ? 34
                : height;
        GetClientRect(dialog->window, &client);
        const int footer_y = MulDiv(
            client.bottom,
            96,
            (int)dialog->dpi) - 64;
        (void)SetPropW(
            control,
            DIO_SETTINGS_BASE_TOP,
            (HANDLE)(INT_PTR)(base_y + 1));
        (void)SetPropW(
            control,
            DIO_SETTINGS_BASE_BOTTOM,
            (HANDLE)(INT_PTR)(base_y + visible_height + 1));
        y -= dialog->scroll_y;
        if (visible &&
            (y < 122 || y + visible_height > footer_y)) {
            x = -32768;
            y = -32768;
        }
    }
    (void)MoveWindow(
        control,
        dio_dialog_px(dialog, x),
        dio_dialog_px(dialog, y),
        dio_dialog_px(dialog, width),
        dio_dialog_px(dialog, height),
        TRUE);
    (void)SetWindowRgn(control, NULL, TRUE);
    ShowWindow(control, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

static int dio_settings_page_scroll_max(
    const DioSettingsDialog *dialog,
    int width,
    int viewport) {
    int content_bottom;
    if (dialog->page == DIO_SETTINGS_PAGE_GENERAL) {
        return (int)ceilf(
            dio_settings_view_scroll_max((float)viewport));
    }
    if (dialog->page == DIO_SETTINGS_PAGE_MODEL) {
        content_bottom = 574;
    } else if (dialog->page == DIO_SETTINGS_PAGE_SYSTEM_PROMPT) {
        content_bottom = 242;
    } else {
        content_bottom = width < 460 ? 724 : 592;
    }
    content_bottom -= viewport - 64 - 12;
    return content_bottom > 0 ? content_bottom : 0;
}

static void dio_layout_settings_pages(
    DioSettingsDialog *dialog,
    int width,
    int height) {
    const bool general = dialog->page == DIO_SETTINGS_PAGE_GENERAL;
    const bool model = dialog->page == DIO_SETTINGS_PAGE_MODEL;
    const bool prompt = dialog->page == DIO_SETTINGS_PAGE_SYSTEM_PROMPT;
    const bool tools = dialog->page == DIO_SETTINGS_PAGE_TOOLS;
    const bool mcp_stdio = tools &&
        SendMessageW(
            dialog->mcp_transport,
            CB_GETCURSEL,
            0u,
            0u) == 1;
    const bool rtl = dialog->ui->settings.persian;
    const int footer_y = height - 64;
    const int label_width = width < 460 ? 120 : 144;
    const int field_width = width - label_width - 60;
    const int label_x = rtl ? width - label_width - 24 : 24;
    const int field_x = rtl ? 24 : label_width + 36;
    static const int rows[] = {144, 194, 244, 294, 344, 394, 444};
    HWND model_controls[] = {
        dialog->base_url,
        dialog->api_key,
        dialog->model,
        dialog->reasoning,
        dialog->service_tier,
        dialog->vault_password,
        dialog->vault_confirm};
    HWND general_controls[] = {
        dialog->locale,
        dialog->microphone,
        dialog->silence,
        dialog->follow_up_enabled,
        dialog->follow_up,
        dialog->reduced_motion};
    size_t index;

    dio_settings_place(
        dialog,
        dialog->tabs,
        16,
        84,
        width - 32,
        36,
        true);
    for (index = 0u; index < _countof(general_controls); ++index) {
        ShowWindow(
            general_controls[index],
            general ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    for (index = 0u; index < dialog->page_label_count; ++index) {
        bool visible =
            dialog->page_label_pages[index] == dialog->page;
        int x = 24;
        int y = 136;
        int item_width = width - 48;
        int item_height = 40;
        if (dialog->page_label_pages[index] == DIO_SETTINGS_PAGE_MODEL) {
            size_t model_label = 0u;
            size_t scan;
            for (scan = 0u; scan < index; ++scan) {
                if (dialog->page_label_pages[scan] == DIO_SETTINGS_PAGE_MODEL) {
                    model_label += 1u;
                }
            }
            x = label_x;
            y = rows[model_label < _countof(rows) ? model_label : 0u] + 6;
            item_width = label_width;
            item_height = 24;
        } else if (dialog->page_label_pages[index] ==
                   DIO_SETTINGS_PAGE_SYSTEM_PROMPT) {
            x = rtl ? 144 : 24;
            item_width = width - 168;
        } else if (dialog->page_label_pages[index] == DIO_SETTINGS_PAGE_TOOLS) {
            size_t tool_label = 0u;
            size_t scan;
            for (scan = 0u; scan < index; ++scan) {
                if (dialog->page_label_pages[scan] == DIO_SETTINGS_PAGE_TOOLS) {
                    tool_label += 1u;
                }
            }
            if (tool_label == 0u) {
                y = 136;
            } else {
                const bool stacked = width < 460;
                const int form_x = stacked ? 24 : (rtl ? 24 : 220);
                const int form_width = stacked ? width - 48 : width - 244;
                x = form_x;
                y = (stacked ? 286 : 158) + (int)(tool_label - 1u) * 58;
                item_width = form_width;
                item_height = 22;
            }
            if ((dialog->page_labels[index] == dialog->mcp_arguments_label ||
                 dialog->page_labels[index] == dialog->mcp_cwd_label) &&
                !mcp_stdio) {
                visible = false;
            }
        }
        dio_settings_place(
            dialog,
            dialog->page_labels[index],
            x,
            y,
            item_width,
            item_height,
            visible);
    }
    for (index = 0u; index < _countof(model_controls); ++index) {
        dio_settings_place(
            dialog,
            model_controls[index],
            field_x,
            rows[index],
            field_width,
            index == 2u || index == 3u || index == 4u ? 160 : 34,
            model);
        if (index != 2u && index != 3u && index != 4u) {
            dio_center_settings_edit(dialog, model_controls[index]);
        }
    }
    dio_settings_place(
        dialog,
        dialog->model_status,
        field_x,
        122,
        field_width,
        20,
        model);
    dio_settings_place(dialog, dialog->vault_action, field_x, 488, 104, 34, model);
    dio_settings_place(dialog, dialog->vault_change, field_x + 112, 488, 132, 34, model);
    dio_settings_place(dialog, dialog->vault_reset, field_x, 530, 132, 32, model);
    dio_settings_place(
        dialog,
        dialog->prompt_reset,
        rtl ? 24 : width - 124,
        142,
        100,
        34,
        prompt);
    dio_settings_place(
        dialog,
        dialog->system_prompt,
        24,
        190,
        width - 48,
        footer_y - 214 > 40 ? footer_y - 214 : 40,
        prompt);

    {
        const bool stacked = width < 460;
        const int list_x = stacked ? 24 : (rtl ? width - 204 : 24);
        const int list_y = 160;
        const int list_width = stacked ? width - 48 : 180;
        const int list_height = stacked
            ? footer_y - 134 < 34
                ? 34
                : footer_y - 134 > 108
                    ? 108
                    : footer_y - 134
            : footer_y - 184 > 60
                ? footer_y - 184
                : 60;
        const int form_x = stacked ? 24 : (rtl ? 24 : 220);
        const int form_width = stacked ? width - 48 : width - 244;
        const int form_y = stacked ? 310 : 178;
        dio_settings_place(dialog, dialog->mcp_list, list_x, list_y, list_width, list_height, tools);
        dio_settings_place(dialog, dialog->mcp_transport, form_x, form_y, 142, 150, tools);
        dio_settings_place(dialog, dialog->mcp_enabled, form_x + 150, form_y + 2, 110, 30, tools);
        dio_settings_place(dialog, dialog->mcp_name, form_x, form_y + 58, form_width, 34, tools);
        dio_settings_place(dialog, dialog->mcp_url, form_x, form_y + 116, form_width, 34, tools);
        dio_settings_place(dialog, dialog->mcp_arguments, form_x, form_y + 174, form_width, 34, tools && mcp_stdio);
        dio_settings_place(dialog, dialog->mcp_cwd, form_x, form_y + 232, form_width, 34, tools && mcp_stdio);
        dio_settings_place(dialog, dialog->mcp_token, form_x, form_y + 290, form_width, 34, tools && !mcp_stdio);
        dio_settings_place(dialog, dialog->mcp_environment, form_x, form_y + 290, form_width, 68, tools && mcp_stdio);
        dio_settings_place(dialog, dialog->mcp_add, form_x, form_y + 368, 130, 34, tools);
        dio_settings_place(dialog, dialog->mcp_remove, form_x + 138, form_y + 368, 92, 34, tools);
        dio_center_settings_edit(dialog, dialog->mcp_name);
        dio_center_settings_edit(dialog, dialog->mcp_url);
        dio_center_settings_edit(dialog, dialog->mcp_arguments);
        dio_center_settings_edit(dialog, dialog->mcp_cwd);
        dio_center_settings_edit(dialog, dialog->mcp_token);
    }
}

static void dio_layout_settings(DioSettingsDialog *dialog) {
    RECT client;
    SCROLLINFO scroll;
    int viewport;

    if (dialog->view == NULL || dialog->graphics == NULL) {
        return;
    }
    GetClientRect(dialog->window, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }
    viewport = MulDiv(client.bottom, 96, (int)dialog->dpi);
    if (viewport < 1) {
        viewport = 1;
    }
    dialog->scroll_max = dio_settings_page_scroll_max(
        dialog,
        MulDiv(client.right, 96, (int)dialog->dpi),
        viewport);
    if (dialog->scroll_y > dialog->scroll_max) {
        dialog->scroll_y = dialog->scroll_max;
    }
    if (dialog->scroll_y < 0) {
        dialog->scroll_y = 0;
    }
    ZeroMemory(&scroll, sizeof(scroll));
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    scroll.nMax =
        dialog->scroll_max + viewport - 1;
    scroll.nPage = (UINT)viewport;
    scroll.nPos = dialog->scroll_y;
    (void)SetScrollInfo(
        dialog->window,
        SB_VERT,
        &scroll,
        TRUE);
    GetClientRect(dialog->window, &client);
    dio_settings_view_set_page(dialog->view, dialog->page);
    if (!dio_settings_view_prepare(
            dialog->view,
            dialog->ui->settings.persian,
            (float)client.right / dialog->scale,
            (float)viewport,
            (float)dialog->scroll_y)) {
        return;
    }
    (void)dio_settings_view_set_choice_text(
        dialog->view,
        DIO_SETTINGS_CONTROL_LOCALE,
        dialog->locale_index == 0
            ? L"\u0641\u0627\u0631\u0633\u06cc"
            : L"English");
    if (dialog->microphone_index <
        dialog->microphone_count) {
        (void)dio_settings_view_set_choice_text(
            dialog->view,
            DIO_SETTINGS_CONTROL_MICROPHONE,
            dialog->microphones[
                dialog->microphone_index]);
    }
    dio_move_settings_control(
        dialog,
        dialog->locale,
        DIO_SETTINGS_CONTROL_LOCALE,
        false);
    dio_move_settings_control(
        dialog,
        dialog->microphone,
        DIO_SETTINGS_CONTROL_MICROPHONE,
        false);
    dio_move_settings_control(
        dialog,
        dialog->silence,
        DIO_SETTINGS_CONTROL_SILENCE,
        true);
    dio_move_settings_control(
        dialog,
        dialog->follow_up_enabled,
        DIO_SETTINGS_CONTROL_FOLLOW_UP_ENABLED,
        false);
    dio_move_settings_control(
        dialog,
        dialog->follow_up,
        DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS,
        true);
    dio_move_settings_control(
        dialog,
        dialog->reduced_motion,
        DIO_SETTINGS_CONTROL_REDUCED_MOTION,
        false);
    dio_move_settings_control(
        dialog,
        dialog->cancel,
        DIO_SETTINGS_CONTROL_CANCEL,
        false);
    dio_move_settings_control(
        dialog,
        dialog->save,
        DIO_SETTINGS_CONTROL_SAVE,
        false);
    dio_layout_settings_pages(
        dialog,
        MulDiv(client.right, 96, (int)dialog->dpi),
        viewport);
    cui_win32_context_resize(
        dialog->graphics,
        (unsigned int)client.right,
        (unsigned int)client.bottom);
    InvalidateRect(dialog->window, NULL, FALSE);
}

static void dio_scroll_settings(
    DioSettingsDialog *dialog,
    int position) {
    if (position < 0) {
        position = 0;
    }
    if (position > dialog->scroll_max) {
        position = dialog->scroll_max;
    }
    if (position == dialog->scroll_y) {
        return;
    }
    dialog->scroll_y = position;
    dio_layout_settings(dialog);
}

static bool dio_settings_page_key(
    DioSettingsDialog *dialog,
    WPARAM key) {
    SCROLLINFO scroll;
    int position;

    if (key != VK_PRIOR && key != VK_NEXT) {
        return false;
    }
    ZeroMemory(&scroll, sizeof(scroll));
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_PAGE;
    if (!GetScrollInfo(
            dialog->window,
            SB_VERT,
            &scroll)) {
        return false;
    }
    position = dialog->scroll_y;
    position += key == VK_NEXT
        ? (int)scroll.nPage
        : -(int)scroll.nPage;
    dio_scroll_settings(dialog, position);
    return true;
}

static void dio_ensure_settings_control_visible(
    DioSettingsDialog *dialog,
    HWND control) {
    CuiRect bounds;
    CuiRect viewport;
    DioSettingsControl slot;
    const int margin =
        dio_dialog_px(dialog, 8);
    int control_top;
    int control_bottom;
    int viewport_top;
    int viewport_bottom;

    if (!IsChild(dialog->window, control) ||
        control == dialog->save ||
        control == dialog->cancel) {
        return;
    }
    if (dialog->page != DIO_SETTINGS_PAGE_GENERAL) {
        RECT client;
        const HANDLE top_property = GetPropW(
            control,
            DIO_SETTINGS_BASE_TOP);
        const HANDLE bottom_property = GetPropW(
            control,
            DIO_SETTINGS_BASE_BOTTOM);
        const int child_margin = 8;
        const int child_viewport_top = 122 + child_margin;
        int child_viewport_bottom;
        if (control == dialog->tabs ||
            top_property == NULL || bottom_property == NULL ||
            !GetClientRect(dialog->window, &client)) {
            return;
        }
        const int child_top =
            (int)(INT_PTR)top_property - 1 - dialog->scroll_y;
        const int child_bottom =
            (int)(INT_PTR)bottom_property - 1 - dialog->scroll_y;
        child_viewport_bottom = MulDiv(
            client.bottom,
            96,
            (int)dialog->dpi) - 64 - child_margin;
        if (child_top < child_viewport_top) {
            dio_scroll_settings(
                dialog,
                dialog->scroll_y + child_top - child_viewport_top);
        } else if (child_bottom > child_viewport_bottom) {
            dio_scroll_settings(
                dialog,
                dialog->scroll_y + child_bottom - child_viewport_bottom);
        }
        return;
    }
    switch (GetDlgCtrlID(control)) {
    case DIO_SETTINGS_LOCALE:
        slot = DIO_SETTINGS_CONTROL_LOCALE;
        break;
    case DIO_SETTINGS_MICROPHONE:
        slot = DIO_SETTINGS_CONTROL_MICROPHONE;
        break;
    case DIO_SETTINGS_SILENCE:
        slot = DIO_SETTINGS_CONTROL_SILENCE;
        break;
    case DIO_SETTINGS_FOLLOW_UP_ENABLED:
        slot = DIO_SETTINGS_CONTROL_FOLLOW_UP_ENABLED;
        break;
    case DIO_SETTINGS_FOLLOW_UP:
        slot = DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS;
        break;
    case DIO_SETTINGS_REDUCED_MOTION:
        slot = DIO_SETTINGS_CONTROL_REDUCED_MOTION;
        break;
    default:
        return;
    }
    bounds = dio_settings_view_control_bounds(
        dialog->view,
        slot);
    viewport =
        dio_settings_view_scroll_viewport(
            dialog->view);
    viewport_top =
        dio_dialog_px(
            dialog,
            (int)viewport.y) +
        margin;
    viewport_bottom =
        dio_dialog_px(
            dialog,
            (int)(viewport.y + viewport.height)) -
        margin;
    control_top =
        dio_dialog_px(dialog, (int)bounds.y);
    control_bottom =
        dio_dialog_px(
            dialog,
            (int)(bounds.y + bounds.height));
    if (control_top < viewport_top) {
        dio_scroll_settings(
            dialog,
            dialog->scroll_y +
                MulDiv(
                    control_top - viewport_top,
                    96,
                    (int)dialog->dpi));
    } else if (control_bottom > viewport_bottom) {
        dio_scroll_settings(
            dialog,
            dialog->scroll_y +
                MulDiv(
                    control_bottom - viewport_bottom,
                    96,
                    (int)dialog->dpi));
    }
}

static void dio_settings_error(
    DioSettingsDialog *dialog,
    const wchar_t *message,
    bool persian) {
    if (dialog->ui->settings_smoke) {
        return;
    }
    MessageBoxW(
        dialog->window,
        message,
        persian
            ? L"\u0630\u062e\u06cc\u0631\u0647\u0654 \u062a\u0646\u0638\u06cc\u0645\u0627\u062a"
            : L"Save settings",
        MB_OK | MB_ICONERROR);
}

static void dio_localize_number(
    wchar_t *text,
    bool persian) {
    size_t index;
    if (!persian || text == NULL) {
        return;
    }
    for (index = 0u; text[index] != L'\0'; ++index) {
        if (text[index] >= L'0' && text[index] <= L'9') {
            text[index] =
                (wchar_t)(L'\u06f0' + text[index] - L'0');
        } else if (text[index] == L'.') {
            text[index] = L'\u066b';
        }
    }
}

static bool dio_parse_settings_seconds(
    const wchar_t *source,
    double minimum,
    double maximum,
    unsigned int *milliseconds) {
    wchar_t text[32];
    wchar_t *end;
    double seconds;
    size_t index;

    if (source == NULL ||
        source[0] == L'\0' ||
        wcscpy_s(text, _countof(text), source) != 0) {
        return false;
    }
    for (index = 0u; text[index] != L'\0'; ++index) {
        if (text[index] >= L'\u06f0' &&
            text[index] <= L'\u06f9') {
            text[index] =
                (wchar_t)(L'0' + text[index] - L'\u06f0');
        } else if (text[index] >= L'\u0660' &&
                   text[index] <= L'\u0669') {
            text[index] =
                (wchar_t)(L'0' + text[index] - L'\u0660');
        } else if (text[index] == L',' ||
                   text[index] == L'\u066b') {
            text[index] = L'.';
        }
    }
    seconds = wcstod(text, &end);
    while (*end == L' ' || *end == L'\t') {
        ++end;
    }
    if (*end != L'\0' ||
        !isfinite(seconds) ||
        seconds < minimum ||
        seconds > maximum) {
        return false;
    }
    *milliseconds =
        (unsigned int)lround(seconds * 1000.0);
    return true;
}

static bool dio_settings_seconds(
    HWND edit,
    double minimum,
    double maximum,
    unsigned int *milliseconds) {
    wchar_t text[32];
    return
        GetWindowTextW(
            edit,
            text,
            (int)_countof(text)) > 0 &&
        dio_parse_settings_seconds(
            text,
            minimum,
            maximum,
            milliseconds);
}

static bool dio_settings_number_policy(void) {
    unsigned int milliseconds = 0u;
    return
        dio_parse_settings_seconds(
            L"\u06f1\u066b\u06f5",
            0.1,
            60.0,
            &milliseconds) &&
        milliseconds == 1500u &&
        dio_parse_settings_seconds(
            L"\u0666\u0660",
            1.0,
            60.0,
            &milliseconds) &&
        milliseconds == 60000u &&
        !dio_parse_settings_seconds(
            L"12x",
            0.1,
            60.0,
            &milliseconds);
}

static bool dio_model_endpoint_valid(const wchar_t *endpoint) {
    URL_COMPONENTSW parts;
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = (DWORD)-1;
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (endpoint == NULL || endpoint[0] == L'\0' ||
        !WinHttpCrackUrl(endpoint, 0u, 0u, &parts) ||
        parts.dwHostNameLength == 0u) {
        return false;
    }
    if (parts.nScheme == INTERNET_SCHEME_HTTPS) {
        return true;
    }
    return parts.nScheme == INTERNET_SCHEME_HTTP &&
        ((parts.dwHostNameLength == 9u &&
          (_wcsnicmp(parts.lpszHostName, L"localhost", 9u) == 0 ||
           wcsncmp(parts.lpszHostName, L"127.0.0.1", 9u) == 0)) ||
         (parts.dwHostNameLength == 3u &&
          wcsncmp(parts.lpszHostName, L"::1", 3u) == 0) ||
         (parts.dwHostNameLength == 5u &&
          wcsncmp(parts.lpszHostName, L"[::1]", 5u) == 0));
}

static bool dio_model_endpoint_secret_policy(void) {
    DioAgentProfile profile;
    wchar_t detached[DIO_AGENT_SECRET_NAME_CAP];
    ZeroMemory(&profile, sizeof(profile));
    ZeroMemory(detached, sizeof(detached));
    (void)wcscpy_s(
        profile.api_key_secret_id,
        _countof(profile.api_key_secret_id),
        L"provider.api_key");
    const bool dirty = dio_agent_profile_detach_api_key_secret(
        &profile,
        detached,
        _countof(detached));
    return dirty && profile.api_key_secret_id[0] == L'\0' &&
        wcscmp(detached, L"provider.api_key") == 0;
}

static bool dio_model_endpoint_policy(void) {
    return
        dio_model_endpoint_valid(L"https://provider.example/v1") &&
        dio_model_endpoint_valid(L"http://127.0.0.1:8080/v1") &&
        dio_model_endpoint_valid(L"http://localhost/v1") &&
        !dio_model_endpoint_valid(L"http://provider.example/v1") &&
        !dio_model_endpoint_valid(L"not-a-url") &&
        dio_model_endpoint_secret_policy();
}

static void dio_schedule_model_discovery(
    DioSettingsDialog *dialog) {
    wchar_t endpoint[DIO_AGENT_BASE_URL_CAP];
    if (dialog == NULL || dialog->base_url == NULL) {
        return;
    }
    (void)GetWindowTextW(
        dialog->base_url,
        endpoint,
        (int)_countof(endpoint));
    dialog->discovery_generation += 1u;
    (void)KillTimer(dialog->window, DIO_TIMER_MODEL_DISCOVERY);
    if (dio_model_endpoint_valid(endpoint) &&
        dialog->ui->command != NULL) {
        (void)SetWindowTextW(
            dialog->model_status,
            dialog->ui->settings.persian
                ? L"در حال دریافت فهرست مدل‌ها…"
                : L"Loading model catalog...");
        (void)SetTimer(
            dialog->window,
            DIO_TIMER_MODEL_DISCOVERY,
            600u,
            NULL);
    } else {
        (void)SetWindowTextW(
            dialog->model_status,
            endpoint[0] == L'\0'
                ? (dialog->ui->settings.persian
                    ? L"ابتدا آدرس ارائه‌دهنده را وارد کنید."
                    : L"Enter a provider endpoint first.")
                : (dialog->ui->settings.persian
                    ? L"یک آدرس معتبر HTTPS یا loopback HTTP وارد کنید."
                    : L"Enter a valid HTTPS or loopback HTTP endpoint."));
    }
}

static void dio_request_model_discovery(
    DioSettingsDialog *dialog) {
    DioUiCommand command;
    const bool anonymous = dialog->discovery_anonymous;
    if (dialog->ui->command == NULL) {
        return;
    }
    ZeroMemory(&command, sizeof(command));
    command.kind = DIO_UI_COMMAND_DISCOVER_MODELS;
    command.generation = dialog->discovery_generation;
    dialog->discovery_anonymous = false;
    (void)GetWindowTextW(
        dialog->base_url,
        command.endpoint,
        (int)_countof(command.endpoint));
    if (!anonymous && dialog->ui->vault.unlocked) {
        (void)GetWindowTextW(
            dialog->api_key,
            command.api_key,
            (int)_countof(command.api_key));
    }
    dialog->ui->command(
        dialog->ui->command_context,
        &command);
    SecureZeroMemory(command.api_key, sizeof(command.api_key));
    if (anonymous && dialog->ui->vault.unlocked &&
        GetWindowTextLengthW(dialog->api_key) > 0) {
        dio_schedule_model_discovery(dialog);
    }
}

static void dio_apply_discovered_models(
    DioSettingsDialog *dialog,
    unsigned long long generation,
    const wchar_t *models) {
    wchar_t current[DIO_AGENT_MODEL_CAP];
    const wchar_t *line;
    if (generation != dialog->discovery_generation ||
        models == NULL) {
        return;
    }
    if (wcsncmp(models, L"!auth", 5u) == 0) {
        (void)SetWindowTextW(
            dialog->model_status,
            dialog->ui->settings.persian
                ? L"برای دریافت مدل‌ها کلید API لازم است."
                : L"An API key is required to list models.");
        return;
    }
    if (wcscmp(models, L"!timeout") == 0) {
        (void)SetWindowTextW(
            dialog->model_status,
            dialog->ui->settings.persian
                ? L"مهلت ۱۰ ثانیه‌ای دریافت مدل‌ها تمام شد؛ Model ID دستی مجاز است."
                : L"Model discovery timed out after 10 seconds; a custom model ID is still allowed.");
        return;
    }
    if (models[0] == L'!') {
        (void)SetWindowTextW(
            dialog->model_status,
            dialog->ui->settings.persian
                ? L"دریافت مدل‌ها ناموفق بود؛ Model ID دستی مجاز است."
                : L"Catalog failed; a custom model ID is still allowed.");
        return;
    }
    (void)GetWindowTextW(
        dialog->model,
        current,
        (int)_countof(current));
    (void)SendMessageW(dialog->model, CB_RESETCONTENT, 0u, 0u);
    line = models;
    while (*line != L'\0') {
        const wchar_t *end = wcschr(line, L'\n');
        const size_t length = end != NULL
            ? (size_t)(end - line)
            : wcslen(line);
        wchar_t name[DIO_AGENT_MODEL_CAP];
        size_t trimmed = length;
        while (trimmed > 0u && line[trimmed - 1u] == L'\r') {
            trimmed -= 1u;
        }
        if (trimmed > 0u && trimmed < _countof(name)) {
            memcpy(name, line, trimmed * sizeof(*name));
            name[trimmed] = L'\0';
            if (SendMessageW(
                    dialog->model,
                    CB_FINDSTRINGEXACT,
                    (WPARAM)-1,
                    (LPARAM)name) == CB_ERR) {
                (void)SendMessageW(
                    dialog->model,
                    CB_ADDSTRING,
                    0u,
                    (LPARAM)name);
            }
        }
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
    (void)SetWindowTextW(dialog->model, current);
    (void)SetWindowTextW(
        dialog->model_status,
        models[0] != L'\0'
            ? (dialog->ui->settings.persian
                ? L"فهرست مدل‌ها به‌روز شد."
                : L"Model catalog updated.")
            : (dialog->ui->settings.persian
                ? L"مدلی برنگشت؛ Model ID دستی مجاز است."
                : L"No models returned; a custom model ID is allowed."));
}

static bool dio_model_stale_generation_policy(
    DioSettingsDialog *dialog) {
    wchar_t status[256];
    wchar_t model[DIO_AGENT_MODEL_CAP];
    wchar_t after_status[256];
    wchar_t after_model[DIO_AGENT_MODEL_CAP];
    const unsigned long long generation = dialog->discovery_generation;
    const LRESULT count = SendMessageW(
        dialog->model,
        CB_GETCOUNT,
        0u,
        0u);
    (void)GetWindowTextW(
        dialog->model_status,
        status,
        (int)_countof(status));
    (void)GetWindowTextW(
        dialog->model,
        model,
        (int)_countof(model));
    dialog->discovery_generation = 7u;
    dio_apply_discovered_models(dialog, 6u, L"stale-model\n");
    (void)GetWindowTextW(
        dialog->model_status,
        after_status,
        (int)_countof(after_status));
    (void)GetWindowTextW(
        dialog->model,
        after_model,
        (int)_countof(after_model));
    const bool unchanged =
        SendMessageW(dialog->model, CB_GETCOUNT, 0u, 0u) == count &&
        wcscmp(after_status, status) == 0 &&
        wcscmp(after_model, model) == 0;
    dialog->discovery_generation = generation;
    return unchanged;
}

static bool dio_model_timeout_policy(DioSettingsDialog *dialog) {
    wchar_t previous[256];
    wchar_t status[256];
    const unsigned long long generation = dialog->discovery_generation;
    (void)GetWindowTextW(
        dialog->model_status,
        previous,
        (int)_countof(previous));
    dialog->discovery_generation = 9u;
    dio_apply_discovered_models(dialog, 9u, L"!timeout");
    (void)GetWindowTextW(
        dialog->model_status,
        status,
        (int)_countof(status));
    const bool valid = wcscmp(
        status,
        dialog->ui->settings.persian
            ? L"مهلت ۱۰ ثانیه‌ای دریافت مدل‌ها تمام شد؛ Model ID دستی مجاز است."
            : L"Model discovery timed out after 10 seconds; a custom model ID is still allowed.") == 0;
    (void)SetWindowTextW(dialog->model_status, previous);
    dialog->discovery_generation = generation;
    return valid;
}

static bool dio_mcp_stdio_selected(const DioSettingsDialog *dialog) {
    return SendMessageW(
        dialog->mcp_transport,
        CB_GETCURSEL,
        0u,
        0u) == 1;
}

static void dio_mcp_update_transport(DioSettingsDialog *dialog) {
    const bool stdio = dio_mcp_stdio_selected(dialog);
    const bool fa = dialog->ui->settings.persian;
    (void)SetWindowTextW(
        dialog->mcp_target_label,
        stdio ? (fa ? L"\u0641\u0627\u06cc\u0644 \u0627\u062c\u0631\u0627\u06cc\u06cc" : L"Executable") : L"URL");
    (void)SetWindowTextW(
        dialog->mcp_secret_label,
        stdio
            ? (fa ? L"\u0645\u062d\u06cc\u0637 (\u0647\u0631 \u062e\u0637 KEY=VALUE)" : L"Environment (one KEY=VALUE per line)")
            : L"Bearer token");
    dio_layout_settings(dialog);
}

static bool dio_mcp_name_valid(const wchar_t *name) {
    return name != NULL && name[0] != L'\0' &&
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            name,
            -1,
            NULL,
            0,
            NULL,
            NULL) > 1 &&
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            name,
            -1,
            NULL,
            0,
            NULL,
            NULL) <= 65;
}

static bool dio_mcp_environment_valid(const wchar_t *environment) {
    const wchar_t *cursor = environment != NULL ? environment : L"";
    size_t count = 0u;
    while (*cursor != L'\0') {
        const wchar_t *end = cursor;
        const wchar_t *separator = NULL;
        while (*end != L'\0' && *end != L'\r' && *end != L'\n') {
            if (*end == L'=' && separator == NULL) {
                separator = end;
            }
            ++end;
        }
        if (end != cursor &&
            (separator == NULL || separator == cursor || ++count > 64u)) {
            return false;
        }
        cursor = end;
        while (*cursor == L'\r' || *cursor == L'\n') {
            ++cursor;
        }
    }
    return true;
}

static bool dio_mcp_new_secret_id(wchar_t *output, size_t capacity) {
    GUID guid;
    wchar_t value[40];
    return output != NULL && capacity != 0u &&
        SUCCEEDED(CoCreateGuid(&guid)) &&
        StringFromGUID2(&guid, value, (int)_countof(value)) > 0 &&
        swprintf_s(output, capacity, L"mcp.%ls", value) > 0;
}

static void dio_mcp_show_selection(
    DioSettingsDialog *dialog,
    int index) {
    dialog->changing_mcp = true;
    (void)SetWindowTextW(dialog->mcp_name, L"");
    (void)SetWindowTextW(dialog->mcp_url, L"");
    (void)SetWindowTextW(dialog->mcp_arguments, L"");
    (void)SetWindowTextW(dialog->mcp_cwd, L"");
    (void)SetWindowTextW(dialog->mcp_token, L"");
    (void)SetWindowTextW(dialog->mcp_environment, L"");
    if (index < 0 ||
        (size_t)index >= dialog->profile.mcp_server_count) {
        dialog->mcp_index = -1;
        (void)SendMessageW(dialog->mcp_transport, CB_SETCURSEL, 0u, 0u);
        (void)SendMessageW(
            dialog->mcp_enabled,
            BM_SETCHECK,
            BST_CHECKED,
            0u);
    } else {
        const DioMcpServer *server = &dialog->profile.mcp_servers[index];
        const wchar_t *secret = dialog->ui->vault.unlocked &&
            server->secret_id[0] != L'\0'
                ? dio_vault_get(&dialog->ui->vault, server->secret_id)
                : NULL;
        dialog->mcp_index = index;
        (void)SendMessageW(
            dialog->mcp_transport,
            CB_SETCURSEL,
            server->stdio ? 1u : 0u,
            0u);
        (void)SetWindowTextW(dialog->mcp_name, server->name);
        (void)SetWindowTextW(dialog->mcp_url, server->target);
        (void)SetWindowTextW(dialog->mcp_arguments, server->arguments);
        (void)SetWindowTextW(dialog->mcp_cwd, server->working_directory);
        (void)SetWindowTextW(
            server->stdio ? dialog->mcp_environment : dialog->mcp_token,
            secret != NULL ? secret : L"");
        (void)SendMessageW(
            dialog->mcp_enabled,
            BM_SETCHECK,
            server->enabled ? BST_CHECKED : BST_UNCHECKED,
            0u);
    }
    dialog->mcp_secret_dirty = false;
    dialog->changing_mcp = false;
    dio_mcp_update_transport(dialog);
}

static bool dio_mcp_upsert(DioSettingsDialog *dialog) {
    DioMcpServer server;
    size_t index;
    int target = dialog->mcp_index;
    ZeroMemory(&server, sizeof(server));
    (void)GetWindowTextW(
        dialog->mcp_name,
        server.name,
        (int)_countof(server.name));
    (void)GetWindowTextW(
        dialog->mcp_url,
        server.target,
        (int)_countof(server.target));
    (void)GetWindowTextW(
        dialog->mcp_arguments,
        server.arguments,
        (int)_countof(server.arguments));
    (void)GetWindowTextW(
        dialog->mcp_cwd,
        server.working_directory,
        (int)_countof(server.working_directory));
    server.stdio = dio_mcp_stdio_selected(dialog);
    server.secret_dirty = dialog->mcp_secret_dirty;
    if (server.secret_dirty) {
        (void)GetWindowTextW(
            server.stdio ? dialog->mcp_environment : dialog->mcp_token,
            server.secret_value,
            (int)_countof(server.secret_value));
    }
    server.enabled = SendMessageW(
        dialog->mcp_enabled,
        BM_GETCHECK,
        0u,
        0u) == BST_CHECKED;
    if (!dio_mcp_name_valid(server.name) || server.target[0] == L'\0' ||
        (!server.stdio && !dio_model_endpoint_valid(server.target)) ||
        (server.stdio && server.secret_dirty &&
         !dio_mcp_environment_valid(server.secret_value))) {
        dio_settings_error(
            dialog,
            dialog->ui->settings.persian
                ? L"\u067e\u06cc\u06a9\u0631\u0628\u0646\u062f\u06cc MCP \u0646\u0627\u0645\u0639\u062a\u0628\u0631 \u0627\u0633\u062a. \u0646\u0627\u0645 \u0628\u0627\u06cc\u062f \u062a\u0627 \u06f6\u06f4 \u0628\u0627\u06cc\u062a \u0628\u0627\u0634\u062f\u061b HTTP \u0641\u0642\u0637 HTTPS \u06cc\u0627 loopback \u0648 env \u0647\u0631 \u062e\u0637 KEY=VALUE \u0627\u0633\u062a."
                : L"Invalid MCP settings. Names are at most 64 UTF-8 bytes; HTTP must use HTTPS or loopback, and each environment line is KEY=VALUE.",
            dialog->ui->settings.persian);
        return false;
    }
    if (target < 0) {
        for (index = 0u; index < dialog->profile.mcp_server_count; ++index) {
            if (_wcsicmp(
                    dialog->profile.mcp_servers[index].name,
                    server.name) == 0) {
                target = (int)index;
                break;
            }
        }
    }
    if (target >= 0) {
        DioMcpServer *existing = &dialog->profile.mcp_servers[target];
        (void)wcsncpy_s(
            server.secret_id,
            _countof(server.secret_id),
            existing->secret_id,
            _TRUNCATE);
        server.always_tools = existing->always_tools;
        existing->always_tools = NULL;
    }
    if (server.secret_dirty && server.secret_value[0] != L'\0' &&
        server.secret_id[0] == L'\0' &&
        !dio_mcp_new_secret_id(server.secret_id, _countof(server.secret_id))) {
        free(server.always_tools);
        SecureZeroMemory(server.secret_value, sizeof(server.secret_value));
        return false;
    }
    if (target < 0) {
        if (dialog->profile.mcp_server_count >= DIO_AGENT_MCP_MAX) {
            dio_settings_error(
                dialog,
                L"The MCP server limit is 16.",
                dialog->ui->settings.persian);
            return false;
        }
        target = (int)dialog->profile.mcp_server_count++;
        (void)SendMessageW(
            dialog->mcp_list,
            LB_ADDSTRING,
            0u,
            (LPARAM)server.name);
    } else {
        (void)SendMessageW(dialog->mcp_list, LB_DELETESTRING, (WPARAM)target, 0u);
        (void)SendMessageW(dialog->mcp_list, LB_INSERTSTRING, (WPARAM)target, (LPARAM)server.name);
    }
    dialog->profile.mcp_servers[target] = server;
    dialog->mcp_index = target;
    (void)SendMessageW(dialog->mcp_list, LB_SETCURSEL, (WPARAM)target, 0u);
    dialog->mcp_secret_dirty = false;
    return true;
}

static void dio_mcp_remove_selection(DioSettingsDialog *dialog) {
    size_t index;
    if (dialog->mcp_index < 0 ||
        (size_t)dialog->mcp_index >= dialog->profile.mcp_server_count) {
        return;
    }
    free(dialog->profile.mcp_servers[dialog->mcp_index].always_tools);
    dialog->profile.mcp_servers[dialog->mcp_index].always_tools = NULL;
    SecureZeroMemory(
        dialog->profile.mcp_servers[dialog->mcp_index].secret_value,
        sizeof(dialog->profile.mcp_servers[dialog->mcp_index].secret_value));
    for (index = (size_t)dialog->mcp_index;
         index + 1u < dialog->profile.mcp_server_count;
         ++index) {
        dialog->profile.mcp_servers[index] =
            dialog->profile.mcp_servers[index + 1u];
    }
    dialog->profile.mcp_server_count -= 1u;
    ZeroMemory(
        &dialog->profile.mcp_servers[dialog->profile.mcp_server_count],
        sizeof(dialog->profile.mcp_servers[0]));
    (void)SendMessageW(
        dialog->mcp_list,
        LB_DELETESTRING,
        (WPARAM)dialog->mcp_index,
        0u);
    dio_mcp_show_selection(dialog, -1);
}

static bool dio_read_system_prompt(
    DioSettingsDialog *dialog) {
    const int length = GetWindowTextLengthW(dialog->system_prompt);
    wchar_t *prompt;
    int utf8_bytes;
    if (length < 0 || length > 65536) {
        return false;
    }
    prompt = (wchar_t *)calloc((size_t)length + 1u, sizeof(*prompt));
    if (prompt == NULL ||
        GetWindowTextW(
            dialog->system_prompt,
            prompt,
            length + 1) != length) {
        free(prompt);
        return false;
    }
    utf8_bytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        prompt,
        -1,
        NULL,
        0,
        NULL,
        NULL);
    if (utf8_bytes <= 0 || utf8_bytes - 1 > 64 * 1024) {
        SecureZeroMemory(
            prompt,
            ((size_t)length + 1u) * sizeof(*prompt));
        free(prompt);
        return false;
    }
    if (dialog->profile.system_prompt != NULL) {
        SecureZeroMemory(
            dialog->profile.system_prompt,
            (wcslen(dialog->profile.system_prompt) + 1u) * sizeof(wchar_t));
        free(dialog->profile.system_prompt);
    }
    dialog->profile.system_prompt = prompt;
    return true;
}

static void dio_vault_unlock_or_create(
    DioSettingsDialog *dialog) {
    wchar_t password[1024];
    wchar_t confirmation[1024];
    wchar_t error[256];
    const bool exists = dio_vault_exists(&dialog->ui->vault);
    bool result;
    (void)GetWindowTextW(
        dialog->vault_password,
        password,
        (int)_countof(password));
    (void)GetWindowTextW(
        dialog->vault_confirm,
        confirmation,
        (int)_countof(confirmation));
    if (!exists && wcscmp(password, confirmation) != 0) {
        SecureZeroMemory(password, sizeof(password));
        SecureZeroMemory(confirmation, sizeof(confirmation));
        dio_settings_error(
            dialog,
            dialog->ui->settings.persian
                ? L"\u0631\u0645\u0632 \u0648 \u062a\u06a9\u0631\u0627\u0631 \u0622\u0646 \u06cc\u06a9\u0633\u0627\u0646 \u0646\u06cc\u0633\u062a."
                : L"The password and confirmation do not match.",
            dialog->ui->settings.persian);
        return;
    }
    result = exists
        ? dio_vault_unlock(
            &dialog->ui->vault,
            password,
            error,
            _countof(error))
        : dio_vault_create(
            &dialog->ui->vault,
            password,
            error,
            _countof(error));
    SecureZeroMemory(password, sizeof(password));
    SecureZeroMemory(confirmation, sizeof(confirmation));
    (void)SetWindowTextW(dialog->vault_password, L"");
    (void)SetWindowTextW(dialog->vault_confirm, L"");
    if (!result) {
        dio_settings_error(
            dialog,
            error,
            dialog->ui->settings.persian);
        return;
    }
    (void)SetWindowTextW(
        dialog->vault_action,
        dialog->ui->settings.persian
            ? L"vault \u0628\u0627\u0632 \u0627\u0633\u062a"
            : L"Vault unlocked");
    (void)EnableWindow(dialog->vault_action, FALSE);
    (void)EnableWindow(dialog->vault_change, TRUE);
    (void)EnableWindow(dialog->vault_confirm, TRUE);
    (void)EnableWindow(dialog->api_key, TRUE);
    (void)EnableWindow(dialog->mcp_token, TRUE);
    (void)EnableWindow(dialog->mcp_environment, TRUE);
    {
        const wchar_t *secret =
            dialog->profile.api_key_secret_id[0] != L'\0'
                ? dio_vault_get(
                      &dialog->ui->vault,
                      dialog->profile.api_key_secret_id)
                : NULL;
        (void)SetWindowTextW(
            dialog->api_key,
            secret != NULL ? secret : L"");
        dialog->secret_dirty =
            dialog->detached_api_key_secret_id[0] != L'\0';
    }
    if (dialog->mcp_index >= 0) {
        dio_mcp_show_selection(dialog, dialog->mcp_index);
    }
    dio_schedule_model_discovery(dialog);
}

static void dio_vault_change_master_password(
    DioSettingsDialog *dialog) {
    wchar_t password[1024];
    wchar_t confirmation[1024];
    wchar_t error[256];
    bool result;
    (void)GetWindowTextW(
        dialog->vault_password,
        password,
        (int)_countof(password));
    (void)GetWindowTextW(
        dialog->vault_confirm,
        confirmation,
        (int)_countof(confirmation));
    if (wcscmp(password, confirmation) != 0) {
        SecureZeroMemory(password, sizeof(password));
        SecureZeroMemory(confirmation, sizeof(confirmation));
        dio_settings_error(
            dialog,
            dialog->ui->settings.persian
                ? L"\u0631\u0645\u0632 \u0648 \u062a\u06a9\u0631\u0627\u0631 \u0622\u0646 \u06cc\u06a9\u0633\u0627\u0646 \u0646\u06cc\u0633\u062a."
                : L"The password and confirmation do not match.",
            dialog->ui->settings.persian);
        return;
    }
    result = dio_vault_change_password(
        &dialog->ui->vault,
        password,
        error,
        _countof(error));
    SecureZeroMemory(password, sizeof(password));
    SecureZeroMemory(confirmation, sizeof(confirmation));
    (void)SetWindowTextW(dialog->vault_password, L"");
    (void)SetWindowTextW(dialog->vault_confirm, L"");
    if (!result) {
        dio_settings_error(
            dialog,
            error,
            dialog->ui->settings.persian);
    }
}

static void dio_vault_reset_dialog(
    DioSettingsDialog *dialog) {
    wchar_t error[256];
    const bool confirmed = dialog->ui->settings_smoke ||
        MessageBoxW(
            dialog->window,
            dialog->ui->settings.persian
                ? L"\u0647\u0645\u0647\u0654 \u06a9\u0644\u06cc\u062f\u0647\u0627\u06cc \u0630\u062e\u06cc\u0631\u0647\u200c\u0634\u062f\u0647 \u062d\u0630\u0641 \u0634\u0648\u0646\u062f\u061f"
                : L"Delete every saved API key and MCP token?",
            dialog->ui->settings.persian
                ? L"\u0628\u0627\u0632\u0646\u0634\u0627\u0646\u06cc vault"
                : L"Reset vault",
            MB_YESNO | MB_ICONWARNING) == IDYES;
    if (!confirmed) {
        return;
    }
    if (!dio_vault_reset(
            &dialog->ui->vault,
            error,
            _countof(error))) {
        dio_settings_error(
            dialog,
            error,
            dialog->ui->settings.persian);
        return;
    }
    (void)SetWindowTextW(dialog->api_key, L"");
    (void)SetWindowTextW(dialog->mcp_token, L"");
    (void)SetWindowTextW(dialog->mcp_environment, L"");
    (void)SetWindowTextW(dialog->vault_password, L"");
    (void)SetWindowTextW(dialog->vault_confirm, L"");
    (void)SetWindowTextW(
        dialog->vault_action,
        dialog->ui->settings.persian
            ? L"\u0633\u0627\u062e\u062a vault"
            : L"Create vault");
    (void)EnableWindow(dialog->vault_action, TRUE);
    (void)EnableWindow(dialog->vault_change, FALSE);
    (void)EnableWindow(dialog->vault_confirm, TRUE);
    (void)EnableWindow(dialog->api_key, FALSE);
    (void)EnableWindow(dialog->mcp_token, FALSE);
    (void)EnableWindow(dialog->mcp_environment, FALSE);
    dialog->profile.api_key_secret_id[0] = L'\0';
    for (size_t index = 0u;
         index < dialog->profile.mcp_server_count;
         ++index) {
        dialog->profile.mcp_servers[index].secret_id[0] = L'\0';
        dialog->profile.mcp_servers[index].secret_dirty = false;
        SecureZeroMemory(
            dialog->profile.mcp_servers[index].secret_value,
            sizeof(dialog->profile.mcp_servers[index].secret_value));
    }
    dialog->secret_dirty = false;
    dialog->mcp_secret_dirty = false;
}

static bool dio_profile_has_mcp_secret(
    const DioAgentProfile *profile,
    const wchar_t *secret_id) {
    size_t index;
    if (profile == NULL || secret_id == NULL || secret_id[0] == L'\0') {
        return false;
    }
    for (index = 0u; index < profile->mcp_server_count; ++index) {
        if (wcscmp(profile->mcp_servers[index].secret_id, secret_id) == 0) {
            return true;
        }
    }
    return false;
}

static bool dio_save_mcp_secrets(
    DioSettingsDialog *dialog,
    wchar_t *error,
    size_t error_capacity) {
    DioVault *vault = &dialog->ui->vault;
    bool changed = false;
    size_t index;
    for (index = 0u; index < dialog->profile.mcp_server_count; ++index) {
        DioMcpServer *server = &dialog->profile.mcp_servers[index];
        if (!server->secret_dirty) {
            continue;
        }
        if (!vault->unlocked) {
            (void)wcscpy_s(
                error,
                error_capacity,
                L"Unlock the vault before saving an MCP secret.");
            return false;
        }
        if (server->secret_value[0] != L'\0') {
            if (server->secret_id[0] == L'\0' &&
                !dio_mcp_new_secret_id(
                    server->secret_id,
                    _countof(server->secret_id))) {
                (void)wcscpy_s(error, error_capacity, L"Could not create a secret identifier.");
                return false;
            }
            if (!dio_vault_set(
                    vault,
                    server->secret_id,
                    server->secret_value,
                    error,
                    error_capacity)) {
                return false;
            }
        } else if (server->secret_id[0] != L'\0') {
            if (!dio_vault_remove(
                    vault,
                    server->secret_id,
                    error,
                    error_capacity)) {
                return false;
            }
            server->secret_id[0] = L'\0';
        }
        SecureZeroMemory(server->secret_value, sizeof(server->secret_value));
        server->secret_dirty = false;
        changed = true;
    }
    if (vault->unlocked) {
        for (index = 0u; index < dialog->ui->profile.mcp_server_count; ++index) {
            const wchar_t *old_id =
                dialog->ui->profile.mcp_servers[index].secret_id;
            if (old_id[0] != L'\0' &&
                !dio_profile_has_mcp_secret(&dialog->profile, old_id)) {
                if (!dio_vault_remove(
                        vault,
                        old_id,
                        error,
                        error_capacity)) {
                    return false;
                }
                changed = true;
            }
        }
    }
    return !changed || dio_vault_save(vault, error, error_capacity);
}

static bool dio_save_settings_dialog(DioSettingsDialog *dialog) {
    DioUi *ui = dialog->ui;
    DioSettings candidate = ui->settings;
    const unsigned int previous_invalid_controls =
        dialog->invalid_controls;
    wchar_t error[256];
    bool follow_up_valid = false;
    const bool follow_up_enabled =
        SendMessageW(
            dialog->follow_up_enabled,
            BM_GETCHECK,
            0u,
            0) == BST_CHECKED;
    dialog->invalid_controls = 0u;
    candidate.persian = dialog->locale_index == 0;
    candidate.reduced_motion =
        SendMessageW(
            dialog->reduced_motion,
            BM_GETCHECK,
            0u,
            0) == BST_CHECKED;
    if (dialog->microphone_index == 0u ||
        dialog->microphone_index >=
            dialog->microphone_count) {
        candidate.microphone_name[0] = L'\0';
        candidate.microphone_id[0] = L'\0';
    } else {
        (void)wcscpy_s(
            candidate.microphone_name,
            _countof(candidate.microphone_name),
            dialog->microphones[
                dialog->microphone_index]);
        (void)wcscpy_s(
            candidate.microphone_id,
            _countof(candidate.microphone_id),
            dialog->microphone_ids[
                dialog->microphone_index]);
    }
    if (!dio_settings_seconds(
            dialog->silence,
            0.1,
            60.0,
            &candidate.command_silence_ms)) {
        candidate.command_silence_ms = 0u;
    }
    if (follow_up_enabled) {
        follow_up_valid = dio_settings_seconds(
            dialog->follow_up,
            1.0,
            60.0,
            &candidate.follow_up_ms);
    } else {
        follow_up_valid = true;
        candidate.follow_up_ms = 0u;
    }
    (void)GetWindowTextW(
        dialog->base_url,
        dialog->profile.base_url,
        (int)_countof(dialog->profile.base_url));
    (void)GetWindowTextW(
        dialog->model,
        dialog->profile.model,
        (int)_countof(dialog->profile.model));
    (void)GetWindowTextW(
        dialog->reasoning,
        dialog->profile.reasoning_effort,
        (int)_countof(dialog->profile.reasoning_effort));
    (void)GetWindowTextW(
        dialog->service_tier,
        dialog->profile.service_tier,
        (int)_countof(dialog->profile.service_tier));
    if (_wcsicmp(dialog->profile.reasoning_effort, L"omit") == 0) {
        dialog->profile.reasoning_effort[0] = L'\0';
    }
    if (_wcsicmp(dialog->profile.service_tier, L"omit") == 0) {
        dialog->profile.service_tier[0] = L'\0';
    }
    if (!dio_read_system_prompt(dialog)) {
        dio_settings_error(
            dialog,
            candidate.persian
                ? L"\u067e\u0631\u0627\u0645\u067e\u062a \u0633\u06cc\u0633\u062a\u0645 \u062e\u06cc\u0644\u06cc \u0628\u0632\u0631\u06af \u0627\u0633\u062a \u06cc\u0627 \u062e\u0648\u0627\u0646\u062f\u0647 \u0646\u0634\u062f."
                : L"The system prompt is too large or could not be read.",
            candidate.persian);
        return false;
    }
    if (candidate.command_silence_ms < 100u ||
        candidate.command_silence_ms > 60000u) {
        dialog->invalid_controls |=
            1u << DIO_SETTINGS_CONTROL_SILENCE;
    }
    if (follow_up_enabled &&
        (!follow_up_valid ||
         candidate.follow_up_ms < 1000u ||
         candidate.follow_up_ms > 60000u)) {
        dialog->invalid_controls |=
            1u << DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS;
    }
    if (((previous_invalid_controls ^
          dialog->invalid_controls) &
         (1u << DIO_SETTINGS_CONTROL_SILENCE)) != 0u) {
        dio_settings_invalid_accessibility(
            dialog,
            dialog->silence,
            (dialog->invalid_controls &
             (1u << DIO_SETTINGS_CONTROL_SILENCE)) != 0u);
    }
    if (((previous_invalid_controls ^
          dialog->invalid_controls) &
         (1u << DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS)) != 0u) {
        dio_settings_invalid_accessibility(
            dialog,
            dialog->follow_up,
            (dialog->invalid_controls &
             (1u << DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS)) != 0u);
    }
    if (dialog->invalid_controls != 0u) {
        HWND first_invalid =
            (dialog->invalid_controls &
             (1u << DIO_SETTINGS_CONTROL_SILENCE)) != 0u
                ? dialog->silence
                : dialog->follow_up;
        (void)SetFocus(first_invalid);
        dio_ensure_settings_control_visible(
            dialog,
            first_invalid);
        dio_layout_settings(dialog);
        dio_settings_error(
            dialog,
            candidate.persian
                ? L"\u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646 \u0628\u0627\u06cc\u062f \u0628\u06cc\u0646 \u06f0\u066b\u06f1 \u062a\u0627 \u06f6\u06f0 \u062b\u0627\u0646\u06cc\u0647 \u0648 \u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634 \u0628\u06cc\u0646 \u06f1 \u062a\u0627 \u06f6\u06f0 \u062b\u0627\u0646\u06cc\u0647 \u0628\u0627\u0634\u062f."
                : L"Command delay must be 0.1\u201360 seconds and enabled follow-up 1\u201360 seconds.",
            candidate.persian);
        (void)SetFocus(first_invalid);
        dio_ensure_settings_control_visible(
            dialog,
            first_invalid);
        return false;
    }
    if (dialog->secret_dirty) {
        wchar_t secret[DIO_VAULT_VALUE_CAP];
        bool secret_result;
        (void)GetWindowTextW(
            dialog->api_key,
            secret,
            (int)_countof(secret));
        if (!ui->vault.unlocked) {
            SecureZeroMemory(secret, sizeof(secret));
            dio_settings_error(
                dialog,
                candidate.persian
                    ? L"\u0628\u0631\u0627\u06cc \u0630\u062e\u06cc\u0631\u0647\u0654 \u06a9\u0644\u06cc\u062f API\u060c vault \u0631\u0627 \u0628\u0627\u0632 \u06a9\u0646\u06cc\u062f."
                    : L"Unlock the vault before saving an API key.",
                candidate.persian);
            return false;
        }
        if (secret[0] != L'\0' &&
            dialog->profile.api_key_secret_id[0] == L'\0') {
            (void)wcscpy_s(
                dialog->profile.api_key_secret_id,
                _countof(dialog->profile.api_key_secret_id),
                L"provider.api_key");
        }
        secret_result = secret[0] != L'\0'
            ? dio_vault_set(
                &ui->vault,
                dialog->profile.api_key_secret_id,
                secret,
                error,
                _countof(error))
            : (dialog->profile.api_key_secret_id[0] == L'\0' || dio_vault_remove(
                &ui->vault,
                dialog->profile.api_key_secret_id,
                error,
                _countof(error)));
        if (secret_result &&
            dialog->detached_api_key_secret_id[0] != L'\0' &&
            wcscmp(
                dialog->detached_api_key_secret_id,
                dialog->profile.api_key_secret_id) != 0) {
            secret_result = dio_vault_remove(
                &ui->vault,
                dialog->detached_api_key_secret_id,
                error,
                _countof(error));
        }
        if (secret[0] == L'\0') {
            dialog->profile.api_key_secret_id[0] = L'\0';
        }
        SecureZeroMemory(secret, sizeof(secret));
        if (!secret_result ||
            !dio_vault_save(
                &ui->vault,
                error,
                _countof(error))) {
            dio_settings_error(dialog, error, candidate.persian);
            return false;
        }
        SecureZeroMemory(
            dialog->detached_api_key_secret_id,
            sizeof(dialog->detached_api_key_secret_id));
    }
    if (!dio_save_mcp_secrets(dialog, error, _countof(error))) {
        dio_settings_error(dialog, error, candidate.persian);
        return false;
    }
    if (!dio_settings_save_all(
            &ui->paths,
            &candidate,
            &dialog->profile,
            error,
            _countof(error))) {
        dio_settings_error(
            dialog,
            error,
            candidate.persian);
        return false;
    }
    ui->settings = candidate;
    if (!dio_agent_profile_copy(&ui->profile, &dialog->profile)) {
        dio_settings_error(
            dialog,
            candidate.persian
                ? L"\u062d\u0627\u0641\u0638\u0647\u0654 \u06a9\u0627\u0641\u06cc \u0648\u062c\u0648\u062f \u0646\u062f\u0627\u0631\u062f."
                : L"Out of memory while applying the agent profile.",
            candidate.persian);
        return false;
    }
    ui->model.rtl = candidate.persian;
    dio_refresh(ui, true, true);
    dio_emit_command(ui, DIO_UI_COMMAND_SETTINGS_CHANGED, true);
    return true;
}

static int dio_add_microphone(
    DioSettingsDialog *dialog,
    const wchar_t *name,
    const wchar_t *id) {
    size_t index;
    if (name == NULL || name[0] == L'\0') {
        return -1;
    }
    for (index = 0u;
         index < dialog->microphone_count;
         ++index) {
        if (id != NULL &&
            id[0] != L'\0' &&
            dialog->microphone_ids[index][0] != L'\0' &&
            wcscmp(
                dialog->microphone_ids[index],
                id) == 0) {
            return (int)index;
        }
        if ((id == NULL || id[0] == L'\0') &&
            _wcsicmp(
                dialog->microphones[index],
                name) == 0) {
            return (int)index;
        }
    }
    /* ponytail: 32 active endpoints; allocate only if real hardware exceeds it. */
    if (dialog->microphone_count >=
        DIO_SETTINGS_MICROPHONE_CAP) {
        return -1;
    }
    (void)wcsncpy_s(
        dialog->microphones[
            dialog->microphone_count],
        _countof(dialog->microphones[0]),
        name,
        _TRUNCATE);
    if (id != NULL) {
        (void)wcsncpy_s(
            dialog->microphone_ids[
                dialog->microphone_count],
            _countof(dialog->microphone_ids[0]),
            id,
            _TRUNCATE);
    }
    index = dialog->microphone_count++;
    return (int)index;
}

static void dio_populate_microphones(
    DioSettingsDialog *dialog) {
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDeviceCollection *collection = NULL;
    const wchar_t *saved =
        dialog->ui->settings.microphone_name;
    const wchar_t *saved_id =
        dialog->ui->settings.microphone_id;
    UINT count = 0u;
    UINT index;
    int selected;

    selected = dio_add_microphone(
        dialog,
        dialog->ui->settings.persian
            ? L"\u067e\u06cc\u0634\u200c\u0641\u0631\u0636 \u0633\u06cc\u0633\u062a\u0645"
            : L"System default",
        L"");
    if (SUCCEEDED(CoCreateInstance(
            &CLSID_MMDeviceEnumerator,
            NULL,
            CLSCTX_INPROC_SERVER,
            &IID_IMMDeviceEnumerator,
            (void **)&enumerator)) &&
        SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(
            enumerator,
            eCapture,
            DEVICE_STATE_ACTIVE,
            &collection)) &&
        SUCCEEDED(IMMDeviceCollection_GetCount(
            collection,
            &count))) {
        for (index = 0u; index < count; ++index) {
            IMMDevice *device = NULL;
            IPropertyStore *properties = NULL;
            LPWSTR endpoint_id = NULL;
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(IMMDeviceCollection_Item(
                    collection,
                    index,
                    &device)) &&
                SUCCEEDED(IMMDevice_OpenPropertyStore(
                    device,
                    STGM_READ,
                    &properties)) &&
                SUCCEEDED(IPropertyStore_GetValue(
                    properties,
                    &PKEY_Device_FriendlyName,
                    &name)) &&
                SUCCEEDED(IMMDevice_GetId(
                    device,
                    &endpoint_id)) &&
                name.vt == VT_LPWSTR) {
                (void)dio_add_microphone(
                    dialog,
                    name.pwszVal,
                    endpoint_id);
            }
            CoTaskMemFree(endpoint_id);
            PropVariantClear(&name);
            if (properties != NULL) {
                IPropertyStore_Release(properties);
            }
            if (device != NULL) {
                IMMDevice_Release(device);
            }
        }
    }
    if (collection != NULL) {
        IMMDeviceCollection_Release(collection);
    }
    if (enumerator != NULL) {
        IMMDeviceEnumerator_Release(enumerator);
    }
    if (saved[0] != L'\0') {
        selected = dio_add_microphone(
            dialog,
            saved,
            saved_id);
    }
    dialog->microphone_index =
        selected < 0 ? 0u : (size_t)selected;
}

static bool dio_create_settings_controls(
    DioSettingsDialog *dialog) {
    const bool fa = dialog->ui->settings.persian;
    wchar_t number[32];

    dialog->font = dio_settings_font(dialog);
    dialog->mcp_index = -1;
    dialog->locale_index =
        dialog->ui->settings.persian ? 0 : 1;
    dialog->locale = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        dialog->locale_index == 0
            ? L"\u0641\u0627\u0631\u0633\u06cc"
            : L"English",
        BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
        DIO_SETTINGS_LOCALE);

    dio_populate_microphones(dialog);
    dialog->microphone = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        dialog->microphone_count > 0u
            ? dialog->microphones[
                dialog->microphone_index]
            : L"",
        BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
        DIO_SETTINGS_MICROPHONE);

    (void)swprintf_s(
        number,
        _countof(number),
        L"%.1f",
        (double)dialog->ui->settings.command_silence_ms /
            1000.0);
    dio_localize_number(number, fa);
    dialog->silence = dio_dialog_control(
        dialog,
        0u,
        L"EDIT",
        number,
        ES_RIGHT | ES_AUTOHSCROLL | ES_MULTILINE |
            WS_TABSTOP,
        DIO_SETTINGS_SILENCE);
    if (dialog->silence != NULL) {
        (void)SendMessageW(
            dialog->silence,
            EM_SETLIMITTEXT,
            8u,
            0u);
    }

    dialog->follow_up_enabled = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u0641\u0639\u0627\u0644" : L"Enabled",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        DIO_SETTINGS_FOLLOW_UP_ENABLED);
    (void)SendMessageW(
        dialog->follow_up_enabled,
        BM_SETCHECK,
        dialog->ui->settings.follow_up_ms > 0u
            ? BST_CHECKED
            : BST_UNCHECKED,
        0u);
    (void)swprintf_s(
        number,
        _countof(number),
        L"%u",
        dialog->ui->settings.follow_up_ms > 0u
            ? dialog->ui->settings.follow_up_ms / 1000u
            : 4u);
    dio_localize_number(number, fa);
    dialog->follow_up = dio_dialog_control(
        dialog,
        0u,
        L"EDIT",
        number,
        ES_RIGHT | ES_AUTOHSCROLL | ES_MULTILINE |
            WS_TABSTOP,
        DIO_SETTINGS_FOLLOW_UP);
    (void)EnableWindow(
        dialog->follow_up,
        dialog->ui->settings.follow_up_ms > 0u);
    if (dialog->follow_up != NULL) {
        (void)SendMessageW(
            dialog->follow_up,
            EM_SETLIMITTEXT,
            2u,
            0u);
    }

    dialog->reduced_motion = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa
            ? L"\u06a9\u0627\u0647\u0634 \u062d\u0631\u06a9\u062a"
            : L"Reduce motion",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        DIO_SETTINGS_REDUCED_MOTION);
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETCHECK,
        dialog->ui->settings.reduced_motion
            ? BST_CHECKED
            : BST_UNCHECKED,
        0u);

    dialog->tabs = dio_dialog_control(
        dialog,
        fa ? WS_EX_LAYOUTRTL : 0u,
        WC_TABCONTROLW,
        L"",
        WS_TABSTOP | TCS_FOCUSONBUTTONDOWN,
        DIO_SETTINGS_TABS);
    if (dialog->tabs != NULL) {
        static const wchar_t *const en_tabs[] = {
            L"General", L"Model", L"System prompt", L"Tools"};
        static const wchar_t *const fa_tabs[] = {
            L"\u0639\u0645\u0648\u0645\u06cc",
            L"\u0645\u062f\u0644",
            L"\u067e\u0631\u0627\u0645\u067e\u062a \u0633\u06cc\u0633\u062a\u0645",
            L"\u0627\u0628\u0632\u0627\u0631\u0647\u0627"};
        const wchar_t *const *titles = fa ? fa_tabs : en_tabs;
        size_t index;
        for (index = 0u; index < DIO_SETTINGS_PAGE_COUNT; ++index) {
            TCITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = TCIF_TEXT;
            item.pszText = (LPWSTR)titles[index];
            (void)TabCtrl_InsertItem(dialog->tabs, (int)index, &item);
        }
    }

    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u0622\u062f\u0631\u0633 \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647" : L"Provider endpoint");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u06a9\u0644\u06cc\u062f API" : L"API key");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u0645\u062f\u0644" : L"Model");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u0645\u06cc\u0632\u0627\u0646 \u0627\u0633\u062a\u062f\u0644\u0627\u0644" : L"Reasoning effort");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u0633\u0637\u062d \u0633\u0631\u0648\u06cc\u0633" : L"Service tier");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u0631\u0645\u0632 \u0627\u0635\u0644\u06cc vault" : L"Vault master password");
    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_MODEL,
        fa ? L"\u062a\u06a9\u0631\u0627\u0631 \u0631\u0645\u0632" : L"Confirm password");
    dialog->base_url = dio_dialog_control(
        dialog,
        WS_EX_LTRREADING | WS_EX_LEFT,
        L"EDIT",
        dialog->profile.base_url,
        ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
        DIO_SETTINGS_BASE_URL);
    dialog->api_key = dio_dialog_control(
        dialog,
        WS_EX_LTRREADING | WS_EX_LEFT,
        L"EDIT",
        L"",
        ES_LEFT | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
        DIO_SETTINGS_API_KEY);
    dialog->model = dio_dialog_control(
        dialog,
        WS_EX_LTRREADING | WS_EX_LEFT,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_MODEL);
    if (dialog->model != NULL && dialog->profile.model[0] != L'\0') {
        (void)SendMessageW(
            dialog->model,
            CB_ADDSTRING,
            0u,
            (LPARAM)dialog->profile.model);
        (void)SetWindowTextW(dialog->model, dialog->profile.model);
    }
    dialog->reasoning = dio_dialog_control(
        dialog,
        0u,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_REASONING);
    dialog->service_tier = dio_dialog_control(
        dialog,
        0u,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_SERVICE_TIER);
    if (dialog->reasoning != NULL) {
        static const wchar_t *const values[] = {
            L"omit", L"none", L"minimal", L"low", L"medium",
            L"high", L"xhigh", L"max"};
        size_t index;
        for (index = 0u; index < _countof(values); ++index) {
            dio_combo_add(dialog->reasoning, values[index]);
        }
        (void)SetWindowTextW(
            dialog->reasoning,
            dialog->profile.reasoning_effort[0] != L'\0'
                ? dialog->profile.reasoning_effort
                : L"omit");
    }
    if (dialog->service_tier != NULL) {
        static const wchar_t *const values[] = {
            L"omit", L"auto", L"default", L"flex", L"fast", L"priority"};
        size_t index;
        for (index = 0u; index < _countof(values); ++index) {
            dio_combo_add(dialog->service_tier, values[index]);
        }
        (void)SetWindowTextW(
            dialog->service_tier,
            dialog->profile.service_tier[0] != L'\0'
                ? dialog->profile.service_tier
                : L"omit");
    }
    dialog->model_status = dio_dialog_control(
        dialog,
        fa ? WS_EX_RTLREADING : 0u,
        L"STATIC",
        dialog->profile.base_url[0] != L'\0'
            ? (fa ? L"در انتظار دریافت فهرست مدل‌ها…" : L"Waiting to load model catalog...")
            : (fa ? L"ابتدا آدرس ارائه‌دهنده را وارد کنید." : L"Enter a provider endpoint first."),
        fa ? SS_RIGHT : SS_LEFT,
        -1);
    (void)wcscpy_s(
        dialog->discovery_endpoint,
        _countof(dialog->discovery_endpoint),
        dialog->profile.base_url);
    dialog->discovery_anonymous =
        dialog->profile.base_url[0] != L'\0';
    dialog->vault_password = dio_dialog_control(
        dialog,
        0u,
        L"EDIT",
        L"",
        ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
        DIO_SETTINGS_VAULT_PASSWORD);
    dialog->vault_confirm = dio_dialog_control(
        dialog,
        0u,
        L"EDIT",
        L"",
        ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
        DIO_SETTINGS_VAULT_CONFIRM);
    dialog->vault_action = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        dio_vault_exists(&dialog->ui->vault)
            ? (fa ? L"\u0628\u0627\u0632\u06a9\u0631\u062f\u0646" : L"Unlock")
            : (fa ? L"\u0633\u0627\u062e\u062a vault" : L"Create vault"),
        BS_PUSHBUTTON | WS_TABSTOP,
        DIO_SETTINGS_VAULT_ACTION);
    dialog->vault_change = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u062a\u063a\u06cc\u06cc\u0631 \u0631\u0645\u0632" : L"Change password",
        BS_PUSHBUTTON | WS_TABSTOP,
        DIO_SETTINGS_VAULT_CHANGE);
    dialog->vault_reset = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u0628\u0627\u0632\u0646\u0634\u0627\u0646\u06cc vault" : L"Reset vault",
        BS_PUSHBUTTON | WS_TABSTOP,
        DIO_SETTINGS_VAULT_RESET);
    (void)EnableWindow(dialog->vault_change, dialog->ui->vault.unlocked);
    (void)EnableWindow(dialog->api_key, dialog->ui->vault.unlocked);
    (void)EnableWindow(
        dialog->vault_confirm,
        dialog->ui->vault.unlocked || !dio_vault_exists(&dialog->ui->vault));
    if (dialog->ui->vault.unlocked) {
        const wchar_t *secret = dio_vault_get(
            &dialog->ui->vault,
            dialog->profile.api_key_secret_id);
        if (secret != NULL) {
            (void)SetWindowTextW(dialog->api_key, secret);
        }
    }

    (void)dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_SYSTEM_PROMPT,
        fa ? L"\u062f\u0633\u062a\u0648\u0631\u0647\u0627\u06cc\u06cc \u06a9\u0647 \u062f\u0631 \u0627\u0628\u062a\u062f\u0627\u06cc \u0647\u0631 \u06af\u0641\u062a\u200c\u0648\u06af\u0648 \u0628\u0631\u0627\u06cc \u0645\u062f\u0644 \u0641\u0631\u0633\u062a\u0627\u062f\u0647 \u0645\u06cc\u200c\u0634\u0648\u062f." : L"Instructions sent to the model at the start of every conversation.");
    dialog->system_prompt = dio_dialog_control(
        dialog,
        0u,
        L"EDIT",
        dialog->profile.system_prompt,
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
            WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_SYSTEM_PROMPT);
    dialog->prompt_reset = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u0628\u0627\u0632\u0646\u0634\u0627\u0646\u06cc" : L"Reset",
        BS_PUSHBUTTON | WS_TABSTOP,
        DIO_SETTINGS_PROMPT_RESET);

    (void)dio_settings_label(dialog, DIO_SETTINGS_PAGE_TOOLS, fa ? L"\u0633\u0631\u0648\u0631\u0647\u0627\u06cc MCP" : L"MCP servers");
    (void)dio_settings_label(dialog, DIO_SETTINGS_PAGE_TOOLS, fa ? L"\u0631\u0648\u0634 \u0627\u062a\u0635\u0627\u0644" : L"Transport");
    (void)dio_settings_label(dialog, DIO_SETTINGS_PAGE_TOOLS, fa ? L"\u0646\u0627\u0645" : L"Name");
    dialog->mcp_target_label = dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_TOOLS,
        L"URL");
    dialog->mcp_arguments_label = dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_TOOLS,
        fa ? L"\u0622\u0631\u06af\u0648\u0645\u0627\u0646\u200c\u0647\u0627 (stdio)" : L"Arguments (stdio)");
    dialog->mcp_cwd_label = dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_TOOLS,
        fa ? L"\u067e\u0648\u0634\u0647\u0654 \u06a9\u0627\u0631 (stdio)" : L"Working directory (stdio)");
    dialog->mcp_secret_label = dio_settings_label(
        dialog,
        DIO_SETTINGS_PAGE_TOOLS,
        fa ? L"Bearer token" : L"Bearer token");
    dialog->mcp_list = dio_dialog_control(
        dialog,
        0u,
        L"LISTBOX",
        L"",
        LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_MCP_LIST);
    dialog->mcp_transport = dio_dialog_control(
        dialog,
        0u,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_MCP_TRANSPORT);
    if (dialog->mcp_transport != NULL) {
        dio_combo_add(dialog->mcp_transport, L"HTTP");
        dio_combo_add(dialog->mcp_transport, L"stdio");
        (void)SendMessageW(dialog->mcp_transport, CB_SETCURSEL, 0u, 0u);
    }
    dialog->mcp_name = dio_dialog_control(dialog, 0u, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, DIO_SETTINGS_MCP_NAME);
    dialog->mcp_url = dio_dialog_control(dialog, WS_EX_LTRREADING | WS_EX_LEFT, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP, DIO_SETTINGS_MCP_URL);
    dialog->mcp_arguments = dio_dialog_control(dialog, WS_EX_LTRREADING | WS_EX_LEFT, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP, DIO_SETTINGS_MCP_ARGUMENTS);
    dialog->mcp_cwd = dio_dialog_control(dialog, WS_EX_LTRREADING | WS_EX_LEFT, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP, DIO_SETTINGS_MCP_CWD);
    dialog->mcp_token = dio_dialog_control(dialog, WS_EX_LTRREADING | WS_EX_LEFT, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, DIO_SETTINGS_MCP_TOKEN);
    dialog->mcp_environment = dio_dialog_control(
        dialog,
        WS_EX_LTRREADING | WS_EX_LEFT,
        L"EDIT",
        L"",
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
            WS_VSCROLL | WS_TABSTOP,
        DIO_SETTINGS_MCP_ENVIRONMENT);
    dialog->mcp_enabled = dio_dialog_control(dialog, 0u, L"BUTTON", fa ? L"\u0641\u0639\u0627\u0644" : L"Enabled", BS_AUTOCHECKBOX | WS_TABSTOP, DIO_SETTINGS_MCP_ENABLED);
    dialog->mcp_add = dio_dialog_control(dialog, 0u, L"BUTTON", fa ? L"\u0627\u0641\u0632\u0648\u062f\u0646 / \u0628\u0647\u200c\u0631\u0648\u0632\u0631\u0633\u0627\u0646\u06cc" : L"Add / update", BS_PUSHBUTTON | WS_TABSTOP, DIO_SETTINGS_MCP_ADD);
    dialog->mcp_remove = dio_dialog_control(dialog, 0u, L"BUTTON", fa ? L"\u062d\u0630\u0641" : L"Remove", BS_PUSHBUTTON | WS_TABSTOP, DIO_SETTINGS_MCP_REMOVE);
    (void)SendMessageW(dialog->mcp_enabled, BM_SETCHECK, BST_CHECKED, 0u);
    (void)EnableWindow(dialog->mcp_token, dialog->ui->vault.unlocked);
    (void)EnableWindow(dialog->mcp_environment, dialog->ui->vault.unlocked);
    {
        size_t index;
        for (index = 0u; index < dialog->profile.mcp_server_count; ++index) {
            (void)SendMessageW(
                dialog->mcp_list,
                LB_ADDSTRING,
                0u,
                (LPARAM)dialog->profile.mcp_servers[index].name);
        }
    }
    dialog->cancel = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u0627\u0646\u0635\u0631\u0627\u0641" : L"Cancel",
        BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
        IDCANCEL);
    dialog->save = dio_dialog_control(
        dialog,
        0u,
        L"BUTTON",
        fa ? L"\u0630\u062e\u06cc\u0631\u0647" : L"Save changes",
        BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
        IDOK);
    if (dialog->locale == NULL ||
        dialog->microphone == NULL ||
        dialog->silence == NULL ||
        dialog->follow_up_enabled == NULL ||
        dialog->follow_up == NULL ||
        dialog->reduced_motion == NULL ||
        dialog->tabs == NULL ||
        dialog->base_url == NULL ||
        dialog->api_key == NULL ||
        dialog->model == NULL ||
        dialog->reasoning == NULL ||
        dialog->service_tier == NULL ||
        dialog->model_status == NULL ||
        dialog->vault_password == NULL ||
        dialog->vault_confirm == NULL ||
        dialog->vault_action == NULL ||
        dialog->vault_change == NULL ||
        dialog->vault_reset == NULL ||
        dialog->system_prompt == NULL ||
        dialog->prompt_reset == NULL ||
        dialog->mcp_list == NULL ||
        dialog->mcp_transport == NULL ||
        dialog->mcp_name == NULL ||
        dialog->mcp_url == NULL ||
        dialog->mcp_arguments == NULL ||
        dialog->mcp_cwd == NULL ||
        dialog->mcp_token == NULL ||
        dialog->mcp_environment == NULL ||
        dialog->mcp_enabled == NULL ||
        dialog->mcp_add == NULL ||
        dialog->mcp_remove == NULL ||
        dialog->save == NULL ||
        dialog->cancel == NULL) {
        return false;
    }
    (void)SendMessageW(dialog->base_url, EM_SETLIMITTEXT, DIO_AGENT_BASE_URL_CAP - 1u, 0u);
    (void)SendMessageW(dialog->api_key, EM_SETLIMITTEXT, DIO_VAULT_VALUE_CAP - 1u, 0u);
    (void)SendMessageW(dialog->model, CB_LIMITTEXT, DIO_AGENT_MODEL_CAP - 1u, 0u);
    (void)SendMessageW(dialog->reasoning, CB_LIMITTEXT, DIO_AGENT_OPTION_CAP - 1u, 0u);
    (void)SendMessageW(dialog->service_tier, CB_LIMITTEXT, DIO_AGENT_OPTION_CAP - 1u, 0u);
    (void)SendMessageW(dialog->vault_password, EM_SETLIMITTEXT, 1024u, 0u);
    (void)SendMessageW(dialog->vault_confirm, EM_SETLIMITTEXT, 1024u, 0u);
    (void)SendMessageW(dialog->system_prompt, EM_SETLIMITTEXT, 65536u, 0u);
    (void)SendMessageW(dialog->mcp_name, EM_SETLIMITTEXT, DIO_AGENT_MCP_NAME_CAP - 1u, 0u);
    (void)SendMessageW(dialog->mcp_url, EM_SETLIMITTEXT, DIO_AGENT_MCP_TARGET_CAP - 1u, 0u);
    (void)SendMessageW(dialog->mcp_arguments, EM_SETLIMITTEXT, DIO_AGENT_MCP_ARGUMENTS_CAP - 1u, 0u);
    (void)SendMessageW(dialog->mcp_cwd, EM_SETLIMITTEXT, DIO_AGENT_MCP_WORKING_DIRECTORY_CAP - 1u, 0u);
    (void)SendMessageW(dialog->mcp_token, EM_SETLIMITTEXT, DIO_AGENT_MCP_SECRET_VALUE_CAP - 1u, 0u);
    (void)SendMessageW(dialog->mcp_environment, EM_SETLIMITTEXT, DIO_AGENT_MCP_SECRET_VALUE_CAP - 1u, 0u);
    dialog->secret_dirty = false;
    dio_layout_settings(dialog);
    dio_schedule_model_discovery(dialog);
    return true;
}

static BYTE dio_settings_channel(float value) {
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return 255u;
    }
    return (BYTE)lroundf(value * 255.0f);
}

static COLORREF dio_settings_color(
    const DioSettingsDialog *dialog,
    CuiColorRole role) {
    const CuiColor color =
        cui_theme_color(&dialog->ui->theme, role);
    return RGB(
        dio_settings_channel(color.r),
        dio_settings_channel(color.g),
        dio_settings_channel(color.b));
}

static void dio_draw_settings_checkbox(
    DioSettingsDialog *dialog,
    HWND checkbox,
    HDC dc) {
    wchar_t label[128];
    RECT client;
    RECT box;
    RECT text;
    CuiButtonPaint paint;
    CuiResult result = CUI_INVALID_ARGUMENT;
    HDC box_dc = NULL;
    HBITMAP box_bitmap = NULL;
    HGDIOBJ box_previous = NULL;
    HGDIOBJ previous = NULL;
    const bool checked =
        SendMessageW(
            checkbox,
            BM_GETCHECK,
            0u,
            0u) == BST_CHECKED;
    const LRESULT native_state =
        SendMessageW(
            checkbox,
            BM_GETSTATE,
            0u,
            0u);
    const bool focused =
        GetFocus() == checkbox;
    const int box_size =
        dio_dialog_px(dialog, 20);
    const int gap =
        dio_dialog_px(dialog, 8);

    if (dc == NULL ||
        !GetClientRect(checkbox, &client)) {
        return;
    }
    FillRect(
        dc,
        &client,
        dialog->background_brush);
    box.top =
        (client.bottom - box_size) / 2;
    box.bottom = box.top + box_size;
    if (dialog->ui->settings.persian) {
        box.right = client.right;
        box.left = box.right - box_size;
        text = client;
        text.right = box.left - gap;
    } else {
        box.left = client.left;
        box.right = box.left + box_size;
        text = client;
        text.left = box.right + gap;
    }

    ZeroMemory(&paint, sizeof(paint));
    paint.variant = checked
        ? CUI_BUTTON_PRIMARY
        : CUI_BUTTON_OUTLINE;
    paint.icon = checked
        ? CUI_ICON_CHECK
        : CUI_ICON_NONE;
    paint.radius = CUI_RADIUS_SMALL;
    paint.parent_background = CUI_ROLE_BACKGROUND;
    paint.icon_size = 12.0f;
    if (IsWindowEnabled(checkbox)) {
        paint.state |= CUI_BUTTON_ENABLED;
    }
    if (dio_button_is_hot(checkbox)) {
        paint.state |= CUI_BUTTON_HOT;
    }
    if ((native_state & BST_PUSHED) != 0u) {
        paint.state |= CUI_BUTTON_PRESSED;
    }
    if (dialog->high_contrast && focused) {
        paint.state |= CUI_BUTTON_FOCUSED;
    }
    if (dialog->high_contrast && checked) {
        paint.state |= CUI_BUTTON_CHECKED;
    }
    box_dc = CreateCompatibleDC(dc);
    if (box_dc != NULL) {
        RECT local = {
            0,
            0,
            box_size,
            box_size};
        box_bitmap = CreateCompatibleBitmap(
            dc,
            box_size,
            box_size);
        if (box_bitmap != NULL) {
            box_previous = SelectObject(
                box_dc,
                box_bitmap);
            result = cui_win32_draw_owner_button(
                dialog->graphics,
                box_dc,
                &local,
                &paint);
            if (result == CUI_OK) {
                (void)BitBlt(
                    dc,
                    box.left,
                    box.top,
                    box_size,
                    box_size,
                    box_dc,
                    0,
                    0,
                    SRCCOPY);
            }
        }
    }
    if (box_previous != NULL) {
        (void)SelectObject(
            box_dc,
            box_previous);
    }
    if (box_bitmap != NULL) {
        DeleteObject(box_bitmap);
    }
    if (box_dc != NULL) {
        DeleteDC(box_dc);
    }
    if (result == CUI_TARGET_RECREATE) {
        cui_win32_context_discard_target(
            dialog->graphics);
        InvalidateRect(checkbox, NULL, FALSE);
    }

    (void)GetWindowTextW(
        checkbox,
        label,
        (int)_countof(label));
    if (dialog->font != NULL) {
        previous = SelectObject(dc, dialog->font);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(
        dc,
        dialog->high_contrast
            ? GetSysColor(
                IsWindowEnabled(checkbox)
                    ? COLOR_WINDOWTEXT
                    : COLOR_GRAYTEXT)
            : dio_settings_color(
                dialog,
                !IsWindowEnabled(checkbox)
                    ? CUI_ROLE_MUTED_FOREGROUND
                    : focused
                        ? CUI_ROLE_PRIMARY
                        : CUI_ROLE_FOREGROUND));
    (void)DrawTextW(
        dc,
        label,
        -1,
        &text,
        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
            DT_NOPREFIX |
            (dialog->ui->settings.persian
                ? DT_RIGHT | DT_RTLREADING
                : DT_LEFT));
    if (previous != NULL) {
        (void)SelectObject(dc, previous);
    }
}

static bool dio_normalize_single_line(
    wchar_t *text) {
    bool changed = false;
    size_t index;

    for (index = 0u; text[index] != L'\0'; ++index) {
        if (text[index] == L'\r' ||
            text[index] == L'\n') {
            text[index] = L' ';
            changed = true;
        }
    }
    return changed;
}

static void dio_normalize_settings_edit(
    DioSettingsDialog *dialog,
    HWND edit) {
    wchar_t text[128];
    DWORD selection_start = 0u;
    DWORD selection_end = 0u;

    if (dialog->normalizing_edit) {
        return;
    }
    text[0] = L'\0';
    (void)GetWindowTextW(
        edit,
        text,
        (int)_countof(text));
    if (!dio_normalize_single_line(text)) {
        return;
    }
    (void)SendMessageW(
        edit,
        EM_GETSEL,
        (WPARAM)&selection_start,
        (LPARAM)&selection_end);
    dialog->normalizing_edit = true;
    (void)SetWindowTextW(edit, text);
    (void)SendMessageW(
        edit,
        EM_SETSEL,
        selection_start,
        selection_end);
    dialog->normalizing_edit = false;
    dio_center_settings_edit(dialog, edit);
}

static COLORREF dio_ui_color(
    const DioUi *ui,
    CuiColorRole role) {
    const CuiColor color =
        cui_theme_color(&ui->theme, role);
    return RGB(
        dio_settings_channel(color.r),
        dio_settings_channel(color.g),
        dio_settings_channel(color.b));
}

static HBRUSH dio_prepare_menu(
    HMENU menu,
    const DioUi *ui,
    bool high_contrast) {
    MENUINFO info;
    HBRUSH background;
    if (menu == NULL || ui == NULL || high_contrast) {
        return NULL;
    }
    background = CreateSolidBrush(
        dio_ui_color(ui, CUI_ROLE_POPOVER));
    if (background == NULL) {
        return NULL;
    }
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    info.hbrBack = background;
    if (!SetMenuInfo(menu, &info)) {
        DeleteObject(background);
        return NULL;
    }
    return background;
}

static bool dio_append_menu_item(
    HMENU menu,
    DioMenuItem *item,
    const DioUi *ui,
    UINT command,
    const wchar_t *text,
    HFONT font,
    UINT dpi,
    bool rtl,
    bool separator,
    bool checked,
    bool owner_draw) {
    MENUITEMINFOW info;
    if (!owner_draw) {
        return AppendMenuW(
            menu,
            separator
                ? MF_SEPARATOR
                : MF_STRING |
                    (checked ? MF_CHECKED : 0u),
            command,
            separator ? NULL : text) != FALSE;
    }
    if (item == NULL || ui == NULL) {
        return false;
    }
    item->ui = ui;
    item->text = text;
    item->font = font;
    item->dpi = dpi;
    item->rtl = rtl;
    item->separator = separator;
    item->checked = checked;
    item->accessibility.dwMSAASignature =
        MSAA_MENU_SIG;
    item->accessibility.cchWText =
        text != NULL ? (DWORD)wcslen(text) : 0u;
    item->accessibility.pszWText =
        (LPWSTR)text;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_DATA;
    info.fType = MFT_OWNERDRAW |
        (separator ? MFT_SEPARATOR : 0u);
    info.dwItemData = (ULONG_PTR)item;
    if (!separator) {
        info.fMask |=
            MIIM_ID | MIIM_STATE | MIIM_STRING;
        info.wID = command;
        info.fState = checked
            ? MFS_CHECKED
            : MFS_ENABLED;
        info.dwTypeData = (LPWSTR)text;
        info.cch = text != NULL
            ? (UINT)wcslen(text)
            : 0u;
    }
    return InsertMenuItemW(
        menu,
        (UINT)GetMenuItemCount(menu),
        TRUE,
        &info) != FALSE;
}

static bool dio_measure_menu_item(
    MEASUREITEMSTRUCT *measure) {
    const DioMenuItem *item;
    HDC dc;
    HFONT previous = NULL;
    SIZE extent = {0, 0};
    if (measure == NULL ||
        measure->CtlType != ODT_MENU ||
        measure->itemData == 0u) {
        return false;
    }
    item = (const DioMenuItem *)measure->itemData;
    if (item->separator) {
        measure->itemWidth = 1u;
        measure->itemHeight =
            (UINT)MulDiv(9, (int)item->dpi, 96);
        return true;
    }
    dc = GetDC(NULL);
    if (dc != NULL) {
        previous = (HFONT)SelectObject(
            dc,
            item->font != NULL
                ? item->font
                : GetStockObject(DEFAULT_GUI_FONT));
        (void)GetTextExtentPoint32W(
            dc,
            item->text != NULL ? item->text : L"",
            item->text != NULL
                ? (int)wcslen(item->text)
                : 0,
            &extent);
        if (previous != NULL) {
            (void)SelectObject(dc, previous);
        }
        ReleaseDC(NULL, dc);
    }
    measure->itemWidth =
        (UINT)(extent.cx +
            MulDiv(52, (int)item->dpi, 96));
    measure->itemHeight =
        (UINT)MulDiv(34, (int)item->dpi, 96);
    return true;
}

static bool dio_draw_menu_item(
    const DRAWITEMSTRUCT *draw) {
    const DioMenuItem *item;
    RECT bounds;
    RECT text_bounds;
    HBRUSH brush;
    HFONT previous_font = NULL;
    COLORREF previous_text;
    int previous_mode;
    int padding;
    int check_width;
    if (draw == NULL ||
        draw->CtlType != ODT_MENU ||
        draw->itemData == 0u) {
        return false;
    }
    item = (const DioMenuItem *)draw->itemData;
    padding = MulDiv(12, (int)item->dpi, 96);
    check_width = MulDiv(20, (int)item->dpi, 96);
    bounds = draw->rcItem;
    brush = CreateSolidBrush(
        dio_ui_color(
            item->ui,
            (draw->itemState & ODS_SELECTED) != 0u
                ? CUI_ROLE_ACCENT
                : CUI_ROLE_POPOVER));
    if (brush == NULL) {
        return false;
    }
    FillRect(draw->hDC, &bounds, brush);
    DeleteObject(brush);
    if (item->separator) {
        bounds.left += padding;
        bounds.right -= padding;
        bounds.top =
            (bounds.top + bounds.bottom) / 2;
        bounds.bottom = bounds.top + 1;
        brush = CreateSolidBrush(
            dio_ui_color(
                item->ui,
                CUI_ROLE_INPUT));
        if (brush != NULL) {
            FillRect(draw->hDC, &bounds, brush);
            DeleteObject(brush);
        }
        return true;
    }
    text_bounds = bounds;
    if (item->rtl) {
        text_bounds.left += padding;
        text_bounds.right -=
            padding + check_width;
    } else {
        text_bounds.left +=
            padding + check_width;
        text_bounds.right -= padding;
    }
    previous_font = (HFONT)SelectObject(
        draw->hDC,
        item->font != NULL
            ? item->font
            : GetStockObject(DEFAULT_GUI_FONT));
    previous_mode = SetBkMode(
        draw->hDC,
        TRANSPARENT);
    previous_text = SetTextColor(
        draw->hDC,
        dio_ui_color(
            item->ui,
            (draw->itemState & ODS_DISABLED) != 0u
                ? CUI_ROLE_MUTED_FOREGROUND
                : CUI_ROLE_POPOVER_FOREGROUND));
    (void)DrawTextW(
        draw->hDC,
        item->text != NULL ? item->text : L"",
        -1,
        &text_bounds,
        DT_SINGLELINE | DT_VCENTER |
            DT_END_ELLIPSIS | DT_NOPREFIX |
            (item->rtl
                ? DT_RIGHT | DT_RTLREADING
                : DT_LEFT));
    if (item->checked) {
        const int center_x = item->rtl
            ? bounds.right - padding -
                check_width / 2
            : bounds.left + padding +
                check_width / 2;
        const int center_y =
            (bounds.top + bounds.bottom) / 2;
        HPEN pen = CreatePen(
            PS_SOLID,
            MulDiv(2, (int)item->dpi, 96) > 1
                ? MulDiv(2, (int)item->dpi, 96)
                : 1,
            dio_ui_color(
                item->ui,
                CUI_ROLE_PRIMARY));
        if (pen != NULL) {
            HPEN previous_pen =
                (HPEN)SelectObject(draw->hDC, pen);
            MoveToEx(
                draw->hDC,
                center_x -
                    MulDiv(5, (int)item->dpi, 96),
                center_y,
                NULL);
            LineTo(
                draw->hDC,
                center_x -
                    MulDiv(1, (int)item->dpi, 96),
                center_y +
                    MulDiv(4, (int)item->dpi, 96));
            LineTo(
                draw->hDC,
                center_x +
                    MulDiv(6, (int)item->dpi, 96),
                center_y -
                    MulDiv(5, (int)item->dpi, 96));
            if (previous_pen != NULL) {
                (void)SelectObject(
                    draw->hDC,
                    previous_pen);
            }
            DeleteObject(pen);
        }
    }
    (void)SetTextColor(draw->hDC, previous_text);
    (void)SetBkMode(draw->hDC, previous_mode);
    if (previous_font != NULL) {
        (void)SelectObject(
            draw->hDC,
            previous_font);
    }
    return true;
}

static bool dio_menu_char(
    WPARAM wparam,
    LPARAM lparam,
    LRESULT *result) {
    const HMENU menu = (HMENU)lparam;
    const wchar_t typed = (wchar_t)LOWORD(wparam);
    const int count = GetMenuItemCount(menu);
    int start = 0;
    int offset;
    int index;
    bool owner_draw = false;
    if (menu == NULL || result == NULL) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if ((GetMenuState(
                menu,
                (UINT)index,
                MF_BYPOSITION) &
             MF_HILITE) != 0u) {
            start = (index + 1) % count;
            break;
        }
    }
    for (offset = 0; offset < count; ++offset) {
        MENUITEMINFOW info;
        const DioMenuItem *item;
        index = (start + offset) % count;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_DATA | MIIM_FTYPE;
        if (!GetMenuItemInfoW(
                menu,
                (UINT)index,
                TRUE,
                &info) ||
            (info.fType & MFT_OWNERDRAW) == 0u ||
            info.dwItemData == 0u) {
            continue;
        }
        owner_draw = true;
        item = (const DioMenuItem *)info.dwItemData;
        if (!item->separator &&
            item->text != NULL &&
            item->text[0] != L'\0' &&
            CompareStringOrdinal(
                &typed,
                1,
                item->text,
                1,
                TRUE) == CSTR_EQUAL) {
            *result = MAKELRESULT(
                index,
                MNC_SELECT);
            return true;
        }
    }
    if (owner_draw) {
        *result = MAKELRESULT(0, MNC_IGNORE);
    }
    return owner_draw;
}

static void dio_settings_recreate_brushes(
    DioSettingsDialog *dialog) {
    COLORREF background;
    COLORREF field;
    if (dialog->field_brush != NULL) {
        DeleteObject(dialog->field_brush);
    }
    if (dialog->background_brush != NULL) {
        DeleteObject(dialog->background_brush);
    }
    background = dialog->high_contrast
        ? GetSysColor(COLOR_WINDOW)
        : dio_settings_color(dialog, CUI_ROLE_BACKGROUND);
    field = dialog->high_contrast
        ? GetSysColor(COLOR_WINDOW)
        : dio_settings_color(dialog, CUI_ROLE_MUTED);
    dialog->background_brush = CreateSolidBrush(background);
    dialog->field_brush = CreateSolidBrush(field);
}

static void dio_settings_apply_child_style(
    DioSettingsDialog *dialog) {
    HWND child = GetWindow(dialog->window, GW_CHILD);
    while (child != NULL) {
        (void)SetWindowTheme(
            child,
            dialog->high_contrast
                ? L""
                : L"DarkMode_Explorer",
            NULL);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void dio_settings_set_appearance(
    DioSettingsDialog *dialog,
    bool high_contrast) {
    if (dialog->applying_appearance) {
        return;
    }
    dialog->applying_appearance = true;
    dialog->high_contrast = high_contrast;
    (void)SetWindowTheme(
        dialog->window,
        dialog->high_contrast
            ? L""
            : L"DarkMode_Explorer",
        NULL);
    dio_settings_recreate_brushes(dialog);
    if (dialog->graphics != NULL) {
        cui_win32_context_set_appearance(
            dialog->graphics,
            dialog->scale,
            dialog->high_contrast);
    }
    cui_win32_apply_window_theme(
        dialog->window,
        &dialog->ui->theme,
        dialog->high_contrast);
    dio_settings_apply_child_style(dialog);
    RedrawWindow(
        dialog->window,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_ALLCHILDREN);
    dialog->applying_appearance = false;
}

static void dio_settings_apply_appearance(
    DioSettingsDialog *dialog) {
    dio_settings_set_appearance(
        dialog,
        cui_win32_high_contrast());
}

static void dio_settings_apply_font(
    DioSettingsDialog *dialog) {
    HWND child;
    if (dialog->font != NULL) {
        DeleteObject(dialog->font);
    }
    dialog->font = dio_settings_font(dialog);
    if (dialog->font == NULL) {
        return;
    }
    child = GetWindow(dialog->window, GW_CHILD);
    while (child != NULL) {
        (void)SendMessageW(
            child,
            WM_SETFONT,
            (WPARAM)dialog->font,
            TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static bool dio_settings_accessible_name(
    DioSettingsDialog *dialog,
    HWND control,
    const wchar_t *name) {
    return dialog->accessibility != NULL &&
        SUCCEEDED(IAccPropServices_SetHwndPropStr(
            dialog->accessibility,
            control,
            (DWORD)OBJID_CLIENT,
            (DWORD)CHILDID_SELF,
            PROPID_ACC_NAME,
            name));
}

static bool dio_settings_accessible_role(
    DioSettingsDialog *dialog,
    HWND control,
    LONG role) {
    VARIANT value;
    if (dialog->accessibility == NULL) {
        return false;
    }
    VariantInit(&value);
    V_VT(&value) = VT_I4;
    V_I4(&value) = role;
    return SUCCEEDED(IAccPropServices_SetHwndProp(
        dialog->accessibility,
        control,
        (DWORD)OBJID_CLIENT,
        (DWORD)CHILDID_SELF,
        PROPID_ACC_ROLE,
        value));
}

static void dio_settings_invalid_accessibility(
    DioSettingsDialog *dialog,
    HWND control,
    bool invalid) {
    MSAAPROPID properties[2];
    const bool fa = dialog->ui->settings.persian;
    const wchar_t *base_name =
        control == dialog->silence
            ? fa
                ? L"\u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"End-of-command delay, seconds"
            : fa
                ? L"\u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"Follow-up time, seconds";
    const wchar_t *invalid_name =
        control == dialog->silence
            ? fa
                ? L"\u0645\u0642\u062f\u0627\u0631 \u0646\u0627\u0645\u0639\u062a\u0628\u0631\u060c \u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"Invalid value, end-of-command delay, seconds"
            : fa
                ? L"\u0645\u0642\u062f\u0627\u0631 \u0646\u0627\u0645\u0639\u062a\u0628\u0631\u060c \u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"Invalid value, follow-up time, seconds";
    if (dialog->accessibility == NULL ||
        control == NULL) {
        return;
    }
    if (!invalid) {
        properties[0] = PROPID_ACC_DESCRIPTION;
        properties[1] = PROPID_ACC_HELP;
        (void)IAccPropServices_ClearHwndProps(
            dialog->accessibility,
            control,
            (DWORD)OBJID_CLIENT,
            (DWORD)CHILDID_SELF,
            properties,
            (int)_countof(properties));
        (void)dio_settings_accessible_name(
            dialog,
            control,
            base_name);
        NotifyWinEvent(
            EVENT_OBJECT_DESCRIPTIONCHANGE,
            control,
            OBJID_CLIENT,
            CHILDID_SELF);
        NotifyWinEvent(
            EVENT_OBJECT_NAMECHANGE,
            control,
            OBJID_CLIENT,
            CHILDID_SELF);
        return;
    }
    (void)IAccPropServices_SetHwndPropStr(
        dialog->accessibility,
        control,
        (DWORD)OBJID_CLIENT,
        (DWORD)CHILDID_SELF,
        PROPID_ACC_DESCRIPTION,
        dialog->ui->settings.persian
            ? L"\u0645\u0642\u062f\u0627\u0631 \u0646\u0627\u0645\u0639\u062a\u0628\u0631 \u0627\u0633\u062a."
            : L"Invalid value.");
    (void)dio_settings_accessible_name(
        dialog,
        control,
        invalid_name);
    (void)IAccPropServices_SetHwndPropStr(
        dialog->accessibility,
        control,
        (DWORD)OBJID_CLIENT,
        (DWORD)CHILDID_SELF,
        PROPID_ACC_HELP,
        dialog->ui->settings.persian
            ? L"\u0645\u0642\u062f\u0627\u0631 \u0646\u0627\u0645\u0639\u062a\u0628\u0631 \u0627\u0633\u062a."
            : L"Invalid value.");
    NotifyWinEvent(
        EVENT_OBJECT_DESCRIPTIONCHANGE,
        control,
        OBJID_CLIENT,
        CHILDID_SELF);
    NotifyWinEvent(
        EVENT_OBJECT_NAMECHANGE,
        control,
        OBJID_CLIENT,
        CHILDID_SELF);
}

static bool dio_settings_choice_name(
    DioSettingsDialog *dialog,
    bool microphone,
    wchar_t *name,
    size_t capacity) {
    const wchar_t *value = microphone
        ? dialog->microphones[
            dialog->microphone_index]
        : dialog->locale_index == 0
            ? L"\u0641\u0627\u0631\u0633\u06cc"
            : L"English";
    if (swprintf_s(
            name,
            capacity,
            dialog->ui->settings.persian
                ? microphone
                    ? L"\u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646: %ls"
                    : L"\u0632\u0628\u0627\u0646: %ls"
                : microphone
                    ? L"Microphone: %ls"
                    : L"Language: %ls",
            value) < 0) {
        return false;
    }
    return true;
}

static bool dio_settings_choice_accessible_name(
    DioSettingsDialog *dialog,
    bool microphone) {
    wchar_t name[512];
    if (!dio_settings_choice_name(
            dialog,
            microphone,
            name,
            _countof(name))) {
        return false;
    }
    {
        HWND control = microphone
            ? dialog->microphone
            : dialog->locale;
        return
            SetWindowTextW(control, name) != FALSE &&
            dio_settings_accessible_name(
                dialog,
                control,
                name);
    }
}

static bool dio_settings_annotate_controls(
    DioSettingsDialog *dialog) {
    const bool fa = dialog->ui->settings.persian;
    return
        dio_settings_choice_accessible_name(
            dialog,
            false) &&
        dio_settings_accessible_role(
            dialog,
            dialog->locale,
            ROLE_SYSTEM_COMBOBOX) &&
        dio_settings_choice_accessible_name(
            dialog,
            true) &&
        dio_settings_accessible_role(
            dialog,
            dialog->microphone,
            ROLE_SYSTEM_COMBOBOX) &&
        dio_settings_accessible_name(
            dialog,
            dialog->silence,
            fa
                ? L"\u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"End-of-command delay, seconds") &&
        dio_settings_accessible_name(
            dialog,
            dialog->follow_up,
            fa
                ? L"\u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"Follow-up time, seconds") &&
        dio_settings_accessible_name(
            dialog,
            dialog->follow_up_enabled,
            fa
                ? L"\u0641\u0639\u0627\u0644\u200c\u06a9\u0631\u062f\u0646 \u06af\u0648\u0634\u200c\u062f\u0627\u062f\u0646 \u0628\u0647 \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc"
                : L"Enable follow-up listening after each reply") &&
        dio_settings_accessible_name(
            dialog,
            dialog->reduced_motion,
            fa
                ? L"\u06a9\u0627\u0647\u0634 \u062d\u0631\u06a9\u062a\u200c\u0647\u0627\u06cc \u063a\u06cc\u0631\u0636\u0631\u0648\u0631\u06cc"
                : L"Reduce non-essential motion") &&
        dio_settings_accessible_name(dialog, dialog->tabs, fa ? L"\u0628\u062e\u0634\u200c\u0647\u0627\u06cc \u062a\u0646\u0638\u06cc\u0645\u0627\u062a" : L"Settings sections") &&
        dio_settings_accessible_name(dialog, dialog->base_url, fa ? L"\u0622\u062f\u0631\u0633 \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647" : L"Provider endpoint") &&
        dio_settings_accessible_name(dialog, dialog->api_key, L"API key") &&
        dio_settings_accessible_name(dialog, dialog->model, fa ? L"\u0645\u062f\u0644" : L"Model") &&
        dio_settings_accessible_name(dialog, dialog->reasoning, fa ? L"\u0645\u06cc\u0632\u0627\u0646 \u0627\u0633\u062a\u062f\u0644\u0627\u0644" : L"Reasoning effort") &&
        dio_settings_accessible_name(dialog, dialog->service_tier, fa ? L"\u0633\u0637\u062d \u0633\u0631\u0648\u06cc\u0633" : L"Service tier") &&
        dio_settings_accessible_name(dialog, dialog->vault_password, fa ? L"\u0631\u0645\u0632 \u0627\u0635\u0644\u06cc vault" : L"Vault master password") &&
        dio_settings_accessible_name(dialog, dialog->vault_confirm, fa ? L"\u062a\u06a9\u0631\u0627\u0631 \u0631\u0645\u0632 vault" : L"Confirm vault password") &&
        dio_settings_accessible_name(dialog, dialog->system_prompt, fa ? L"\u067e\u0631\u0627\u0645\u067e\u062a \u0633\u06cc\u0633\u062a\u0645" : L"System prompt") &&
        dio_settings_accessible_name(dialog, dialog->prompt_reset, fa ? L"\u0628\u0627\u0632\u0646\u0634\u0627\u0646\u06cc \u067e\u0631\u0627\u0645\u067e\u062a \u0633\u06cc\u0633\u062a\u0645" : L"Reset system prompt") &&
        dio_settings_accessible_name(dialog, dialog->mcp_list, fa ? L"\u0633\u0631\u0648\u0631\u0647\u0627\u06cc MCP" : L"MCP servers") &&
        dio_settings_accessible_name(dialog, dialog->mcp_transport, fa ? L"\u0631\u0648\u0634 \u0627\u062a\u0635\u0627\u0644 MCP" : L"MCP transport") &&
        dio_settings_accessible_name(dialog, dialog->mcp_name, fa ? L"\u0646\u0627\u0645 \u0633\u0631\u0648\u0631 MCP" : L"MCP server name") &&
        dio_settings_accessible_name(dialog, dialog->mcp_url, fa ? L"URL \u06cc\u0627 \u0641\u0627\u06cc\u0644 \u0627\u062c\u0631\u0627\u06cc\u06cc MCP" : L"MCP URL or executable") &&
        dio_settings_accessible_name(dialog, dialog->mcp_arguments, fa ? L"\u0622\u0631\u06af\u0648\u0645\u0627\u0646\u200c\u0647\u0627\u06cc MCP stdio" : L"MCP stdio arguments") &&
        dio_settings_accessible_name(dialog, dialog->mcp_cwd, fa ? L"\u067e\u0648\u0634\u0647\u0654 \u06a9\u0627\u0631 MCP stdio" : L"MCP stdio working directory") &&
        dio_settings_accessible_name(dialog, dialog->mcp_token, L"MCP bearer token") &&
        dio_settings_accessible_name(dialog, dialog->mcp_environment, fa ? L"\u0645\u062a\u063a\u06cc\u0631\u0647\u0627\u06cc \u0645\u062d\u06cc\u0637 MCP stdio" : L"MCP stdio environment");
}

static void dio_settings_menu_text(
    const wchar_t *source,
    wchar_t *output,
    size_t capacity) {
    size_t used = 0u;
    while (*source != L'\0' && used + 1u < capacity) {
        if (*source == L'&' && used + 2u < capacity) {
            output[used++] = L'&';
        }
        output[used++] = *source++;
    }
    output[used] = L'\0';
}

static void dio_show_settings_choice(
    DioSettingsDialog *dialog,
    bool microphone) {
    static const wchar_t *const locales[] = {
        L"\u0641\u0627\u0631\u0633\u06cc",
        L"English"};
    HWND anchor = microphone
        ? dialog->microphone
        : dialog->locale;
    const size_t count = microphone
        ? dialog->microphone_count
        : _countof(locales);
    HMENU menu = CreatePopupMenu();
    HBRUSH menu_background;
    DioMenuItem items[DIO_SETTINGS_MICROPHONE_CAP];
    RECT bounds;
    UINT selected;
    size_t index;

    if (menu == NULL || count == 0u) {
        if (menu != NULL) {
            DestroyMenu(menu);
        }
        return;
    }
    menu_background = dio_prepare_menu(
        menu,
        dialog->ui,
        dialog->high_contrast);
    for (index = 0u; index < count; ++index) {
        wchar_t menu_text[512];
        const wchar_t *text = microphone
            ? dialog->microphones[index]
            : locales[index];
        const bool checked = microphone
            ? index == dialog->microphone_index
            : (int)index == dialog->locale_index;
        if (menu_background == NULL) {
            dio_settings_menu_text(
                text,
                menu_text,
                _countof(menu_text));
        }
        (void)dio_append_menu_item(
            menu,
            &items[index],
            dialog->ui,
            (UINT)(index + 1u),
            menu_background != NULL
                ? text
                : menu_text,
            dialog->font,
            dialog->dpi,
            dialog->ui->settings.persian,
            false,
            checked,
            menu_background != NULL);
    }
    if (dialog->ui->smoke) {
        LRESULT menu_char_result = 0;
        dialog->menu_smoke_ok =
            menu_background == NULL ||
            (items[0].text != NULL &&
             dio_menu_char(
                 items[0].text[0],
                 (LPARAM)menu,
                 &menu_char_result) &&
             LOWORD(menu_char_result) == 0u &&
             HIWORD(menu_char_result) == MNC_SELECT);
    }
    GetWindowRect(anchor, &bounds);
    SetForegroundWindow(dialog->window);
    if (dialog->ui->smoke) {
        (void)HiliteMenuItem(
            dialog->window,
            menu,
            (UINT)(microphone
                ? dialog->microphone_index
                : (size_t)dialog->locale_index) + 1u,
            MF_BYCOMMAND | MF_HILITE);
    }
    selected = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_WORKAREA |
            (dialog->ui->settings.persian
                ? TPM_RIGHTALIGN |
                    (menu_background == NULL
                        ? TPM_LAYOUTRTL
                        : 0u)
                : TPM_LEFTALIGN),
        dialog->ui->settings.persian
            ? bounds.right
            : bounds.left,
        bounds.bottom,
        dialog->window,
        NULL);
    DestroyMenu(menu);
    if (menu_background != NULL) {
        DeleteObject(menu_background);
    }
    if (selected > 0u &&
        (size_t)selected <= count) {
        index = (size_t)selected - 1u;
        if (microphone) {
            dialog->microphone_index = index;
        } else {
            dialog->locale_index = (int)index;
        }
        SetWindowTextW(
            anchor,
            microphone
                ? dialog->microphones[index]
                : locales[index]);
        (void)dio_settings_view_set_choice_text(
            dialog->view,
            microphone
                ? DIO_SETTINGS_CONTROL_MICROPHONE
                : DIO_SETTINGS_CONTROL_LOCALE,
            microphone
                ? dialog->microphones[index]
                : locales[index]);
        (void)dio_settings_choice_accessible_name(
            dialog,
            microphone);
        InvalidateRect(anchor, NULL, TRUE);
    }
    (void)PostMessageW(
        dialog->window,
        WM_NULL,
        0u,
        0u);
}

static void dio_draw_settings_button(
    DioSettingsDialog *dialog,
    const DRAWITEMSTRUCT *draw) {
    DioSettingsControl control;
    const bool save = draw->CtlID == IDOK;
    const bool choice =
        draw->CtlID == DIO_SETTINGS_LOCALE ||
        draw->CtlID == DIO_SETTINGS_MICROPHONE;
    CuiButtonPaint paint;
    CuiResult result;

    if (draw->CtlID == DIO_SETTINGS_LOCALE) {
        control = DIO_SETTINGS_CONTROL_LOCALE;
    } else if (draw->CtlID ==
               DIO_SETTINGS_MICROPHONE) {
        control = DIO_SETTINGS_CONTROL_MICROPHONE;
    } else if (save) {
        control = DIO_SETTINGS_CONTROL_SAVE;
    } else {
        control = DIO_SETTINGS_CONTROL_CANCEL;
    }
    ZeroMemory(&paint, sizeof(paint));
    paint.variant = save
        ? CUI_BUTTON_PRIMARY
        : choice
            ? CUI_BUTTON_OUTLINE
            : CUI_BUTTON_SECONDARY;
    paint.icon = CUI_ICON_NONE;
    paint.radius = CUI_RADIUS_MEDIUM;
    paint.parent_background =
        choice ? CUI_ROLE_BACKGROUND : CUI_ROLE_CARD;
    paint.label = dio_settings_view_button_label(
        dialog->view,
        control);
    paint.icon_size = 0.0f;
    paint.content_gap = 0.0f;
    if ((draw->itemState & ODS_DISABLED) == 0u) {
        paint.state |= CUI_BUTTON_ENABLED;
    }
    if ((draw->itemState & ODS_HOTLIGHT) != 0u ||
        dio_button_is_hot(draw->hwndItem)) {
        paint.state |= CUI_BUTTON_HOT;
    }
    if ((draw->itemState & ODS_SELECTED) != 0u) {
        paint.state |= CUI_BUTTON_PRESSED;
    }
    if ((draw->itemState & ODS_FOCUS) != 0u) {
        paint.state |= CUI_BUTTON_FOCUSED;
    }
    result = cui_win32_draw_owner_button(
        dialog->graphics,
        draw->hDC,
        &draw->rcItem,
        &paint);
    if (choice && result == CUI_OK) {
        const int half =
            dio_dialog_px(dialog, 4);
        const int center_x =
            dialog->ui->settings.persian
                ? draw->rcItem.left +
                    dio_dialog_px(dialog, 16)
                : draw->rcItem.right -
                    dio_dialog_px(dialog, 16);
        const int center_y =
            (draw->rcItem.top +
             draw->rcItem.bottom) /
            2;
        POINT chevron[3] = {
            {center_x - half, center_y - half / 2},
            {center_x, center_y + half / 2},
            {center_x + half, center_y - half / 2}};
        HPEN pen = CreatePen(
            PS_SOLID,
            dio_dialog_px(dialog, 2),
            dialog->high_contrast
                ? GetSysColor(COLOR_WINDOWTEXT)
                : dio_settings_color(
                    dialog,
                    CUI_ROLE_FOREGROUND));
        if (pen != NULL) {
            HGDIOBJ previous =
                SelectObject(draw->hDC, pen);
            (void)Polyline(
                draw->hDC,
                chevron,
                (int)_countof(chevron));
            if (previous != NULL) {
                (void)SelectObject(
                    draw->hDC,
                    previous);
            }
            DeleteObject(pen);
        }
    }
    if (result == CUI_TARGET_RECREATE) {
        cui_win32_context_discard_target(dialog->graphics);
        InvalidateRect(draw->hwndItem, NULL, TRUE);
    }
}

static bool dio_write_bytes(
    HANDLE file,
    const void *data,
    DWORD size) {
    const BYTE *bytes = (const BYTE *)data;
    DWORD written = 0u;
    while (written < size) {
        DWORD chunk = 0u;
        if (!WriteFile(
                file,
                bytes + written,
                size - written,
                &chunk,
                NULL) ||
            chunk == 0u) {
            return false;
        }
        written += chunk;
    }
    return true;
}

static bool dio_capture_window_bitmap(
    HWND window,
    const wchar_t *path) {
    RECT bounds;
    RECT window_bounds;
    RECT visible_bounds;
    BITMAPINFO info;
    BITMAPFILEHEADER file_header;
    HDC screen = NULL;
    HDC memory = NULL;
    HDC print_memory = NULL;
    HBITMAP bitmap = NULL;
    HBITMAP print_bitmap = NULL;
    HGDIOBJ previous = NULL;
    HGDIOBJ print_previous = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE *pixels = NULL;
    size_t pixel_size;
    int width;
    int height;
    int window_width;
    int window_height;
    int print_offset_x = 0;
    int print_offset_y = 0;
    bool result = false;

    if (!GetWindowRect(window, &window_bounds)) {
        return false;
    }
    bounds = window_bounds;
    if (SUCCEEDED(DwmGetWindowAttribute(
            window,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &visible_bounds,
            sizeof(visible_bounds))) &&
        visible_bounds.right > visible_bounds.left &&
        visible_bounds.bottom > visible_bounds.top) {
        bounds = visible_bounds;
        print_offset_x =
            visible_bounds.left - window_bounds.left;
        print_offset_y =
            visible_bounds.top - window_bounds.top;
    }
    width = bounds.right - bounds.left;
    height = bounds.bottom - bounds.top;
    window_width =
        window_bounds.right - window_bounds.left;
    window_height =
        window_bounds.bottom - window_bounds.top;
    if (width <= 0 || height <= 0 ||
        window_width <= 0 || window_height <= 0) {
        return false;
    }
    pixel_size = (size_t)width * (size_t)height * 4u;
    if (pixel_size > MAXDWORD) {
        return false;
    }
    screen = GetDC(NULL);
    memory = screen != NULL ? CreateCompatibleDC(screen) : NULL;
    print_memory =
        screen != NULL ? CreateCompatibleDC(screen) : NULL;
    bitmap = screen != NULL
        ? CreateCompatibleBitmap(screen, width, height)
        : NULL;
    print_bitmap = screen != NULL
        ? CreateCompatibleBitmap(
            screen,
            window_width,
            window_height)
        : NULL;
    pixels = (BYTE *)malloc(pixel_size);
    if (screen == NULL ||
        memory == NULL ||
        print_memory == NULL ||
        bitmap == NULL ||
        print_bitmap == NULL ||
        pixels == NULL) {
        goto cleanup;
    }
    previous = SelectObject(memory, bitmap);
    print_previous =
        SelectObject(print_memory, print_bitmap);
    if (!PrintWindow(
            window,
            print_memory,
            PW_RENDERFULLCONTENT) ||
        !BitBlt(
                memory,
                0,
                0,
                width,
                height,
                print_memory,
                print_offset_x,
                print_offset_y,
                SRCCOPY)) {
        goto cleanup;
    }
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1u;
    info.bmiHeader.biBitCount = 32u;
    info.bmiHeader.biCompression = BI_RGB;
    if (previous != NULL) {
        (void)SelectObject(memory, previous);
        previous = NULL;
    }
    if (GetDIBits(
            memory,
            bitmap,
            0u,
            (UINT)height,
            pixels,
            &info,
            DIB_RGB_COLORS) != height) {
        goto cleanup;
    }
    ZeroMemory(&file_header, sizeof(file_header));
    file_header.bfType = 0x4d42u;
    file_header.bfOffBits =
        sizeof(file_header) + sizeof(info.bmiHeader);
    file_header.bfSize =
        file_header.bfOffBits + (DWORD)pixel_size;
    file = CreateFileW(
        path,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    result = file != INVALID_HANDLE_VALUE &&
        dio_write_bytes(
            file,
            &file_header,
            sizeof(file_header)) &&
        dio_write_bytes(
            file,
            &info.bmiHeader,
            sizeof(info.bmiHeader)) &&
        dio_write_bytes(
            file,
            pixels,
            (DWORD)pixel_size);

cleanup:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (previous != NULL && memory != NULL) {
        (void)SelectObject(memory, previous);
    }
    if (print_previous != NULL &&
        print_memory != NULL) {
        (void)SelectObject(
            print_memory,
            print_previous);
    }
    free(pixels);
    if (bitmap != NULL) {
        DeleteObject(bitmap);
    }
    if (print_bitmap != NULL) {
        DeleteObject(print_bitmap);
    }
    if (memory != NULL) {
        DeleteDC(memory);
    }
    if (print_memory != NULL) {
        DeleteDC(print_memory);
    }
    if (screen != NULL) {
        ReleaseDC(NULL, screen);
    }
    return result;
}

static bool dio_capture_popup_menu(
    const wchar_t *path) {
    HWND menu = NULL;
    DWORD process_id = 0u;
    RECT bounds;
    MONITORINFO monitor;
    do {
        menu = FindWindowExW(
            NULL,
            menu,
            L"#32768",
            NULL);
        if (menu != NULL) {
            (void)GetWindowThreadProcessId(
                menu,
                &process_id);
        }
    } while (
        menu != NULL &&
        (process_id != GetCurrentProcessId() ||
         !IsWindowVisible(menu)));
    ZeroMemory(&monitor, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    return
        menu != NULL &&
        GetWindowRect(menu, &bounds) &&
        GetMonitorInfoW(
            MonitorFromRect(
                &bounds,
                MONITOR_DEFAULTTONEAREST),
            &monitor) &&
        bounds.left >= monitor.rcWork.left &&
        bounds.top >= monitor.rcWork.top &&
        bounds.right <= monitor.rcWork.right &&
        bounds.bottom <= monitor.rcWork.bottom &&
        dio_capture_window_bitmap(menu, path);
}

static bool dio_accessible_name_present(HWND window) {
    IAccessible *accessible = NULL;
    VARIANT child;
    BSTR name = NULL;
    bool present;
    HRESULT result = AccessibleObjectFromWindow(
        window,
        (DWORD)OBJID_CLIENT,
        &IID_IAccessible,
        (void **)&accessible);
    if (FAILED(result) || accessible == NULL) {
        return false;
    }
    VariantInit(&child);
    V_VT(&child) = VT_I4;
    V_I4(&child) = CHILDID_SELF;
    result = IAccessible_get_accName(
        accessible,
        child,
        &name);
    present = SUCCEEDED(result) &&
        name != NULL &&
        SysStringLen(name) > 0u;
    SysFreeString(name);
    IAccessible_Release(accessible);
    return present;
}

struct DioCloseProvider {
    IRawElementProviderSimple simple;
    IInvokeProvider invoke;
    volatile LONG references;
    HWND window;
};

static ULONG dio_close_provider_add_ref(
    DioCloseProvider *provider) {
    return (ULONG)InterlockedIncrement(
        &provider->references);
}

static ULONG dio_close_provider_release(
    DioCloseProvider *provider) {
    const LONG remaining =
        InterlockedDecrement(&provider->references);
    if (remaining == 0) {
        free(provider);
    }
    return (ULONG)remaining;
}

static HRESULT dio_close_provider_query_interface(
    DioCloseProvider *provider,
    REFIID interface_id,
    void **output) {
    if (output == NULL) {
        return E_POINTER;
    }
    *output = NULL;
    if (IsEqualIID(interface_id, &IID_IUnknown) ||
        IsEqualIID(
            interface_id,
            &IID_IRawElementProviderSimple)) {
        *output = (void *)&provider->simple;
    } else if (
        IsEqualIID(
            interface_id,
            &IID_IInvokeProvider)) {
        *output = (void *)&provider->invoke;
    } else {
        return E_NOINTERFACE;
    }
    (void)dio_close_provider_add_ref(provider);
    return S_OK;
}

static DioCloseProvider *dio_close_provider_from_simple(
    IRawElementProviderSimple *interface_pointer) {
    return CONTAINING_RECORD(
        interface_pointer,
        DioCloseProvider,
        simple);
}

static DioCloseProvider *dio_close_provider_from_invoke(
    IInvokeProvider *interface_pointer) {
    return CONTAINING_RECORD(
        interface_pointer,
        DioCloseProvider,
        invoke);
}

static HRESULT STDMETHODCALLTYPE
dio_close_simple_query_interface(
    IRawElementProviderSimple *interface_pointer,
    REFIID interface_id,
    void **output) {
    return dio_close_provider_query_interface(
        dio_close_provider_from_simple(
            interface_pointer),
        interface_id,
        output);
}

static ULONG STDMETHODCALLTYPE dio_close_simple_add_ref(
    IRawElementProviderSimple *interface_pointer) {
    return dio_close_provider_add_ref(
        dio_close_provider_from_simple(
            interface_pointer));
}

static ULONG STDMETHODCALLTYPE dio_close_simple_release(
    IRawElementProviderSimple *interface_pointer) {
    return dio_close_provider_release(
        dio_close_provider_from_simple(
            interface_pointer));
}

static HRESULT STDMETHODCALLTYPE
dio_close_simple_provider_options(
    IRawElementProviderSimple *interface_pointer,
    enum ProviderOptions *output) {
    (void)interface_pointer;
    if (output == NULL) {
        return E_POINTER;
    }
    *output =
        ProviderOptions_ServerSideProvider |
        ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_close_simple_pattern_provider(
    IRawElementProviderSimple *interface_pointer,
    PATTERNID pattern_id,
    IUnknown **output) {
    DioCloseProvider *provider;
    if (output == NULL) {
        return E_POINTER;
    }
    *output = NULL;
    if (pattern_id != UIA_InvokePatternId) {
        return S_OK;
    }
    provider = dio_close_provider_from_simple(
        interface_pointer);
    *output = (IUnknown *)&provider->invoke;
    (void)dio_close_provider_add_ref(provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_close_simple_property_value(
    IRawElementProviderSimple *interface_pointer,
    PROPERTYID property_id,
    VARIANT *output) {
    (void)interface_pointer;
    if (output == NULL) {
        return E_POINTER;
    }
    VariantInit(output);
    if (property_id ==
        UIA_ControlTypePropertyId) {
        V_VT(output) = VT_I4;
        V_I4(output) = UIA_ButtonControlTypeId;
    } else if (
        property_id ==
            UIA_IsKeyboardFocusablePropertyId ||
        property_id ==
            UIA_IsControlElementPropertyId ||
        property_id ==
            UIA_IsContentElementPropertyId) {
        V_VT(output) = VT_BOOL;
        V_BOOL(output) = VARIANT_TRUE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_close_simple_host_provider(
    IRawElementProviderSimple *interface_pointer,
    IRawElementProviderSimple **output) {
    const DioCloseProvider *provider =
        dio_close_provider_from_simple(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    if (provider->window == NULL ||
        !IsWindow(provider->window)) {
        *output = NULL;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return UiaHostProviderFromHwnd(
        provider->window,
        output);
}

static HRESULT STDMETHODCALLTYPE
dio_close_invoke_query_interface(
    IInvokeProvider *interface_pointer,
    REFIID interface_id,
    void **output) {
    return dio_close_provider_query_interface(
        dio_close_provider_from_invoke(
            interface_pointer),
        interface_id,
        output);
}

static ULONG STDMETHODCALLTYPE dio_close_invoke_add_ref(
    IInvokeProvider *interface_pointer) {
    return dio_close_provider_add_ref(
        dio_close_provider_from_invoke(
            interface_pointer));
}

static ULONG STDMETHODCALLTYPE dio_close_invoke_release(
    IInvokeProvider *interface_pointer) {
    return dio_close_provider_release(
        dio_close_provider_from_invoke(
            interface_pointer));
}

static HRESULT STDMETHODCALLTYPE dio_close_invoke(
    IInvokeProvider *interface_pointer) {
    const DioCloseProvider *provider =
        dio_close_provider_from_invoke(
            interface_pointer);
    HWND parent;
    DWORD error;

    if (provider->window == NULL ||
        !IsWindow(provider->window)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!IsWindowEnabled(provider->window)) {
        return UIA_E_ELEMENTNOTENABLED;
    }
    parent = GetParent(provider->window);
    if (parent == NULL ||
        !PostMessageW(
            parent,
            WM_COMMAND,
            MAKEWPARAM(DIO_CLOSE_ID, BN_CLICKED),
            (LPARAM)provider->window)) {
        error = GetLastError();
        return error != ERROR_SUCCESS
            ? HRESULT_FROM_WIN32(error)
            : E_FAIL;
    }
    return S_OK;
}

static IRawElementProviderSimpleVtbl
DIO_CLOSE_SIMPLE_VTABLE = {
    dio_close_simple_query_interface,
    dio_close_simple_add_ref,
    dio_close_simple_release,
    dio_close_simple_provider_options,
    dio_close_simple_pattern_provider,
    dio_close_simple_property_value,
    dio_close_simple_host_provider};

static IInvokeProviderVtbl DIO_CLOSE_INVOKE_VTABLE = {
    dio_close_invoke_query_interface,
    dio_close_invoke_add_ref,
    dio_close_invoke_release,
    dio_close_invoke};

static DioCloseProvider *dio_close_provider_create(
    HWND window) {
    DioCloseProvider *provider;
    if (window == NULL) {
        return NULL;
    }
    provider = (DioCloseProvider *)calloc(
        1u,
        sizeof(*provider));
    if (provider == NULL) {
        return NULL;
    }
    provider->simple.lpVtbl =
        &DIO_CLOSE_SIMPLE_VTABLE;
    provider->invoke.lpVtbl =
        &DIO_CLOSE_INVOKE_VTABLE;
    provider->references = 1;
    provider->window = window;
    return provider;
}

static void dio_close_provider_disconnect(
    DioUi *ui,
    HWND window) {
    DioCloseProvider *provider =
        ui->close_provider;
    if (provider == NULL) {
        return;
    }
    ui->close_provider = NULL;
    (void)UiaReturnRawElementProvider(
        window,
        0u,
        0,
        NULL);
    provider->window = NULL;
    (void)dio_close_provider_release(provider);
}

static ULONG dio_transcript_provider_add_ref(
    DioTranscriptProvider *provider) {
    return (ULONG)InterlockedIncrement(
        &provider->references);
}

static ULONG dio_transcript_provider_release(
    DioTranscriptProvider *provider) {
    const LONG remaining =
        InterlockedDecrement(&provider->references);
    if (remaining == 0) {
        free(provider);
    }
    return (ULONG)remaining;
}

static HRESULT dio_transcript_provider_query_interface(
    DioTranscriptProvider *provider,
    REFIID interface_id,
    void **output) {
    if (output == NULL) {
        return E_POINTER;
    }
    *output = NULL;
    if (IsEqualIID(interface_id, &IID_IUnknown) ||
        IsEqualIID(
            interface_id,
            &IID_IRawElementProviderSimple)) {
        *output = (void *)&provider->simple;
    } else if (IsEqualIID(
                   interface_id,
                   &IID_IScrollProvider)) {
        *output = (void *)&provider->scroll;
    } else {
        return E_NOINTERFACE;
    }
    (void)dio_transcript_provider_add_ref(provider);
    return S_OK;
}

static DioTranscriptProvider *
dio_transcript_provider_from_simple(
    IRawElementProviderSimple *interface_pointer) {
    return CONTAINING_RECORD(
        interface_pointer,
        DioTranscriptProvider,
        simple);
}

static DioTranscriptProvider *
dio_transcript_provider_from_scroll(
    IScrollProvider *interface_pointer) {
    return CONTAINING_RECORD(
        interface_pointer,
        DioTranscriptProvider,
        scroll);
}

static HRESULT dio_transcript_provider_info(
    const DioTranscriptProvider *provider,
    DioTranscriptScrollInfo *info) {
    if (provider->window == NULL ||
        !IsWindow(provider->window)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    ZeroMemory(info, sizeof(*info));
    return SendMessageW(
               provider->window,
               DIO_WM_TRANSCRIPT_QUERY,
               0u,
               (LPARAM)info) != 0
        ? S_OK
        : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_simple_query_interface(
    IRawElementProviderSimple *interface_pointer,
    REFIID interface_id,
    void **output) {
    return dio_transcript_provider_query_interface(
        dio_transcript_provider_from_simple(
            interface_pointer),
        interface_id,
        output);
}

static ULONG STDMETHODCALLTYPE
dio_transcript_simple_add_ref(
    IRawElementProviderSimple *interface_pointer) {
    return dio_transcript_provider_add_ref(
        dio_transcript_provider_from_simple(
            interface_pointer));
}

static ULONG STDMETHODCALLTYPE
dio_transcript_simple_release(
    IRawElementProviderSimple *interface_pointer) {
    return dio_transcript_provider_release(
        dio_transcript_provider_from_simple(
            interface_pointer));
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_simple_provider_options(
    IRawElementProviderSimple *interface_pointer,
    enum ProviderOptions *output) {
    (void)interface_pointer;
    if (output == NULL) {
        return E_POINTER;
    }
    *output =
        ProviderOptions_ServerSideProvider |
        ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_simple_pattern_provider(
    IRawElementProviderSimple *interface_pointer,
    PATTERNID pattern_id,
    IUnknown **output) {
    DioTranscriptProvider *provider;
    if (output == NULL) {
        return E_POINTER;
    }
    *output = NULL;
    if (pattern_id != UIA_ScrollPatternId) {
        return S_OK;
    }
    provider = dio_transcript_provider_from_simple(
        interface_pointer);
    *output = (IUnknown *)&provider->scroll;
    (void)dio_transcript_provider_add_ref(provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_simple_property_value(
    IRawElementProviderSimple *interface_pointer,
    PROPERTYID property_id,
    VARIANT *output) {
    const DioTranscriptProvider *provider =
        dio_transcript_provider_from_simple(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    VariantInit(output);
    if (property_id == UIA_ControlTypePropertyId) {
        V_VT(output) = VT_I4;
        V_I4(output) = UIA_PaneControlTypeId;
    } else if (
        property_id ==
            UIA_IsKeyboardFocusablePropertyId) {
        V_VT(output) = VT_BOOL;
        V_BOOL(output) =
            provider->window != NULL &&
            IsWindow(provider->window) &&
            (GetWindowLongPtrW(
                 provider->window,
                 GWL_STYLE) &
             WS_TABSTOP) != 0
                ? VARIANT_TRUE
                : VARIANT_FALSE;
    } else if (
        property_id == UIA_IsControlElementPropertyId ||
        property_id == UIA_IsContentElementPropertyId ||
        property_id == UIA_IsEnabledPropertyId) {
        V_VT(output) = VT_BOOL;
        V_BOOL(output) = VARIANT_TRUE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_simple_host_provider(
    IRawElementProviderSimple *interface_pointer,
    IRawElementProviderSimple **output) {
    const DioTranscriptProvider *provider =
        dio_transcript_provider_from_simple(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    if (provider->window == NULL ||
        !IsWindow(provider->window)) {
        *output = NULL;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return UiaHostProviderFromHwnd(
        provider->window,
        output);
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_scroll_query_interface(
    IScrollProvider *interface_pointer,
    REFIID interface_id,
    void **output) {
    return dio_transcript_provider_query_interface(
        dio_transcript_provider_from_scroll(
            interface_pointer),
        interface_id,
        output);
}

static ULONG STDMETHODCALLTYPE
dio_transcript_scroll_add_ref(
    IScrollProvider *interface_pointer) {
    return dio_transcript_provider_add_ref(
        dio_transcript_provider_from_scroll(
            interface_pointer));
}

static ULONG STDMETHODCALLTYPE
dio_transcript_scroll_release(
    IScrollProvider *interface_pointer) {
    return dio_transcript_provider_release(
        dio_transcript_provider_from_scroll(
            interface_pointer));
}

static bool dio_transcript_scroll_amount_valid(
    enum ScrollAmount amount) {
    return
        amount == ScrollAmount_LargeDecrement ||
        amount == ScrollAmount_SmallDecrement ||
        amount == ScrollAmount_NoAmount ||
        amount == ScrollAmount_LargeIncrement ||
        amount == ScrollAmount_SmallIncrement;
}

static HRESULT STDMETHODCALLTYPE dio_transcript_scroll(
    IScrollProvider *interface_pointer,
    enum ScrollAmount horizontal_amount,
    enum ScrollAmount vertical_amount) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    DioTranscriptScrollInfo info;
    UINT command;
    HRESULT result;
    if (!dio_transcript_scroll_amount_valid(
            horizontal_amount) ||
        !dio_transcript_scroll_amount_valid(
            vertical_amount)) {
        return E_INVALIDARG;
    }
    if (horizontal_amount != ScrollAmount_NoAmount) {
        return UIA_E_INVALIDOPERATION;
    }
    if (vertical_amount == ScrollAmount_NoAmount) {
        return S_OK;
    }
    result = dio_transcript_provider_info(
        provider,
        &info);
    if (FAILED(result) ||
        !info.vertically_scrollable) {
        return FAILED(result)
            ? result
            : UIA_E_INVALIDOPERATION;
    }
    switch (vertical_amount) {
    case ScrollAmount_LargeDecrement:
        command = SB_PAGEUP;
        break;
    case ScrollAmount_SmallDecrement:
        command = SB_LINEUP;
        break;
    case ScrollAmount_LargeIncrement:
        command = SB_PAGEDOWN;
        break;
    case ScrollAmount_SmallIncrement:
        command = SB_LINEDOWN;
        break;
    default:
        return E_INVALIDARG;
    }
    return SendMessageW(
               provider->window,
               DIO_WM_TRANSCRIPT_SCROLL,
               command,
               0u) != 0
        ? S_OK
        : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_set_scroll_percent(
    IScrollProvider *interface_pointer,
    double horizontal_percent,
    double vertical_percent) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    DioTranscriptScrollInfo info;
    HRESULT result;
    if (isnan(horizontal_percent) ||
        isnan(vertical_percent)) {
        return E_INVALIDARG;
    }
    if ((horizontal_percent !=
             UIA_ScrollPatternNoScroll &&
         (horizontal_percent < 0.0 ||
          horizontal_percent > 100.0)) ||
        (vertical_percent !=
             UIA_ScrollPatternNoScroll &&
         (vertical_percent < 0.0 ||
          vertical_percent > 100.0))) {
        return DIO_E_ARGUMENT_OUT_OF_RANGE;
    }
    if (horizontal_percent !=
        UIA_ScrollPatternNoScroll) {
        return UIA_E_INVALIDOPERATION;
    }
    if (vertical_percent ==
        UIA_ScrollPatternNoScroll) {
        return S_OK;
    }
    result = dio_transcript_provider_info(
        provider,
        &info);
    if (FAILED(result) ||
        !info.vertically_scrollable) {
        return FAILED(result)
            ? result
            : UIA_E_INVALIDOPERATION;
    }
    return SendMessageW(
               provider->window,
               DIO_WM_TRANSCRIPT_PERCENT,
               0u,
               (LPARAM)&vertical_percent) != 0
        ? S_OK
        : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_horizontal_scroll_percent(
    IScrollProvider *interface_pointer,
    double *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    *output = UIA_ScrollPatternNoScroll;
    return provider->window != NULL &&
           IsWindow(provider->window)
        ? S_OK
        : UIA_E_ELEMENTNOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_vertical_scroll_percent(
    IScrollProvider *interface_pointer,
    double *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    DioTranscriptScrollInfo info;
    HRESULT result;
    if (output == NULL) {
        return E_POINTER;
    }
    result = dio_transcript_provider_info(
        provider,
        &info);
    *output = SUCCEEDED(result)
        ? info.vertical_percent
        : UIA_ScrollPatternNoScroll;
    return result;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_horizontal_view_size(
    IScrollProvider *interface_pointer,
    double *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    *output = 100.0;
    return provider->window != NULL &&
           IsWindow(provider->window)
        ? S_OK
        : UIA_E_ELEMENTNOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_vertical_view_size(
    IScrollProvider *interface_pointer,
    double *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    DioTranscriptScrollInfo info;
    HRESULT result;
    if (output == NULL) {
        return E_POINTER;
    }
    result = dio_transcript_provider_info(
        provider,
        &info);
    *output = SUCCEEDED(result)
        ? info.vertical_view_size
        : 100.0;
    return result;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_horizontally_scrollable(
    IScrollProvider *interface_pointer,
    BOOL *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    if (output == NULL) {
        return E_POINTER;
    }
    *output = FALSE;
    return provider->window != NULL &&
           IsWindow(provider->window)
        ? S_OK
        : UIA_E_ELEMENTNOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE
dio_transcript_vertically_scrollable(
    IScrollProvider *interface_pointer,
    BOOL *output) {
    DioTranscriptProvider *provider =
        dio_transcript_provider_from_scroll(
            interface_pointer);
    DioTranscriptScrollInfo info;
    HRESULT result;
    if (output == NULL) {
        return E_POINTER;
    }
    result = dio_transcript_provider_info(
        provider,
        &info);
    *output = SUCCEEDED(result)
        ? info.vertically_scrollable
        : FALSE;
    return result;
}

static IRawElementProviderSimpleVtbl
DIO_TRANSCRIPT_SIMPLE_VTABLE = {
    dio_transcript_simple_query_interface,
    dio_transcript_simple_add_ref,
    dio_transcript_simple_release,
    dio_transcript_simple_provider_options,
    dio_transcript_simple_pattern_provider,
    dio_transcript_simple_property_value,
    dio_transcript_simple_host_provider};

static IScrollProviderVtbl
DIO_TRANSCRIPT_SCROLL_VTABLE = {
    dio_transcript_scroll_query_interface,
    dio_transcript_scroll_add_ref,
    dio_transcript_scroll_release,
    dio_transcript_scroll,
    dio_transcript_set_scroll_percent,
    dio_transcript_horizontal_scroll_percent,
    dio_transcript_vertical_scroll_percent,
    dio_transcript_horizontal_view_size,
    dio_transcript_vertical_view_size,
    dio_transcript_horizontally_scrollable,
    dio_transcript_vertically_scrollable};

static DioTranscriptProvider *
dio_transcript_provider_create(HWND window) {
    DioTranscriptProvider *provider;
    if (window == NULL) {
        return NULL;
    }
    provider = (DioTranscriptProvider *)calloc(
        1u,
        sizeof(*provider));
    if (provider == NULL) {
        return NULL;
    }
    provider->simple.lpVtbl =
        &DIO_TRANSCRIPT_SIMPLE_VTABLE;
    provider->scroll.lpVtbl =
        &DIO_TRANSCRIPT_SCROLL_VTABLE;
    provider->references = 1;
    provider->window = window;
    return provider;
}

static void dio_transcript_provider_disconnect(
    DioUi *ui,
    HWND window) {
    DioTranscriptProvider *provider =
        ui->transcript_provider;
    if (provider == NULL) {
        return;
    }
    ui->transcript_provider = NULL;
    (void)UiaReturnRawElementProvider(
        window,
        0u,
        0,
        NULL);
    provider->window = NULL;
    (void)dio_transcript_provider_release(provider);
}

static bool dio_uia_name_equals(
    HWND window,
    const wchar_t *expected) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    BSTR name = NULL;
    bool matches = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result = IUIAutomationElement_get_CurrentName(
            element,
            &name);
    }
    if (SUCCEEDED(result) && name != NULL) {
        matches = wcscmp(name, expected) == 0;
    }
    SysFreeString(name);
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return matches;
}

static bool dio_uia_name_matches_window(
    HWND window) {
    wchar_t name[512];
    return
        GetWindowTextW(
            window,
            name,
            (int)_countof(name)) > 0 &&
        dio_uia_name_equals(window, name);
}

static bool dio_uia_name_contains(
    HWND window,
    const wchar_t *first,
    const wchar_t *last,
    size_t minimum_length) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    BSTR name = NULL;
    bool matches = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result = IUIAutomationElement_get_CurrentName(
            element,
            &name);
    }
    if (SUCCEEDED(result) && name != NULL) {
        matches =
            (size_t)SysStringLen(name) >=
                minimum_length &&
            wcsstr(name, first) != NULL &&
            wcsstr(name, last) != NULL;
    }
    SysFreeString(name);
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return matches;
}

static bool dio_uia_control_type_equals(
    HWND window,
    CONTROLTYPEID expected) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    CONTROLTYPEID type = 0;
    bool matches = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result =
            IUIAutomationElement_get_CurrentControlType(
                element,
                &type);
    }
    if (SUCCEEDED(result)) {
        matches = type == expected;
    }
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return matches;
}

static bool dio_uia_keyboard_focusable(
    HWND window) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    BOOL focusable = FALSE;
    bool matches = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result =
            IUIAutomationElement_get_CurrentIsKeyboardFocusable(
                element,
                &focusable);
    }
    if (SUCCEEDED(result)) {
        matches = focusable != FALSE;
    }
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return matches;
}

static bool dio_uia_pattern_available(
    HWND window,
    PATTERNID pattern_id) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    IUnknown *pattern = NULL;
    bool available = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result = IUIAutomationElement_GetCurrentPattern(
            element,
            pattern_id,
            &pattern);
    }
    available =
        SUCCEEDED(result) &&
        pattern != NULL;
    if (pattern != NULL) {
        IUnknown_Release(pattern);
    }
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return available;
}

static bool dio_uia_set_vertical_scroll_percent(
    HWND window,
    double requested) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    IUIAutomationScrollPattern *pattern = NULL;
    double actual = UIA_ScrollPatternNoScroll;
    double view_size = 100.0;
    BOOL scrollable = FALSE;
    bool verified = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result = IUIAutomationElement_GetCurrentPatternAs(
            element,
            UIA_ScrollPatternId,
            &IID_IUIAutomationScrollPattern,
            (void **)&pattern);
    }
    if (SUCCEEDED(result) && pattern != NULL) {
        result = IUIAutomationScrollPattern_SetScrollPercent(
            pattern,
            UIA_ScrollPatternNoScroll,
            requested);
    }
    if (SUCCEEDED(result)) {
        result =
            IUIAutomationScrollPattern_get_CurrentVerticalScrollPercent(
                pattern,
                &actual);
    }
    if (SUCCEEDED(result)) {
        result =
            IUIAutomationScrollPattern_get_CurrentVerticalViewSize(
                pattern,
                &view_size);
    }
    if (SUCCEEDED(result)) {
        result =
            IUIAutomationScrollPattern_get_CurrentVerticallyScrollable(
                pattern,
                &scrollable);
    }
    verified =
        SUCCEEDED(result) &&
        scrollable != FALSE &&
        view_size > 0.0 &&
        view_size < 100.0 &&
        fabs(actual - requested) < 0.5;
    if (pattern != NULL) {
        IUIAutomationScrollPattern_Release(pattern);
    }
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return verified;
}

static bool dio_uia_invalid_name_present(HWND window) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *element = NULL;
    BSTR name = NULL;
    bool present = false;
    HRESULT result = CoCreateInstance(
        &CLSID_CUIAutomation,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void **)&automation);
    if (SUCCEEDED(result) && automation != NULL) {
        result = IUIAutomation_ElementFromHandle(
            automation,
            window,
            &element);
    }
    if (SUCCEEDED(result) && element != NULL) {
        result =
            IUIAutomationElement_get_CurrentName(
                element,
                &name);
    }
    if (SUCCEEDED(result) && name != NULL) {
        present =
            wcsstr(name, L"Invalid") != NULL ||
            wcsstr(
                name,
                L"\u0646\u0627\u0645\u0639\u062a\u0628\u0631") != NULL;
    }
    SysFreeString(name);
    if (element != NULL) {
        IUIAutomationElement_Release(element);
    }
    if (automation != NULL) {
        IUIAutomation_Release(automation);
    }
    return present;
}

static bool dio_button_begin_hover(HWND button) {
    RECT bounds;
    if (!GetClientRect(button, &bounds)) {
        return false;
    }
    (void)SendMessageW(
        button,
        WM_MOUSEMOVE,
        0u,
        MAKELPARAM(
            (bounds.right - bounds.left) / 2,
            (bounds.bottom - bounds.top) / 2));
    InvalidateRect(button, NULL, FALSE);
    return dio_button_is_hot(button);
}

static void dio_button_end_hover(HWND button) {
    (void)SendMessageW(
        button,
        WM_MOUSELEAVE,
        0u,
        0u);
    InvalidateRect(button, NULL, FALSE);
}

static bool dio_capture_overlay_variant(
    DioUi *ui,
    const wchar_t *suffix) {
    wchar_t path[MAX_PATH];
    const wchar_t *locale =
        ui->settings.persian ? L"fa" : L"en";

    (void)CreateDirectoryW(L"out", NULL);
    (void)CreateDirectoryW(L"out\\ui-smoke", NULL);
    if (swprintf_s(
            path,
            _countof(path),
            L"out\\ui-smoke\\overlay-%ls-%ls.bmp",
            locale,
            suffix) < 0) {
        return false;
    }
    RedrawWindow(
        ui->window,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_UPDATENOW |
            RDW_ALLCHILDREN);
    (void)DwmFlush();
    return dio_capture_window_bitmap(
        ui->window,
        path);
}

static bool dio_smoke_accessibility_policy(
    DioUi *ui) {
    static const UINT dpis[] = {96u, 144u, 192u};
    static const wchar_t *const suffixes[] = {
        L"100",
        L"150",
        L"200"};
    const UINT original_dpi = ui->dpi;
    const float original_scale = ui->scale;
    const bool original_high_contrast =
        ui->high_contrast;
    const bool original_reduced_motion =
        ui->settings.reduced_motion;
    const bool original_smoke = ui->smoke;
    const DioUiState original_state =
        ui->model.state;
    const float original_phase = ui->model.phase;
    HWND original_focus = GetFocus();
    wchar_t close_name[128];
    wchar_t status_name[128];
    wchar_t messages_name[
        DIO_ACCESSIBILITY_PROBE_TEXT_CAP];
    wchar_t class_name[32];
    DioUiEvent level_event;
    bool verified =
        AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) !=
        FALSE;
    bool motion_started;
    bool tray_motion_started;
    bool system_motion;
    unsigned int tray_frame_before;
    size_t index;

    dio_update_semantics(ui);
    verified =
        GetWindowTextW(
            ui->close_button,
            close_name,
            (int)_countof(close_name)) > 0 &&
        GetWindowTextW(
            ui->status_semantic,
            status_name,
            (int)_countof(status_name)) > 0 &&
        GetWindowTextW(
            ui->messages_semantic,
            messages_name,
            (int)_countof(messages_name)) > 0 &&
        dio_accessible_name_present(
            ui->close_button) &&
        dio_accessible_name_present(
            ui->status_semantic) &&
        dio_accessible_name_present(
            ui->messages_semantic) &&
        dio_uia_name_equals(
            ui->close_button,
            close_name) &&
        dio_uia_name_equals(
            ui->status_semantic,
            status_name) &&
        dio_uia_name_equals(
            ui->messages_semantic,
            messages_name) &&
        dio_uia_control_type_equals(
            ui->messages_semantic,
            UIA_PaneControlTypeId) &&
        dio_uia_keyboard_focusable(
            ui->messages_semantic) &&
        dio_uia_pattern_available(
            ui->messages_semantic,
            UIA_ScrollPatternId) &&
        !dio_uia_pattern_available(
            ui->messages_semantic,
            UIA_InvokePatternId) &&
        (ui->settings.persian
            ? wcsstr(
                  messages_name,
                  L"\u0648\u0631\u0648\u062f\u06cc \u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646") != NULL &&
              wcsstr(
                  messages_name,
                  L"\u067e\u0627\u0633\u062e \u0635\u0648\u062a\u06cc") != NULL
            : wcsstr(
                  messages_name,
                  L"microphone input") != NULL &&
              wcsstr(
                  messages_name,
                  L"voice output") != NULL);
    verified =
        verified &&
        GetClassNameW(
            ui->close_button,
            class_name,
            (int)_countof(class_name)) > 0 &&
        _wcsicmp(class_name, L"Button") == 0 &&
        (GetWindowLongPtrW(
             ui->close_button,
             GWL_STYLE) &
         WS_TABSTOP) != 0 &&
        dio_uia_control_type_equals(
            ui->close_button,
            UIA_ButtonControlTypeId) &&
        dio_uia_keyboard_focusable(
            ui->close_button) &&
        dio_uia_pattern_available(
            ui->close_button,
            UIA_InvokePatternId);
    (void)SetFocus(ui->messages_semantic);
    verified =
        verified &&
        GetFocus() == ui->messages_semantic &&
        dio_capture_overlay_variant(
            ui,
            L"transcript-keyboard-focus");
    (void)SetFocus(ui->close_button);
    verified =
        verified &&
        GetFocus() == ui->close_button;
    {
        const bool focus_captured =
            dio_capture_overlay_variant(
                ui,
                L"keyboard-focus");
        verified =
            verified && focus_captured;
    }
    {
        const bool close_hovered =
            dio_button_begin_hover(
                ui->close_button);
        const bool hover_captured =
            close_hovered &&
            dio_capture_overlay_variant(
                ui,
                L"close-hover");
        dio_button_end_hover(ui->close_button);
        verified =
            verified && hover_captured;
    }

    for (index = 0u; index < _countof(dpis); ++index) {
        RECT overlay;
        RECT close;
        MONITORINFO monitor;
        ui->dpi = dpis[index];
        ui->scale = (float)ui->dpi / 96.0f;
        verified =
            dio_create_tray_icons(ui) &&
            verified;
        dio_update_tray_tip(ui);
        dio_set_appearance(
            ui,
            original_high_contrast);
        dio_place_overlay(ui);
        ZeroMemory(&monitor, sizeof(monitor));
        monitor.cbSize = sizeof(monitor);
        verified =
            verified &&
            GetWindowRect(ui->window, &overlay) &&
            GetWindowRect(ui->close_button, &close) &&
            GetMonitorInfoW(
                MonitorFromWindow(
                    ui->window,
                    MONITOR_DEFAULTTONEAREST),
                &monitor) &&
            overlay.right - overlay.left ==
                MulDiv(
                    DIO_OVERLAY_WIDTH_DIP,
                    (int)ui->dpi,
                    96) &&
            overlay.left >= monitor.rcWork.left &&
            overlay.top >= monitor.rcWork.top &&
            overlay.right <= monitor.rcWork.right &&
            overlay.bottom <= monitor.rcWork.bottom &&
            close.right - close.left >=
                MulDiv(32, (int)ui->dpi, 96) &&
            close.bottom - close.top >=
                MulDiv(32, (int)ui->dpi, 96) &&
            dio_capture_overlay_variant(
                ui,
                suffixes[index]);
    }

    ui->dpi = original_dpi;
    ui->scale = original_scale;
    verified =
        dio_create_tray_icons(ui) &&
        verified;
    dio_update_tray_tip(ui);
    dio_set_appearance(ui, true);
    dio_place_overlay(ui);
    verified =
        verified &&
        ui->high_contrast &&
        dio_capture_overlay_variant(
            ui,
            L"high-contrast");

    ui->model.state = DIO_UI_LOADING;
    ui->smoke = false;
    ui->settings.reduced_motion = false;
    dio_sync_animation_timer(ui);
    dio_sync_tray_loading_timer(ui);
    system_motion = dio_system_motion_enabled();
    motion_started =
        ui->animation_timer ||
        !system_motion;
    tray_frame_before = ui->tray_loading_frame;
    tray_motion_started =
        !system_motion ||
        (ui->tray_loading_timer &&
         SendMessageW(
             ui->window,
             WM_TIMER,
             DIO_TIMER_TRAY_LOADING,
             0u) == 0 &&
         ui->tray_loading_frame ==
             (tray_frame_before + 1u) %
                 DIO_TRAY_LOADING_FRAME_COUNT);
    ui->settings.reduced_motion = true;
    ui->model.level = 0.8f;
    dio_set_status(ui);
    dio_sync_animation_timer(ui);
    dio_sync_tray_loading_timer(ui);
    ZeroMemory(&level_event, sizeof(level_event));
    level_event.kind = DIO_UI_EVENT_LEVEL;
    level_event.value = 0.9f;
    dio_apply_event(ui, &level_event);
    verified =
        verified &&
        motion_started &&
        tray_motion_started &&
        !ui->animation_timer &&
        !ui->tray_loading_timer &&
        ui->tray_loading_frame == 0u &&
        ui->model.level == 0.0f &&
        ui->model.phase == 0.0f;

    ui->settings.reduced_motion =
        original_reduced_motion;
    ui->smoke = original_smoke;
    ui->model.state = original_state;
    ui->model.phase = original_phase;
    ui->dpi = original_dpi;
    ui->scale = original_scale;
    dio_set_status(ui);
    dio_update_semantics(ui);
    dio_update_tray_tip(ui);
    dio_set_appearance(
        ui,
        original_high_contrast);
    dio_place_overlay(ui);
    dio_sync_animation_timer(ui);
    dio_sync_tray_loading_timer(ui);
    if (IsWindow(original_focus) &&
        IsChild(ui->window, original_focus)) {
        (void)SetFocus(original_focus);
    }
    return verified;
}

static bool dio_capture_settings_variant(
    DioSettingsDialog *dialog,
    const wchar_t *suffix) {
    wchar_t path[MAX_PATH];
    const wchar_t *locale =
        dialog->ui->settings.persian
            ? L"fa"
            : L"en";

    if (swprintf_s(
            path,
            _countof(path),
            L"out\\ui-smoke\\settings-%ls-%ls.bmp",
            locale,
            suffix) < 0) {
        return false;
    }
    RedrawWindow(
        dialog->window,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_UPDATENOW |
            RDW_ALLCHILDREN);
    (void)DwmFlush();
    return dio_capture_window_bitmap(
        dialog->window,
        path);
}

static bool dio_settings_edit_aligned(
    DioSettingsDialog *dialog,
    HWND edit) {
    RECT client;
    RECT formatting;
    const int padding =
        dio_dialog_px(dialog, 8);

    if (!GetClientRect(edit, &client)) {
        return false;
    }
    (void)SendMessageW(
        edit,
        EM_GETRECT,
        0u,
        (LPARAM)&formatting);
    return
        ((DWORD)GetWindowLongPtrW(
            edit,
            GWL_STYLE) &
         ES_MULTILINE) != 0u &&
        abs(formatting.left - padding) <= 2 &&
        abs(
            client.right -
            formatting.right -
            padding) <= 2 &&
        formatting.top > 0 &&
        formatting.bottom < client.bottom &&
        abs(
            formatting.top +
            formatting.bottom -
            client.bottom) <= 2;
}

static bool dio_settings_edits_aligned(
    DioSettingsDialog *dialog) {
    return
        dio_settings_edit_aligned(
            dialog,
            dialog->silence) &&
        dio_settings_edit_aligned(
            dialog,
            dialog->follow_up);
}

static bool dio_settings_single_line_paste(
    DioSettingsDialog *dialog) {
    wchar_t original[128];
    wchar_t normalized[128];
    wchar_t loaded[] = L"1\r\n.5";
    const bool load_verified =
        dio_normalize_single_line(loaded) &&
        wcschr(loaded, L'\r') == NULL &&
        wcschr(loaded, L'\n') == NULL;
    bool verified;

    original[0] = L'\0';
    (void)GetWindowTextW(
        dialog->silence,
        original,
        (int)_countof(original));
    (void)SetWindowTextW(
        dialog->silence,
        L"1\r\n.5");
    verified =
        GetWindowTextW(
            dialog->silence,
            normalized,
            (int)_countof(normalized)) > 0 &&
        wcschr(normalized, L'\r') == NULL &&
        wcschr(normalized, L'\n') == NULL;
    (void)SetWindowTextW(
        dialog->silence,
        original);
    return
        verified &&
        load_verified;
}

static bool dio_settings_checkbox_semantics(
    DioSettingsDialog *dialog) {
    const HWND original_focus = GetFocus();
    const LRESULT original =
        SendMessageW(
            dialog->reduced_motion,
            BM_GETCHECK,
            0u,
            0u);
    bool toggled;

    (void)SetFocus(dialog->reduced_motion);
    (void)SendMessageW(
        dialog->reduced_motion,
        WM_KEYDOWN,
        VK_SPACE,
        0u);
    (void)SendMessageW(
        dialog->reduced_motion,
        WM_KEYUP,
        VK_SPACE,
        0u);
    toggled =
        SendMessageW(
            dialog->reduced_motion,
            BM_GETCHECK,
            0u,
            0u) != original &&
        GetFocus() == dialog->reduced_motion;
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETCHECK,
        (WPARAM)original,
        0u);
    if (IsWindow(original_focus)) {
        (void)SetFocus(original_focus);
    }
    return
        toggled &&
        dio_uia_control_type_equals(
            dialog->follow_up_enabled,
            UIA_CheckBoxControlTypeId) &&
        dio_uia_control_type_equals(
            dialog->reduced_motion,
            UIA_CheckBoxControlTypeId) &&
        dio_uia_pattern_available(
            dialog->follow_up_enabled,
            UIA_TogglePatternId) &&
        dio_uia_pattern_available(
            dialog->reduced_motion,
            UIA_TogglePatternId);
}

static bool dio_capture_settings_states(
    DioSettingsDialog *dialog) {
    const unsigned int original_invalid =
        dialog->invalid_controls;
    const bool follow_up_enabled =
        IsWindowEnabled(dialog->follow_up) != FALSE;
    const bool reduced_motion_enabled =
        IsWindowEnabled(dialog->reduced_motion) != FALSE;
    const LRESULT reduced_motion_checked =
        SendMessageW(
            dialog->reduced_motion,
            BM_GETCHECK,
            0u,
            0u);
    wchar_t original_silence[32];
    bool verified;

    if (GetWindowTextW(
            dialog->silence,
            original_silence,
            (int)_countof(original_silence)) <= 0) {
        return false;
    }
    (void)SetFocus(dialog->silence);
    dio_layout_settings(dialog);
    verified = dio_capture_settings_variant(
        dialog,
        L"field-focus");

    (void)SetWindowTextW(
        dialog->silence,
        L"x");
    (void)SetFocus(dialog->save);
    verified =
        !dio_save_settings_dialog(dialog) &&
        GetFocus() == dialog->silence &&
        (dialog->invalid_controls &
         (1u << DIO_SETTINGS_CONTROL_SILENCE)) != 0u &&
        dio_uia_invalid_name_present(
            dialog->silence) &&
        verified;
    verified =
        dio_capture_settings_variant(
            dialog,
            L"field-invalid") &&
        verified;
    (void)SetWindowTextW(
        dialog->silence,
        original_silence);

    dialog->invalid_controls = original_invalid;
    EnableWindow(dialog->follow_up, FALSE);
    dio_layout_settings(dialog);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"field-disabled") &&
        verified;

    EnableWindow(
        dialog->follow_up,
        follow_up_enabled);
    (void)SetFocus(dialog->save);
    (void)SendMessageW(
        dialog->save,
        BM_SETSTATE,
        TRUE,
        0u);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"button-pressed") &&
        verified;
    (void)SendMessageW(
        dialog->save,
        BM_SETSTATE,
        FALSE,
        0u);
    verified =
        dio_button_begin_hover(
            dialog->save) &&
        dio_capture_settings_variant(
            dialog,
            L"button-hover") &&
        verified;
    dio_button_end_hover(dialog->save);

    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETCHECK,
        BST_UNCHECKED,
        0u);
    (void)SetFocus(dialog->reduced_motion);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"checkbox-unchecked-focus") &&
        verified;
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETCHECK,
        BST_CHECKED,
        0u);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"checkbox-checked") &&
        verified;
    verified =
        dio_button_begin_hover(
            dialog->reduced_motion) &&
        dio_capture_settings_variant(
            dialog,
            L"checkbox-hover") &&
        verified;
    dio_button_end_hover(dialog->reduced_motion);
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETSTATE,
        TRUE,
        0u);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"checkbox-pressed") &&
        verified;
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETSTATE,
        FALSE,
        0u);
    (void)EnableWindow(
        dialog->reduced_motion,
        FALSE);
    verified =
        dio_capture_settings_variant(
            dialog,
            L"checkbox-disabled") &&
        verified;
    (void)EnableWindow(
        dialog->reduced_motion,
        reduced_motion_enabled);
    (void)SendMessageW(
        dialog->reduced_motion,
        BM_SETCHECK,
        (WPARAM)reduced_motion_checked,
        0u);

    (void)SetFocus(dialog->locale);
    verified =
        dio_button_begin_hover(
            dialog->locale) &&
        dio_capture_settings_variant(
            dialog,
            L"selector-hover") &&
        verified;
    dio_button_end_hover(dialog->locale);
    dialog->menu_smoke_ok = false;
    if (SetTimer(
            dialog->window,
            DIO_TIMER_SETTINGS_MENU_SMOKE,
            80u,
            NULL) == 0u) {
        verified = false;
    } else {
        dio_show_settings_choice(
            dialog,
            false);
        verified =
            dialog->menu_smoke_ok &&
            verified;
    }
    dio_layout_settings(dialog);
    return verified;
}

static bool dio_capture_settings_pages(
    DioSettingsDialog *dialog,
    const wchar_t *prefix) {
    static const DioSettingsPage pages[] = {
        DIO_SETTINGS_PAGE_MODEL,
        DIO_SETTINGS_PAGE_SYSTEM_PROMPT,
        DIO_SETTINGS_PAGE_TOOLS};
    static const wchar_t *const suffixes[] = {
        L"tab-model",
        L"tab-system-prompt",
        L"tab-tools"};
    const DioSettingsPage original = dialog->page;
    bool verified = true;
    size_t index;

    for (index = 0u; index < _countof(pages); ++index) {
        wchar_t suffix[96];
        const wchar_t *capture_suffix = suffixes[index];
        HWND primary;
        bool captured;
        if (prefix != NULL &&
            swprintf_s(
                suffix,
                _countof(suffix),
                L"%ls-%ls",
                prefix,
                suffixes[index]) < 0) {
            verified = false;
            continue;
        }
        if (prefix != NULL) {
            capture_suffix = suffix;
        }
        dialog->page = pages[index];
        dialog->scroll_y = 0;
        (void)TabCtrl_SetCurSel(dialog->tabs, (int)pages[index]);
        dio_layout_settings(dialog);
        primary = pages[index] == DIO_SETTINGS_PAGE_MODEL
            ? dialog->base_url
            : pages[index] == DIO_SETTINGS_PAGE_SYSTEM_PROMPT
                ? dialog->system_prompt
                : dialog->mcp_list;
        captured = dio_capture_settings_variant(
            dialog,
            capture_suffix);
        verified =
            captured &&
            IsWindowVisible(primary) &&
            (pages[index] != DIO_SETTINGS_PAGE_MODEL ||
             (((GetWindowLongPtrW(dialog->model, GWL_STYLE) &
                CBS_DROPDOWNLIST) ==
               CBS_DROPDOWN) &&
              ((GetWindowLongPtrW(dialog->reasoning, GWL_STYLE) &
                CBS_DROPDOWNLIST) ==
               CBS_DROPDOWN) &&
              ((GetWindowLongPtrW(dialog->service_tier, GWL_STYLE) &
                CBS_DROPDOWNLIST) ==
               CBS_DROPDOWN))) &&
            verified;
    }
    dialog->page = original;
    dialog->scroll_y = 0;
    (void)TabCtrl_SetCurSel(dialog->tabs, (int)original);
    dio_layout_settings(dialog);
    return verified;
}

static bool dio_settings_smoke_rect(
    DioSettingsDialog *dialog,
    UINT dpi,
    RECT *placement) {
    const DWORD style = (DWORD)GetWindowLongPtrW(
        dialog->window,
        GWL_STYLE);
    const DWORD extended_style =
        (DWORD)GetWindowLongPtrW(
            dialog->window,
            GWL_EXSTYLE);
    MONITORINFO monitor;
    RECT frame;
    int width;
    int height;

    frame.left = 0;
    frame.top = 0;
    frame.right = MulDiv(520, (int)dpi, 96);
    frame.bottom = MulDiv(
        DIO_SETTINGS_PREFERRED_HEIGHT_DIP,
        (int)dpi,
        96);
    if (!AdjustWindowRectExForDpi(
            &frame,
            style,
            FALSE,
            extended_style,
            dpi) &&
        !AdjustWindowRectEx(
            &frame,
            style,
            FALSE,
            extended_style)) {
        return false;
    }
    ZeroMemory(&monitor, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(
            MonitorFromWindow(
                dialog->window,
                MONITOR_DEFAULTTONEAREST),
            &monitor)) {
        return false;
    }
    width = frame.right - frame.left;
    height = frame.bottom - frame.top;
    placement->left =
        monitor.rcWork.left +
        (monitor.rcWork.right -
         monitor.rcWork.left -
         width) /
            2;
    placement->top =
        monitor.rcWork.top +
        (monitor.rcWork.bottom -
         monitor.rcWork.top -
         height) /
            2;
    placement->right = placement->left + width;
    placement->bottom = placement->top + height;
    return dio_fit_settings_rect(
        placement,
        dpi);
}

static bool dio_settings_compact_tab_cycle(
    DioSettingsDialog *dialog) {
    HWND controls[DIO_SETTINGS_CONTROL_COUNT + 1] = {
        dialog->locale,
        dialog->microphone,
        dialog->silence,
        dialog->follow_up_enabled,
        dialog->follow_up,
        dialog->reduced_motion,
        dialog->tabs,
        dialog->cancel,
        dialog->save};
    static const DioSettingsControl slots[
        DIO_SETTINGS_CONTROL_COUNT + 1] = {
            DIO_SETTINGS_CONTROL_LOCALE,
            DIO_SETTINGS_CONTROL_MICROPHONE,
            DIO_SETTINGS_CONTROL_SILENCE,
            DIO_SETTINGS_CONTROL_FOLLOW_UP_ENABLED,
            DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS,
            DIO_SETTINGS_CONTROL_REDUCED_MOTION,
            DIO_SETTINGS_CONTROL_COUNT,
            DIO_SETTINGS_CONTROL_CANCEL,
            DIO_SETTINGS_CONTROL_SAVE};
    MSG message;
    size_t index;

    (void)SetFocus(controls[0]);
    dio_ensure_settings_control_visible(
        dialog,
        controls[0]);
    for (index = 1u;
         index < _countof(controls);
         ++index) {
        ZeroMemory(&message, sizeof(message));
        message.hwnd = GetFocus();
        message.message = WM_KEYDOWN;
        message.wParam = VK_TAB;
        if (!IsDialogMessageW(
                dialog->window,
                &message) ||
            GetFocus() != controls[index]) {
            return false;
        }
        dio_ensure_settings_control_visible(
            dialog,
            controls[index]);
        if ((slots[index] == DIO_SETTINGS_CONTROL_COUNT &&
             !IsWindowVisible(controls[index])) ||
            (slots[index] != DIO_SETTINGS_CONTROL_COUNT &&
             !dio_settings_view_control_visible(
                 dialog->view,
                 slots[index]))) {
            return false;
        }
    }
    return true;
}

static bool dio_smoke_settings_matrix(
    DioSettingsDialog *dialog) {
    static const UINT dpis[] = {96u, 144u, 192u};
    static const wchar_t *const suffixes[] = {
        L"100",
        L"150",
        L"200"};
    const UINT original_dpi = dialog->dpi;
    const bool original_high_contrast =
        dialog->high_contrast;
    bool verified = true;
    bool scroll_verified = false;
    bool page_keys_verified = false;
    bool compact_tab_verified = false;
    bool validation_recovery_verified = false;
    bool other_pages_verified = false;
    size_t index;

    for (index = 0u; index < _countof(dpis); ++index) {
        MONITORINFO monitor;
        RECT placement;
        RECT window;
        dialog->scroll_y = 0;
        if (!dio_settings_smoke_rect(
                dialog,
                dpis[index],
                &placement)) {
            verified = false;
            continue;
        }
        (void)SendMessageW(
            dialog->window,
            WM_DPICHANGED,
            MAKEWPARAM(dpis[index], dpis[index]),
            (LPARAM)&placement);
        ZeroMemory(&monitor, sizeof(monitor));
        monitor.cbSize = sizeof(monitor);
        verified =
            GetWindowRect(
                dialog->window,
                &window) &&
            GetMonitorInfoW(
                MonitorFromWindow(
                    dialog->window,
                    MONITOR_DEFAULTTONEAREST),
                &monitor) &&
            dialog->dpi == dpis[index] &&
            window.left >= monitor.rcWork.left &&
            window.top >= monitor.rcWork.top &&
            window.right <= monitor.rcWork.right &&
            window.bottom <= monitor.rcWork.bottom &&
            dio_settings_edits_aligned(dialog) &&
            dio_capture_settings_variant(
                dialog,
                suffixes[index]) &&
            verified;
        verified = dio_capture_settings_pages(
            dialog,
            suffixes[index]) && verified;
        if (dpis[index] == 192u) {
            RECT save_before;
            RECT save_after;
            RECT client;
            RECT matrix_window;
            bool save_before_valid;
            if (GetWindowRect(
                    dialog->window,
                    &matrix_window) &&
                GetClientRect(
                    dialog->window,
                    &client)) {
                const int chrome_height =
                    matrix_window.bottom -
                    matrix_window.top -
                    client.bottom;
                const int target_height =
                    MulDiv(480, (int)dpis[index], 96) +
                    chrome_height;
                if (matrix_window.bottom -
                        matrix_window.top >
                    target_height) {
                    (void)SetWindowPos(
                        dialog->window,
                        NULL,
                        0,
                        0,
                        matrix_window.right -
                            matrix_window.left,
                        target_height,
                        SWP_NOMOVE |
                            SWP_NOACTIVATE |
                            SWP_NOZORDER);
                }
            }
            dio_scroll_settings(dialog, 0);
            page_keys_verified =
                dio_settings_page_key(
                    dialog,
                    VK_NEXT) &&
                dialog->scroll_y > 0 &&
                dio_settings_page_key(
                    dialog,
                    VK_PRIOR) &&
                dialog->scroll_y == 0;
            save_before_valid = GetWindowRect(
                dialog->save,
                &save_before) != FALSE;
            (void)SetFocus(
                dialog->reduced_motion);
            dio_ensure_settings_control_visible(
                dialog,
                dialog->reduced_motion);
            if (GetWindowRect(
                    dialog->save,
                    &save_after) &&
                GetClientRect(
                    dialog->window,
                    &client)) {
                POINT points[2] = {
                    {
                        save_after.left,
                        save_after.top},
                    {
                        save_after.right,
                        save_after.bottom}};
                (void)MapWindowPoints(
                    HWND_DESKTOP,
                    dialog->window,
                    points,
                    2u);
                scroll_verified =
                    dialog->scroll_max > 0 &&
                    dialog->scroll_y > 0 &&
                    save_before_valid &&
                    EqualRect(
                        &save_before,
                        &save_after) &&
                    points[0].y >= 0 &&
                    points[1].y <= client.bottom &&
                    GetFocus() ==
                        dialog->reduced_motion &&
                    dio_capture_settings_variant(
                        dialog,
                        L"200-footer");
            }
            (void)SetFocus(dialog->locale);
            dio_ensure_settings_control_visible(
                dialog,
                dialog->locale);
            {
                RECT locale_bounds;
                RECT locale_client;
                if (GetWindowRect(
                        dialog->locale,
                        &locale_bounds) &&
                    GetClientRect(
                        dialog->window,
                        &locale_client)) {
                    POINT points[2] = {
                        {
                            locale_bounds.left,
                            locale_bounds.top},
                        {
                            locale_bounds.right,
                            locale_bounds.bottom}};
                    (void)MapWindowPoints(
                        HWND_DESKTOP,
                        dialog->window,
                        points,
                        2u);
                    scroll_verified =
                        scroll_verified &&
                        points[0].y >= 0 &&
                        points[1].y <=
                            locale_client.bottom;
                } else {
                    scroll_verified = false;
                }
            }
            dio_scroll_settings(dialog, 0);
            scroll_verified =
                scroll_verified &&
                dialog->scroll_y == 0;
            {
                RECT compact = {
                    0,
                    0,
                    MulDiv(360, (int)dpis[index], 96),
                    MulDiv(240, (int)dpis[index], 96)};
                RECT compact_client;
                const DWORD compact_style =
                    (DWORD)GetWindowLongPtrW(
                        dialog->window,
                        GWL_STYLE);
                const DWORD compact_extended_style =
                    (DWORD)GetWindowLongPtrW(
                        dialog->window,
                        GWL_EXSTYLE);
                if ((!AdjustWindowRectExForDpi(
                         &compact,
                         compact_style,
                         FALSE,
                         compact_extended_style,
                         dpis[index]) &&
                     !AdjustWindowRectEx(
                         &compact,
                         compact_style,
                         FALSE,
                         compact_extended_style)) ||
                    !SetWindowPos(
                        dialog->window,
                        NULL,
                        0,
                        0,
                        compact.right - compact.left,
                        compact.bottom - compact.top,
                        SWP_NOMOVE |
                            SWP_NOACTIVATE |
                            SWP_NOZORDER) ||
                    !GetClientRect(
                        dialog->window,
                        &compact_client)) {
                    verified = false;
                } else {
                    verified =
                        compact_client.right >=
                            MulDiv(
                                340,
                                (int)dpis[index],
                                96) &&
                        compact_client.bottom >=
                            MulDiv(
                                220,
                                (int)dpis[index],
                                96) &&
                        dio_capture_settings_variant(
                            dialog,
                            L"200-compact") &&
                        verified;
                    dialog->page = DIO_SETTINGS_PAGE_TOOLS;
                    dialog->scroll_y = 0;
                    (void)TabCtrl_SetCurSel(
                        dialog->tabs,
                        DIO_SETTINGS_PAGE_TOOLS);
                    dio_layout_settings(dialog);
                    other_pages_verified =
                        dialog->scroll_max > 0 &&
                        dio_settings_page_key(dialog, VK_NEXT) &&
                        dialog->scroll_y > 0;
                    (void)SetFocus(dialog->mcp_add);
                    dio_ensure_settings_control_visible(
                        dialog,
                        dialog->mcp_add);
                    other_pages_verified =
                        other_pages_verified &&
                        dialog->scroll_y > 0 &&
                        dio_capture_settings_variant(
                            dialog,
                            L"200-compact-tools-scrolled");
                    dialog->page = DIO_SETTINGS_PAGE_GENERAL;
                    dialog->scroll_y = 0;
                    (void)TabCtrl_SetCurSel(
                        dialog->tabs,
                        DIO_SETTINGS_PAGE_GENERAL);
                    dio_layout_settings(dialog);
                    compact_tab_verified =
                        dio_settings_compact_tab_cycle(
                            dialog);
                    {
                        wchar_t original_silence[32];
                        if (GetWindowTextW(
                                dialog->silence,
                                original_silence,
                                (int)_countof(
                                    original_silence)) > 0) {
                            (void)SetWindowTextW(
                                dialog->silence,
                                L"x");
                            (void)SetFocus(dialog->save);
                            {
                                const bool save_failed =
                                    !dio_save_settings_dialog(
                                        dialog);
                                const bool focus_recovered =
                                    GetFocus() ==
                                    dialog->silence;
                                const bool visible =
                                    dio_settings_view_control_visible(
                                        dialog->view,
                                        DIO_SETTINGS_CONTROL_SILENCE);
                                const bool uia_invalid =
                                    dio_uia_invalid_name_present(
                                        dialog->silence);
                                validation_recovery_verified =
                                    save_failed &&
                                    focus_recovered &&
                                    visible &&
                                    uia_invalid &&
                                    dio_capture_settings_variant(
                                        dialog,
                                        L"200-invalid-recovery");
                            }
                            (void)SetWindowTextW(
                                dialog->silence,
                                original_silence);
                        }
                    }
                }
            }
        }
    }

    {
        RECT placement;
        if (dio_settings_smoke_rect(
                dialog,
                original_dpi,
                &placement)) {
            dialog->scroll_y = 0;
            (void)SendMessageW(
                dialog->window,
                WM_DPICHANGED,
                MAKEWPARAM(
                    original_dpi,
                    original_dpi),
                (LPARAM)&placement);
            dio_settings_set_appearance(
                dialog,
                true);
            (void)SetFocus(
                dialog->reduced_motion);
            verified =
                dialog->high_contrast &&
                GetFocus() ==
                    dialog->reduced_motion &&
                dio_capture_settings_variant(
                    dialog,
                    L"high-contrast") &&
                verified;
            dio_settings_set_appearance(
                dialog,
                original_high_contrast);
        } else {
            verified = false;
        }
    }
    (void)SetFocus(dialog->locale);
    dialog->smoke_matrix_base = verified;
    dialog->smoke_matrix_scroll = scroll_verified;
    dialog->smoke_matrix_page_keys = page_keys_verified;
    dialog->smoke_matrix_compact_tab = compact_tab_verified;
    dialog->smoke_matrix_validation = validation_recovery_verified;
    dialog->smoke_matrix_other_pages = other_pages_verified;
    return
        verified &&
        scroll_verified &&
        page_keys_verified &&
        compact_tab_verified &&
        validation_recovery_verified &&
        other_pages_verified;
}

static bool dio_capture_settings_evidence(
    DioSettingsDialog *dialog) {
    const wchar_t *locale =
        dialog->ui->settings.persian ? L"fa" : L"en";
    wchar_t bitmap_path[MAX_PATH];
    wchar_t manifest_path[MAX_PATH];
    wchar_t face[LF_FACESIZE] = L"";
    char face_utf8[LF_FACESIZE * 4];
    char manifest[2048];
    RECT client;
    RECT edit_client;
    RECT edit_format;
    HDC dc;
    HGDIOBJ previous = NULL;
    HANDLE file;
    size_t microphones;
    int manifest_length;
    int converted;
    bool bitmap_written;
    bool manifest_written;
    bool msaa_names;
    bool uia_names;
    bool selector_semantics;
    bool edit_contract;
    bool checkbox_semantics;
    bool matrix_verified;
    bool pages_verified;
    bool number_policy_verified;
    bool endpoint_policy_verified;
    bool dpi_verified;
    bool states_verified;
    bool focus_verified;
    int focus_id;
    int first_tab_id;
    int last_tab_id;
    int default_id;
    int tab_count = 0;
    HWND tab;
    HWND next_tab;
    wchar_t locale_name[512];
    wchar_t microphone_name[512];
    const bool fa = dialog->ui->settings.persian;

    (void)CreateDirectoryW(L"out", NULL);
    (void)CreateDirectoryW(L"out\\ui-smoke", NULL);
    if (swprintf_s(
            bitmap_path,
            _countof(bitmap_path),
            L"out\\ui-smoke\\settings-%ls.bmp",
            locale) < 0 ||
        swprintf_s(
            manifest_path,
            _countof(manifest_path),
            L"out\\ui-smoke\\settings-%ls-evidence.txt",
            locale) < 0) {
        return false;
    }
    pages_verified = dio_capture_settings_pages(dialog, NULL);
    number_policy_verified = dio_settings_number_policy();
    endpoint_policy_verified = dio_model_endpoint_policy() &&
        dio_model_stale_generation_policy(dialog) &&
        dio_model_timeout_policy(dialog);
    dpi_verified = dio_smoke_settings_matrix(dialog);
    states_verified = dio_capture_settings_states(dialog);
    matrix_verified = pages_verified && number_policy_verified &&
        endpoint_policy_verified && dpi_verified && states_verified;
    edit_contract =
        dio_settings_edits_aligned(dialog) &&
        dio_settings_single_line_paste(dialog);
    checkbox_semantics =
        dio_settings_checkbox_semantics(dialog);
    RedrawWindow(
        dialog->window,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    (void)DwmFlush();
    bitmap_written = dio_capture_window_bitmap(
        dialog->window,
        bitmap_path);
    dc = GetDC(dialog->locale);
    if (dc != NULL && dialog->font != NULL) {
        previous = SelectObject(dc, dialog->font);
        (void)GetTextFaceW(dc, _countof(face), face);
        if (previous != NULL) {
            (void)SelectObject(dc, previous);
        }
        ReleaseDC(dialog->locale, dc);
    }
    converted = WideCharToMultiByte(
        CP_UTF8,
        0u,
        face,
        -1,
        face_utf8,
        (int)sizeof(face_utf8),
        NULL,
        NULL);
    if (converted <= 0) {
        (void)strcpy_s(
            face_utf8,
            sizeof(face_utf8),
            "unknown");
    }
    GetClientRect(dialog->window, &client);
    GetClientRect(dialog->silence, &edit_client);
    (void)SendMessageW(
        dialog->silence,
        EM_GETRECT,
        0u,
        (LPARAM)&edit_format);
    microphones = dialog->microphone_count;
    msaa_names =
        dio_accessible_name_present(dialog->locale) &&
        dio_accessible_name_present(dialog->microphone) &&
        dio_accessible_name_present(dialog->silence) &&
        dio_accessible_name_present(
            dialog->follow_up_enabled) &&
        dio_accessible_name_present(dialog->follow_up) &&
        dio_accessible_name_present(
            dialog->reduced_motion) &&
        dio_accessible_name_present(dialog->save) &&
        dio_accessible_name_present(dialog->cancel);
    uia_names =
        dio_settings_choice_name(
            dialog,
            false,
            locale_name,
            _countof(locale_name)) &&
        dio_settings_choice_name(
            dialog,
            true,
            microphone_name,
            _countof(microphone_name));
    uia_names =
        uia_names &&
        dio_uia_name_equals(
            dialog->locale,
            locale_name) &&
        dio_uia_name_equals(
            dialog->microphone,
            microphone_name) &&
        dio_uia_name_equals(
            dialog->silence,
            fa
                ? L"\u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"End-of-command delay, seconds") &&
        dio_uia_name_equals(
            dialog->follow_up,
            fa
                ? L"\u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc\u060c \u062b\u0627\u0646\u06cc\u0647"
                : L"Follow-up time, seconds") &&
        dio_uia_name_equals(
            dialog->follow_up_enabled,
            fa
                ? L"\u0641\u0639\u0627\u0644\u200c\u06a9\u0631\u062f\u0646 \u06af\u0648\u0634\u200c\u062f\u0627\u062f\u0646 \u0628\u0647 \u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc"
                : L"Enable follow-up listening after each reply") &&
        dio_uia_name_equals(
            dialog->reduced_motion,
            fa
                ? L"\u06a9\u0627\u0647\u0634 \u062d\u0631\u06a9\u062a\u200c\u0647\u0627\u06cc \u063a\u06cc\u0631\u0636\u0631\u0648\u0631\u06cc"
                : L"Reduce non-essential motion") &&
        dio_uia_name_matches_window(dialog->save) &&
        dio_uia_name_matches_window(dialog->cancel);
    selector_semantics =
        dio_uia_control_type_equals(
            dialog->locale,
            UIA_ComboBoxControlTypeId) &&
        dio_uia_control_type_equals(
            dialog->microphone,
            UIA_ComboBoxControlTypeId);
    focus_id = GetDlgCtrlID(GetFocus());
    first_tab_id = GetDlgCtrlID(
        GetNextDlgTabItem(
            dialog->window,
            NULL,
            FALSE));
    tab = dialog->locale;
    last_tab_id = GetDlgCtrlID(tab);
    while (tab_count <
           DIO_SETTINGS_CONTROL_COUNT + 1) {
        next_tab = GetNextDlgTabItem(
            dialog->window,
            tab,
            FALSE);
        if (next_tab == NULL ||
            next_tab == dialog->locale) {
            break;
        }
        tab = next_tab;
        last_tab_id = GetDlgCtrlID(tab);
        tab_count += 1;
    }
    default_id = LOWORD(SendMessageW(
            dialog->window,
            DM_GETDEFID,
            0u,
            0u));
    focus_verified =
        focus_id == DIO_SETTINGS_LOCALE &&
        first_tab_id == DIO_SETTINGS_LOCALE &&
        last_tab_id == IDOK &&
        tab_count >= 7 &&
        default_id == IDOK;
    manifest_length = sprintf_s(
        manifest,
        sizeof(manifest),
        "window=DioVoiceSettingsWindow\n"
        "locale=%s\n"
        "dpi=%u\n"
        "client_px=%ldx%ld\n"
        "gdi_font=%s\n"
        "private_font=%s\n"
        "microphone_choices=%zu\n"
        "accessible_names=%s\n"
        "msaa_names=%s\n"
        "uia_names=%s\n"
        "keyboard_focus=%s\n"
        "focus_ids=%d,%d,%d,%d\n"
        "dpi_matrix=%s\n"
        "settings_scroll=%s\n"
        "page_keys=%s\n"
        "compact_tab_cycle=%s\n"
        "validation_recovery=%s\n"
        "matrix_components=pages:%u,number:%u,endpoint:%u,dpi:%u,states:%u\n"
        "dpi_components=base:%u,scroll:%u,page_keys:%u,compact_tab:%u,validation:%u,other_pages:%u\n"
        "selector_semantics=%s\n"
        "edit_content_alignment=%s\n"
        "edit_rect=%ld,%ld,%ld,%ld/%ld,%ld,%ld,%ld\n"
        "single_line_paste=%s\n"
        "checkbox_semantics=%s\n"
        "checkbox_painter=cui-owner-visual-native-input\n"
        "selector_menu_capture=%s\n"
        "pointer_hover=%s\n"
        "scrollbar_theme=%s\n"
        "control_states=%s\n"
        "high_contrast_branch=%s\n"
        "high_contrast_os=not-mutated\n"
        "high_contrast=%u\n"
        "capture_backend=PrintWindow-cropped\n"
        "capture=%s\n",
        dialog->ui->settings.persian ? "fa" : "en",
        dialog->dpi,
        client.right,
        client.bottom,
        face_utf8,
        GetFileAttributesW(dialog->ui->paths.font) !=
                INVALID_FILE_ATTRIBUTES
            ? "present"
            : "missing",
        microphones,
        msaa_names && uia_names
            ? "verified"
            : "missing",
        msaa_names ? "verified" : "missing",
        uia_names ? "verified" : "missing",
        focus_verified ? "verified" : "failed",
        focus_id,
        first_tab_id,
        last_tab_id,
        default_id,
        matrix_verified
            ? "96-144-192-plus-compact-structural"
            : "failed",
        matrix_verified ? "verified" : "failed",
        matrix_verified ? "verified" : "failed",
        matrix_verified ? "verified" : "failed",
        matrix_verified
            ? "focus-scroll-uia-name-help"
            : "failed",
        pages_verified ? 1u : 0u,
        number_policy_verified ? 1u : 0u,
        endpoint_policy_verified ? 1u : 0u,
        dpi_verified ? 1u : 0u,
        states_verified ? 1u : 0u,
        dialog->smoke_matrix_base ? 1u : 0u,
        dialog->smoke_matrix_scroll ? 1u : 0u,
        dialog->smoke_matrix_page_keys ? 1u : 0u,
        dialog->smoke_matrix_compact_tab ? 1u : 0u,
        dialog->smoke_matrix_validation ? 1u : 0u,
        dialog->smoke_matrix_other_pages ? 1u : 0u,
        selector_semantics
            ? "combobox-with-chevron"
            : "failed",
        edit_contract
            ? "96-144-192-centered-8dip-padding"
            : "failed",
        edit_client.left,
        edit_client.top,
        edit_client.right,
        edit_client.bottom,
        edit_format.left,
        edit_format.top,
        edit_format.right,
        edit_format.bottom,
        edit_contract
            ? "crlf-normalized"
            : "failed",
        checkbox_semantics
            ? "uia-toggle-keyboard"
            : "failed",
        dialog->menu_smoke_ok
            ? "native-hmenu-work-area"
            : "failed",
        matrix_verified ? "verified" : "failed",
        dialog->high_contrast
            ? "system"
            : "DarkMode_Explorer",
        matrix_verified
            ? "field-button-selector-checkbox"
            : "failed",
        matrix_verified
            ? "forced-regression"
            : "failed",
        dialog->high_contrast ? 1u : 0u,
        bitmap_written ? "written" : "failed");
    file = CreateFileW(
        manifest_path,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    manifest_written =
        file != INVALID_HANDLE_VALUE &&
        manifest_length > 0 &&
        dio_write_bytes(
            file,
            manifest,
            (DWORD)manifest_length);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    return
        bitmap_written &&
        manifest_written &&
        msaa_names &&
        uia_names &&
        selector_semantics &&
        edit_contract &&
        checkbox_semantics &&
        focus_verified &&
        matrix_verified;
}

static bool dio_capture_shell_evidence(
    DioUi *ui,
    bool shell_policy_verified,
    bool conversation_verified,
    bool accessibility_verified,
    bool scroll_verified,
    bool state_captures_verified) {
    const wchar_t *locale =
        ui->settings.persian ? L"fa" : L"en";
    wchar_t bitmap_path[MAX_PATH];
    wchar_t manifest_path[MAX_PATH];
    char manifest[2048];
    HANDLE file;
    int manifest_length;
    size_t icon_count = 0u;
    size_t loading_frame_count =
        ui->tray_icons[DIO_UI_LOADING] != NULL
            ? 1u
            : 0u;
    size_t index;
    bool bitmap_written;
    bool manifest_written;
    bool overlay_chrome_verified;

    {
        HRGN region = CreateRectRgn(0, 0, 1, 1);
        DWM_WINDOW_CORNER_PREFERENCE corner =
            DWMWCP_DEFAULT;
        overlay_chrome_verified =
            region != NULL &&
            GetWindowRgn(ui->window, region) == ERROR &&
            SUCCEEDED(DwmGetWindowAttribute(
                ui->window,
                DWMWA_WINDOW_CORNER_PREFERENCE,
                &corner,
                sizeof(corner))) &&
            corner == DWMWCP_ROUND &&
            ui->overlay_chrome_ok;
        if (region != NULL) {
            DeleteObject(region);
        }
    }

    for (index = 0u;
         index < _countof(ui->tray_icons);
         ++index) {
        if (ui->tray_icons[index] != NULL) {
            icon_count += 1u;
        }
    }
    for (index = 0u;
         index < _countof(ui->tray_loading_frames);
         ++index) {
        if (ui->tray_loading_frames[index] != NULL) {
            loading_frame_count += 1u;
        }
    }
    (void)CreateDirectoryW(L"out", NULL);
    (void)CreateDirectoryW(L"out\\ui-smoke", NULL);
    if (swprintf_s(
            bitmap_path,
            _countof(bitmap_path),
            L"out\\ui-smoke\\overlay-%ls.bmp",
            locale) < 0 ||
        swprintf_s(
            manifest_path,
            _countof(manifest_path),
            L"out\\ui-smoke\\shell-%ls-evidence.txt",
            locale) < 0) {
        return false;
    }
    RedrawWindow(
        ui->window,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_UPDATENOW |
            RDW_ALLCHILDREN);
    (void)DwmFlush();
    bitmap_written = dio_capture_window_bitmap(
        ui->window,
        bitmap_path);
    manifest_length = sprintf_s(
        manifest,
        sizeof(manifest),
        "window=DioVoiceOverlayWindow\n"
        "locale=%s\n"
        "dpi=%u\n"
        "tray_icons=%zu/9\n"
        "tray_loading_frames=%zu/%u\n"
        "tray_loading_animation=%ums-clockwise-reduced-motion-aware\n"
        "tray_menu_capture=%s\n"
        "overlay_corners=%s\n"
        "window_region=%s\n"
        "close_painter=native-button-subclass-paint\n"
        "close_visual=cui-vector-close\n"
        "close_uia=button-focusable-invoke\n"
        "close_paints=%u\n"
        "close_paint_result=%d\n"
        "passive_reopen_suppression=%s\n"
        "outside_click_dismiss=%s\n"
        "outside_input=raw-input\n"
        "fresh_error_reset=%s\n"
        "tray_double_click=debounced\n"
        "conversation_rows=%s\n"
        "metadata_rows=%s\n"
        "user_provenance=%s\n"
        "assistant_provenance=%s\n"
        "provenance_affordance=passive-neutral-icon-no-container\n"
        "provenance_interaction=passive-icon-no-invoke\n"
        "transcript_storage=%s\n"
        "assistant_stream=%s\n"
        "transcript_scroll=%s\n"
        "transcript_follow=%s\n"
        "transcript_scrollbar=physical-right-14dip-neutral-2dip\n"
        "transcript_uia=%s\n"
        "transcript_matrix=%s\n"
        "transcript_idle_hold=focus-or-away-from-tail\n"
        "transcript_input_edges=precision-reversal-cancel-empty-guard\n"
        "transcript_uia_contract=full-name-range-errors-property-events\n"
        "accessible_names=%s\n"
        "semantic_projection=full-session-no-truncation\n"
        "keyboard_focus=%s\n"
        "pointer_hover=%s\n"
        "close_target=32dip\n"
        "dpi_awareness=per-monitor-v2\n"
        "dpi_matrix=%s\n"
        "high_contrast_branch=%s\n"
        "high_contrast_os=not-mutated\n"
        "reduced_motion=%s\n"
        "state_captures=%s\n"
        "capture_backend=PrintWindow-cropped\n"
        "capture=%s\n",
        ui->settings.persian ? "fa" : "en",
        ui->dpi,
        icon_count,
        loading_frame_count,
        (unsigned int)DIO_TRAY_LOADING_FRAME_COUNT,
        (unsigned int)DIO_TRAY_LOADING_INTERVAL_MS,
        ui->tray_menu_smoke_ok
            ? "native-hmenu-work-area"
            : "failed",
        overlay_chrome_verified
            ? "dwm-antialiased-8dip"
            : "failed",
        overlay_chrome_verified
            ? "none"
            : "failed",
        ui->close_paint_count,
        (int)ui->close_paint_result,
        shell_policy_verified
            ? "verified"
            : "failed",
        shell_policy_verified
            ? "verified"
            : "failed",
        shell_policy_verified
            ? "verified"
            : "failed",
        conversation_verified
            ? "2"
            : "failed",
        conversation_verified
            ? "0"
            : "failed",
        conversation_verified
            ? "microphone"
            : "failed",
        conversation_verified
            ? "voice"
            : "failed",
        scroll_verified
            ? "dynamic-no-eviction"
            : "failed",
        scroll_verified
            ? "ordered-delta-no-snapshot-cap"
            : "failed",
        scroll_verified
            ? "wheel-keyboard-thumb-drag"
            : "failed",
        scroll_verified
            ? "tail-follow-preserve-away"
            : "failed",
        scroll_verified
            ? "pane-focusable-scrollpattern"
            : "failed",
        scroll_verified
            ? "96-144-192-high-contrast"
            : "failed",
        accessibility_verified
            ? "verified"
            : "failed",
        accessibility_verified
            ? "verified"
            : "failed",
        accessibility_verified
            ? "verified"
            : "failed",
        accessibility_verified
            ? "96-144-192-structural"
            : "failed",
        accessibility_verified
            ? "forced-regression"
            : "failed",
        accessibility_verified
            ? "verified"
            : "failed",
        state_captures_verified
            ? "all-nine-states"
            : "failed",
        bitmap_written ? "written" : "failed");
    file = CreateFileW(
        manifest_path,
        GENERIC_WRITE,
        0u,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    manifest_written =
        file != INVALID_HANDLE_VALUE &&
        manifest_length > 0 &&
        dio_write_bytes(
            file,
            manifest,
            (DWORD)manifest_length);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    return
        bitmap_written &&
        manifest_written &&
        icon_count == _countof(ui->tray_icons) &&
        loading_frame_count ==
            DIO_TRAY_LOADING_FRAME_COUNT &&
        ui->close_paint_count > 0u &&
        ui->close_paint_result == CUI_OK &&
        ui->tray_menu_smoke_ok &&
        overlay_chrome_verified &&
        shell_policy_verified &&
        conversation_verified &&
        accessibility_verified &&
        scroll_verified &&
        state_captures_verified;
}

static bool dio_fit_settings_rect(
    RECT *rect,
    UINT dpi) {
    MONITORINFO monitor;
    const int margin = MulDiv(
        DIO_SETTINGS_WINDOW_MARGIN_DIP,
        (int)dpi,
        96);
    int width;
    int height;
    int max_width;
    int max_height;

    ZeroMemory(&monitor, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    if (rect == NULL ||
        !GetMonitorInfoW(
            MonitorFromRect(
                rect,
                MONITOR_DEFAULTTONEAREST),
            &monitor)) {
        return false;
    }
    width = rect->right - rect->left;
    height = rect->bottom - rect->top;
    max_width =
        monitor.rcWork.right -
        monitor.rcWork.left -
        margin * 2;
    max_height =
        monitor.rcWork.bottom -
        monitor.rcWork.top -
        margin * 2;
    if (max_width <= 0 || max_height <= 0) {
        return false;
    }
    if (width > max_width) {
        width = max_width;
    }
    if (height > max_height) {
        height = max_height;
    }
    if (rect->left < monitor.rcWork.left + margin) {
        rect->left = monitor.rcWork.left + margin;
    }
    if (rect->top < monitor.rcWork.top + margin) {
        rect->top = monitor.rcWork.top + margin;
    }
    if (rect->left + width >
        monitor.rcWork.right - margin) {
        rect->left =
            monitor.rcWork.right - margin - width;
    }
    if (rect->top + height >
        monitor.rcWork.bottom - margin) {
        rect->top =
            monitor.rcWork.bottom - margin - height;
    }
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
    return true;
}

static void dio_show_settings(DioUi *ui, bool vault_required) {
    const DWORD extended_style =
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    const DWORD style =
        WS_POPUP | WS_CAPTION | WS_SYSMENU |
        WS_CLIPCHILDREN | WS_VSCROLL;
    DioSettingsDialog *dialog;
    MONITORINFO monitor;
    RECT frame;
    MSG message;
    HWND previous_foreground;
    HWND previous_focus;
    HWND last_dialog_focus;
    bool owner_was_enabled;
    int result;
    int width;
    int height;
    int x;
    int y;
    RECT placement;

    if (IsWindow(ui->settings_window)) {
        DioSettingsDialog *existing = (DioSettingsDialog *)GetWindowLongPtrW(
            ui->settings_window,
            GWLP_USERDATA);
        ShowWindow(ui->settings_window, SW_RESTORE);
        SetForegroundWindow(ui->settings_window);
        if (vault_required && existing != NULL) {
            existing->page = DIO_SETTINGS_PAGE_MODEL;
            existing->scroll_y = 0;
            (void)TabCtrl_SetCurSel(
                existing->tabs,
                DIO_SETTINGS_PAGE_MODEL);
            dio_layout_settings(existing);
            (void)SetFocus(existing->vault_password);
            dio_ensure_settings_control_visible(
                existing,
                existing->vault_password);
        } else {
            SetFocus(GetDlgItem(
                ui->settings_window,
                DIO_SETTINGS_LOCALE));
        }
        return;
    }
    if (!dio_register_settings_class(ui->instance)) {
        return;
    }
    dialog = (DioSettingsDialog *)calloc(1u, sizeof(*dialog));
    if (dialog == NULL) {
        return;
    }
    dialog->ui = ui;
    dio_agent_profile_init(&dialog->profile);
    if (dialog->profile.system_prompt == NULL ||
        !dio_agent_profile_copy(&dialog->profile, &ui->profile)) {
        dio_agent_profile_free(&dialog->profile);
        free(dialog);
        return;
    }
    dialog->dpi = ui->dpi != 0u ? ui->dpi : 96u;
    frame.left = 0;
    frame.top = 0;
    frame.right = MulDiv(520, (int)dialog->dpi, 96);
    frame.bottom = MulDiv(
        DIO_SETTINGS_PREFERRED_HEIGHT_DIP,
        (int)dialog->dpi,
        96);
    if (!AdjustWindowRectExForDpi(
            &frame,
            style,
            FALSE,
            extended_style,
            dialog->dpi)) {
        (void)AdjustWindowRectEx(
            &frame,
            style,
            FALSE,
            extended_style);
    }
    width = frame.right - frame.left;
    height = frame.bottom - frame.top;
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(
            MonitorFromWindow(
                ui->window,
                MONITOR_DEFAULTTONEAREST),
            &monitor)) {
        monitor.rcWork.left = 0;
        monitor.rcWork.top = 0;
        monitor.rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        monitor.rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    x = monitor.rcWork.left +
        ((monitor.rcWork.right - monitor.rcWork.left) - width) / 2;
    y = monitor.rcWork.top +
        ((monitor.rcWork.bottom - monitor.rcWork.top) - height) / 2;
    if (x < monitor.rcWork.left) {
        x = monitor.rcWork.left;
    }
    if (y < monitor.rcWork.top) {
        y = monitor.rcWork.top;
    }
    placement.left = x;
    placement.top = y;
    placement.right = x + width;
    placement.bottom = y + height;
    (void)dio_fit_settings_rect(
        &placement,
        dialog->dpi);
    previous_foreground = GetForegroundWindow();
    previous_focus = GetFocus();
    owner_was_enabled = IsWindowEnabled(ui->window) != FALSE;
    dialog->window = CreateWindowExW(
        extended_style,
        DIO_SETTINGS_CLASS,
        ui->settings.persian
            ? L"\u062a\u0646\u0638\u06cc\u0645\u0627\u062a DIO Voice"
            : L"DIO Voice settings",
        style,
        placement.left,
        placement.top,
        placement.right - placement.left,
        placement.bottom - placement.top,
        ui->window,
        NULL,
        ui->instance,
        dialog);
    if (dialog->window == NULL) {
        dio_agent_profile_free(&dialog->profile);
        free(dialog);
        return;
    }
    if (owner_was_enabled) {
        EnableWindow(ui->window, FALSE);
    }
    ShowWindow(dialog->window, SW_SHOW);
    SetForegroundWindow(dialog->window);
    if (vault_required) {
        dialog->page = DIO_SETTINGS_PAGE_MODEL;
        dialog->scroll_y = 0;
        (void)TabCtrl_SetCurSel(
            dialog->tabs,
            DIO_SETTINGS_PAGE_MODEL);
        dio_layout_settings(dialog);
        SetFocus(dialog->vault_password);
        dio_ensure_settings_control_visible(
            dialog,
            dialog->vault_password);
    } else {
        SetFocus(dialog->locale);
    }
    last_dialog_focus = GetFocus();
    if (ui->settings_smoke) {
        (void)SetTimer(
            dialog->window,
            DIO_TIMER_SETTINGS_SMOKE,
            900u,
            NULL);
    }
    while (IsWindow(dialog->window)) {
        result = GetMessageW(&message, NULL, 0u, 0u);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage((int)message.wParam);
            }
            break;
        }
        if (message.message == WM_KEYDOWN &&
            (message.hwnd == dialog->window ||
             IsChild(dialog->window, message.hwnd)) &&
            dio_settings_page_key(
                dialog,
                message.wParam)) {
            continue;
        }
        if (!IsDialogMessageW(dialog->window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetFocus() != last_dialog_focus) {
            last_dialog_focus = GetFocus();
            dio_ensure_settings_control_visible(
                dialog,
                last_dialog_focus);
        }
    }
    if (IsWindow(dialog->window)) {
        DestroyWindow(dialog->window);
    }
    if (owner_was_enabled && IsWindow(ui->window)) {
        EnableWindow(ui->window, TRUE);
    }
    if (IsWindow(ui->window) && IsWindowVisible(ui->window)) {
        SetForegroundWindow(ui->window);
        if (IsWindow(previous_focus) &&
            IsChild(ui->window, previous_focus)) {
            SetFocus(previous_focus);
        }
    } else if (IsWindow(previous_foreground) &&
               IsWindowVisible(previous_foreground)) {
        SetForegroundWindow(previous_foreground);
    }
    free(dialog);
}

static LRESULT CALLBACK dio_settings_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    DioSettingsDialog *dialog =
        (DioSettingsDialog *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        dialog = (DioSettingsDialog *)create->lpCreateParams;
        dialog->window = window;
        (void)SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            (LONG_PTR)dialog);
    }
    if (dialog == NULL) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    switch (message) {
    case WM_CREATE: {
        CuiWin32ContextDesc graphics;
        dialog->dpi = GetDpiForWindow(window);
        if (dialog->dpi == 0u) {
            dialog->dpi = 96u;
        }
        dialog->scale = (float)dialog->dpi / 96.0f;
        dialog->high_contrast = cui_win32_high_contrast();
        dialog->default_button_id = IDOK;
        ZeroMemory(&graphics, sizeof(graphics));
        graphics.window = window;
        graphics.theme = &dialog->ui->theme;
        graphics.persian_font_path =
            GetFileAttributesW(dialog->ui->paths.font) !=
                    INVALID_FILE_ATTRIBUTES
                ? dialog->ui->paths.font
                : NULL;
        graphics.scale = dialog->scale;
        graphics.high_contrast = dialog->high_contrast;
        graphics.icon_pack = cui_icons_default();
        if (FAILED(CoCreateInstance(
                &CLSID_AccPropServices,
                NULL,
                CLSCTX_INPROC_SERVER,
                &IID_IAccPropServices,
                (void **)&dialog->accessibility)) ||
            cui_win32_context_create(
                &graphics,
                &dialog->graphics) != CUI_OK ||
            !dio_settings_view_create(
                dialog->graphics,
                &dialog->view)) {
            return -1;
        }
        dialog->ui->settings_window = window;
        dio_settings_recreate_brushes(dialog);
        if (!dio_create_settings_controls(dialog) ||
            !dio_settings_annotate_controls(dialog)) {
            return -1;
        }
        dio_settings_apply_appearance(dialog);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        CuiResult result;
        BeginPaint(window, &paint);
        result = dialog->view != NULL
            ? dio_settings_view_draw(dialog->view)
            : CUI_OK;
        EndPaint(window, &paint);
        if (result == CUI_TARGET_RECREATE) {
            cui_win32_context_discard_target(dialog->graphics);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
    case WM_MEASUREITEM:
        if (dio_measure_menu_item(
                (MEASUREITEMSTRUCT *)lparam)) {
            return TRUE;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *draw =
            (const DRAWITEMSTRUCT *)lparam;
        if (dio_draw_menu_item(draw)) {
            return TRUE;
        }
        if (draw != NULL &&
            (draw->CtlID == IDOK ||
             draw->CtlID == IDCANCEL ||
             draw->CtlID == DIO_SETTINGS_LOCALE ||
             draw->CtlID ==
                 DIO_SETTINGS_MICROPHONE)) {
            dio_draw_settings_button(dialog, draw);
            return TRUE;
        }
        break;
    }
    case WM_MENUCHAR: {
        LRESULT result;
        if (dio_menu_char(wparam, lparam, &result)) {
            return result;
        }
        break;
    }
    case DM_GETDEFID:
        return MAKELRESULT(
            dialog->default_button_id,
            DC_HASDEFID);
    case DM_SETDEFID:
        if (GetDlgItem(window, LOWORD(wparam)) != NULL) {
            dialog->default_button_id = LOWORD(wparam);
            return TRUE;
        }
        return FALSE;
    case WM_NOTIFY: {
        const NMHDR *notice = (const NMHDR *)lparam;
        if (notice != NULL && notice->idFrom == DIO_SETTINGS_TABS &&
            notice->code == TCN_SELCHANGE) {
            const int selected = TabCtrl_GetCurSel(dialog->tabs);
            if (selected >= DIO_SETTINGS_PAGE_GENERAL &&
                selected < DIO_SETTINGS_PAGE_COUNT) {
                HWND focus;
                dialog->page = (DioSettingsPage)selected;
                dialog->scroll_y = 0;
                dio_layout_settings(dialog);
                focus = dialog->page == DIO_SETTINGS_PAGE_GENERAL
                    ? dialog->locale
                    : dialog->page == DIO_SETTINGS_PAGE_MODEL
                        ? dialog->base_url
                        : dialog->page == DIO_SETTINGS_PAGE_SYSTEM_PROMPT
                            ? dialog->system_prompt
                            : dialog->mcp_list;
                (void)SetFocus(focus);
            }
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case DIO_SETTINGS_LOCALE:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_show_settings_choice(
                    dialog,
                    false);
            }
            return 0;
        case DIO_SETTINGS_MICROPHONE:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_show_settings_choice(
                    dialog,
                    true);
            }
            return 0;
        case DIO_SETTINGS_FOLLOW_UP_ENABLED:
            if (HIWORD(wparam) == BN_CLICKED) {
                const bool enabled =
                    SendMessageW(
                        dialog->follow_up_enabled,
                        BM_GETCHECK,
                        0u,
                        0) == BST_CHECKED;
                EnableWindow(dialog->follow_up, enabled);
                if (!enabled) {
                    const unsigned int invalid_mask =
                        1u <<
                        DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS;
                    const bool was_invalid =
                        (dialog->invalid_controls &
                         invalid_mask) != 0u;
                    dialog->invalid_controls &=
                        ~invalid_mask;
                    if (was_invalid) {
                        dio_settings_invalid_accessibility(
                            dialog,
                            dialog->follow_up,
                            false);
                    }
                }
                dio_layout_settings(dialog);
            }
            return 0;
        case DIO_SETTINGS_BASE_URL:
            if (HIWORD(wparam) == EN_CHANGE) {
                wchar_t endpoint[DIO_AGENT_BASE_URL_CAP];
                if (dialog->changing_endpoint) {
                    return 0;
                }
                (void)GetWindowTextW(
                    dialog->base_url,
                    endpoint,
                    (int)_countof(endpoint));
                if (_wcsicmp(
                        endpoint,
                        dialog->discovery_endpoint) != 0) {
                    dialog->changing_endpoint = true;
                    (void)wcscpy_s(
                        dialog->discovery_endpoint,
                        _countof(dialog->discovery_endpoint),
                        endpoint);
                    (void)SendMessageW(
                        dialog->model,
                        CB_RESETCONTENT,
                        0u,
                        0u);
                    (void)SetWindowTextW(dialog->model, L"");
                    (void)SetWindowTextW(dialog->api_key, L"");
                    dialog->secret_dirty =
                        dio_agent_profile_detach_api_key_secret(
                            &dialog->profile,
                            dialog->detached_api_key_secret_id,
                            _countof(dialog->detached_api_key_secret_id));
                    dialog->discovery_anonymous = true;
                    dialog->changing_endpoint = false;
                }
                dio_schedule_model_discovery(dialog);
            }
            return 0;
        case DIO_SETTINGS_API_KEY:
            if (HIWORD(wparam) == EN_CHANGE) {
                if (dialog->changing_endpoint) {
                    return 0;
                }
                dialog->secret_dirty = true;
                dio_schedule_model_discovery(dialog);
            }
            return 0;
        case DIO_SETTINGS_VAULT_ACTION:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_vault_unlock_or_create(dialog);
            }
            return 0;
        case DIO_SETTINGS_VAULT_CHANGE:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_vault_change_master_password(dialog);
            }
            return 0;
        case DIO_SETTINGS_VAULT_RESET:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_vault_reset_dialog(dialog);
            }
            return 0;
        case DIO_SETTINGS_PROMPT_RESET:
            if (HIWORD(wparam) == BN_CLICKED) {
                DioAgentProfile *defaults =
                    (DioAgentProfile *)calloc(1u, sizeof(*defaults));
                if (defaults != NULL) {
                    dio_agent_profile_init(defaults);
                }
                if (defaults != NULL && defaults->system_prompt != NULL) {
                    (void)SetWindowTextW(
                        dialog->system_prompt,
                        defaults->system_prompt);
                }
                if (defaults != NULL) {
                    dio_agent_profile_free(defaults);
                    free(defaults);
                }
            }
            return 0;
        case DIO_SETTINGS_MCP_TRANSPORT:
            if (HIWORD(wparam) == CBN_SELCHANGE && !dialog->changing_mcp) {
                (void)SetWindowTextW(dialog->mcp_token, L"");
                (void)SetWindowTextW(dialog->mcp_environment, L"");
                dialog->mcp_secret_dirty =
                    dialog->mcp_index >= 0 &&
                    (size_t)dialog->mcp_index < dialog->profile.mcp_server_count &&
                    dialog->profile.mcp_servers[dialog->mcp_index].secret_id[0] != L'\0';
                dio_mcp_update_transport(dialog);
            }
            return 0;
        case DIO_SETTINGS_MCP_TOKEN:
        case DIO_SETTINGS_MCP_ENVIRONMENT:
            if (HIWORD(wparam) == EN_CHANGE && !dialog->changing_mcp) {
                dialog->mcp_secret_dirty = true;
            }
            return 0;
        case DIO_SETTINGS_MCP_LIST:
            if (HIWORD(wparam) == LBN_SELCHANGE) {
                dio_mcp_show_selection(
                    dialog,
                    (int)SendMessageW(
                        dialog->mcp_list,
                        LB_GETCURSEL,
                        0u,
                        0u));
            }
            return 0;
        case DIO_SETTINGS_MCP_ADD:
            if (HIWORD(wparam) == BN_CLICKED) {
                (void)dio_mcp_upsert(dialog);
            }
            return 0;
        case DIO_SETTINGS_MCP_REMOVE:
            if (HIWORD(wparam) == BN_CLICKED) {
                dio_mcp_remove_selection(dialog);
            }
            return 0;
        case DIO_SETTINGS_SILENCE:
        case DIO_SETTINGS_FOLLOW_UP:
            if (HIWORD(wparam) == EN_CHANGE) {
                dio_normalize_settings_edit(
                    dialog,
                    (HWND)lparam);
                const DioSettingsControl control =
                    LOWORD(wparam) == DIO_SETTINGS_SILENCE
                        ? DIO_SETTINGS_CONTROL_SILENCE
                        : DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS;
                const unsigned int invalid_mask =
                    1u << (unsigned int)control;
                const bool was_invalid =
                    (dialog->invalid_controls &
                     invalid_mask) != 0u;
                dialog->invalid_controls &=
                    ~invalid_mask;
                if (was_invalid) {
                    dio_settings_invalid_accessibility(
                        dialog,
                        (HWND)lparam,
                        false);
                }
            }
            if (HIWORD(wparam) == EN_CHANGE ||
                HIWORD(wparam) == EN_SETFOCUS ||
                HIWORD(wparam) == EN_KILLFOCUS) {
                dio_layout_settings(dialog);
            }
            return 0;
        case IDOK:
            if (HIWORD(wparam) == BN_CLICKED &&
                dio_save_settings_dialog(dialog)) {
                DestroyWindow(window);
            }
            return 0;
        case IDCANCEL:
            if (HIWORD(wparam) == BN_CLICKED) {
                DestroyWindow(window);
            }
            return 0;
        default:
            break;
        }
        break;
    case DIO_WM_MODELS:
        dio_apply_discovered_models(
            dialog,
            (unsigned long long)wparam,
            (const wchar_t *)lparam);
        return 0;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wparam;
        SetTextColor(
            dc,
            dialog->high_contrast
                ? GetSysColor(COLOR_WINDOWTEXT)
                : dio_settings_color(
                    dialog,
                    CUI_ROLE_FOREGROUND));
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)dialog->background_brush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wparam;
        const HWND control = (HWND)lparam;
        const COLORREF field = dialog->high_contrast
            ? GetSysColor(COLOR_WINDOW)
            : dio_settings_color(dialog, CUI_ROLE_MUTED);
        SetTextColor(
            dc,
            dialog->high_contrast
                ? GetSysColor(COLOR_WINDOWTEXT)
                : dio_settings_color(
                    dialog,
                    IsWindowEnabled(control)
                        ? CUI_ROLE_FOREGROUND
                        : CUI_ROLE_MUTED_FOREGROUND));
        SetBkColor(dc, field);
        return (LRESULT)dialog->field_brush;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO scroll;
        int position = dialog->scroll_y;
        ZeroMemory(&scroll, sizeof(scroll));
        scroll.cbSize = sizeof(scroll);
        scroll.fMask = SIF_ALL;
        (void)GetScrollInfo(
            window,
            SB_VERT,
            &scroll);
        switch (LOWORD(wparam)) {
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = dialog->scroll_max;
            break;
        case SB_LINEUP:
            position -= DIO_SETTINGS_SCROLL_LINE_DIP;
            break;
        case SB_LINEDOWN:
            position += DIO_SETTINGS_SCROLL_LINE_DIP;
            break;
        case SB_PAGEUP:
            position -= (int)scroll.nPage;
            break;
        case SB_PAGEDOWN:
            position += (int)scroll.nPage;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = scroll.nTrackPos;
            break;
        default:
            return 0;
        }
        dio_scroll_settings(dialog, position);
        return 0;
    }
    case WM_MOUSEWHEEL:
        dialog->wheel_remainder +=
            GET_WHEEL_DELTA_WPARAM(wparam);
        while (dialog->wheel_remainder >=
               WHEEL_DELTA) {
            dio_scroll_settings(
                dialog,
                dialog->scroll_y -
                    DIO_SETTINGS_SCROLL_LINE_DIP);
            dialog->wheel_remainder -= WHEEL_DELTA;
        }
        while (dialog->wheel_remainder <=
               -WHEEL_DELTA) {
            dio_scroll_settings(
                dialog,
                dialog->scroll_y +
                    DIO_SETTINGS_SCROLL_LINE_DIP);
            dialog->wheel_remainder += WHEEL_DELTA;
        }
        return 0;
    case WM_DPICHANGED:
        dialog->dpi = HIWORD(wparam);
        dialog->scale = (float)dialog->dpi / 96.0f;
        dio_settings_apply_font(dialog);
        dio_settings_set_appearance(
            dialog,
            dialog->high_contrast);
        if (lparam != 0) {
            RECT suggested = *(const RECT *)lparam;
            (void)dio_fit_settings_rect(
                &suggested,
                dialog->dpi);
            (void)SetWindowPos(
                window,
                NULL,
                suggested.left,
                suggested.top,
                suggested.right - suggested.left,
                suggested.bottom - suggested.top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        dio_layout_settings(dialog);
        return 0;
    case WM_SIZE:
        dio_layout_settings(dialog);
        return 0;
    case WM_TIMER:
        if ((UINT_PTR)wparam == DIO_TIMER_MODEL_DISCOVERY) {
            (void)KillTimer(window, DIO_TIMER_MODEL_DISCOVERY);
            dio_request_model_discovery(dialog);
            return 0;
        }
        if ((UINT_PTR)wparam ==
            DIO_TIMER_SETTINGS_MENU_SMOKE) {
            wchar_t path[MAX_PATH];
            (void)KillTimer(
                window,
                DIO_TIMER_SETTINGS_MENU_SMOKE);
            dialog->menu_smoke_ok =
                dialog->menu_smoke_ok &&
                swprintf_s(
                    path,
                    _countof(path),
                    L"out\\ui-smoke\\settings-%ls-selector-menu.bmp",
                    dialog->ui->settings.persian
                        ? L"fa"
                        : L"en") > 0 &&
                dio_capture_popup_menu(path);
            (void)EndMenu();
            return 0;
        }
        if ((UINT_PTR)wparam ==
            DIO_TIMER_SETTINGS_SMOKE) {
            (void)KillTimer(
                window,
                DIO_TIMER_SETTINGS_SMOKE);
            dialog->ui->settings_smoke_ok =
                dio_capture_settings_evidence(dialog);
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        dio_settings_apply_appearance(dialog);
        return 0;
    case WM_DESTROY:
        (void)KillTimer(
            window,
            DIO_TIMER_SETTINGS_SMOKE);
        (void)KillTimer(
            window,
            DIO_TIMER_SETTINGS_MENU_SMOKE);
        (void)KillTimer(window, DIO_TIMER_MODEL_DISCOVERY);
        if (dialog->ui->settings_window == window) {
            dialog->ui->settings_window = NULL;
        }
        dio_settings_view_destroy(dialog->view);
        dialog->view = NULL;
        cui_win32_context_destroy(dialog->graphics);
        dialog->graphics = NULL;
        if (dialog->font != NULL) {
            DeleteObject(dialog->font);
            dialog->font = NULL;
        }
        if (dialog->field_brush != NULL) {
            DeleteObject(dialog->field_brush);
            dialog->field_brush = NULL;
        }
        if (dialog->background_brush != NULL) {
            DeleteObject(dialog->background_brush);
            dialog->background_brush = NULL;
        }
        if (dialog->accessibility != NULL) {
            IAccPropServices_Release(
                dialog->accessibility);
            dialog->accessibility = NULL;
        }
        dio_agent_profile_free(&dialog->profile);
        dialog->window = NULL;
        return 0;
    case WM_NCDESTROY:
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void dio_smoke_text(
    DioUiEvent *event,
    DioUiEventKind kind,
    const wchar_t *text) {
    ZeroMemory(event, sizeof(*event));
    event->kind = kind;
    dio_copy_text(event->text, _countof(event->text), text);
}

static void dio_smoke_long_text(
    DioUiEvent *event,
    bool persian_ui) {
    const wchar_t *phrase = persian_ui
        ? L"\u0645\u0633\u06cc\u0631 C:\\DIO Voice\\logs\\agent.json \u0648 https://dio.local/session?id=42 \u062f\u0631\u0633\u062a \u0627\u0633\u062a \xd83c\xdfa4 \u2014 English tail. "
        : L"\u0627\u06cc\u0646 \u067e\u0627\u0633\u062e C:\\DIO Voice\\logs\\agent.json \u0648 https://dio.local/session?id=42 \u0631\u0627 \u0628\u0631\u0631\u0633\u06cc \u0645\u06cc\u200c\u06a9\u0646\u062f \xd83c\xdfa4 \u2014 mixed-direction tail. ";
    const size_t phrase_length = wcslen(phrase);
    size_t used = 0u;

    ZeroMemory(event, sizeof(*event));
    event->kind = DIO_UI_EVENT_ASSISTANT_TEXT;
    while (used + phrase_length < _countof(event->text)) {
        (void)memcpy(
            event->text + used,
            phrase,
            phrase_length * sizeof(phrase[0]));
        used += phrase_length;
    }
    event->text[used] = L'\0';
}

static bool dio_smoke_wrap_boundary_policy(
    DioUi *ui) {
    DioUiEvent event;
    wchar_t text[96];
    const float tagged_width =
        dio_view_message_text_width(
            (float)DIO_OVERLAY_WIDTH_DIP,
            DIO_MESSAGE_USER);
    const float full_width =
        dio_view_message_text_width(
            (float)DIO_OVERLAY_WIDTH_DIP,
            DIO_MESSAGE_ANNOUNCEMENT);
    const float font_size =
        13.0f + (ui->model.rtl ? 0.5f : 0.0f);
    float tagged_height = 0.0f;
    float full_height = 0.0f;
    float expected;
    size_t length;

    text[0] = L'\0';
    for (length = 0u;
         length + 2u < _countof(text);
         length += 2u) {
        text[length] = L'W';
        text[length + 1u] = L' ';
        text[length + 2u] = L'\0';
        tagged_height = dio_measure_text_height(
            ui,
            text,
            font_size,
            ui->model.rtl,
            tagged_width);
        full_height = dio_measure_text_height(
            ui,
            text,
            font_size,
            ui->model.rtl,
            full_width);
        if (tagged_height > full_height + 0.1f) {
            break;
        }
    }
    if (tagged_height <= full_height + 0.1f) {
        return false;
    }

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        text);
    dio_apply_event(ui, &event);
    expected =
        (float)DIO_OVERLAY_HEADER_DIP +
        4.0f +
        (tagged_height + 12.0f < 34.0f
            ? 34.0f
            : tagged_height + 12.0f) +
        12.0f;
    if (expected < (float)DIO_OVERLAY_MIN_HEIGHT_DIP) {
        expected = (float)DIO_OVERLAY_MIN_HEIGHT_DIP;
    }
    if (expected > (float)DIO_OVERLAY_MAX_HEIGHT_DIP) {
        expected = (float)DIO_OVERLAY_MAX_HEIGHT_DIP;
    }
    return
        tagged_width < full_width &&
        fabsf(
            dio_overlay_height(ui) -
            expected) < 0.01f &&
        dio_capture_overlay_variant(
            ui,
            L"wrap-boundary");
}

static bool dio_smoke_conversation_policy(
    DioUi *ui) {
    DioUiEvent event;
    const bool fa = ui->settings.persian;
    float before_metadata;
    bool wrap_verified;
    bool verified;

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa ? L"\u0646\u0648\u0628\u062a \u0627\u0648" : L"Turn on");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa ? L"\u0646\u0648\u0628\u062a \u0627\u0648\u0644" : L"Turn one");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_CHIP,
        fa
            ? L"\u062f\u0631\u06cc\u0627\u0641\u062a \u0634\u062f"
            : L"Received");
    event.chip = DIO_CHIP_ACK;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        fa ? L"\u067e\u0627\u0633\u062e \u0627\u0648\u0644" : L"First reply");
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_FOLLOW_UP;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa ? L"\u0646\u0648\u0628\u062a \u062f\u0648" : L"Turn tw");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa ? L"\u0646\u0648\u0628\u062a \u062f\u0648\u0645" : L"Turn two");
    dio_apply_event(ui, &event);
    verified =
        ui->model.message_count == 3u &&
        wcscmp(
            ui->model.messages[0].text,
            fa
                ? L"\u0646\u0648\u0628\u062a \u0627\u0648\u0644"
                : L"Turn one") == 0 &&
        wcscmp(
            ui->model.messages[2].text,
            fa
                ? L"\u0646\u0648\u0628\u062a \u062f\u0648\u0645"
                : L"Turn two") == 0 &&
        (ui->model.messages[0].flags &
         DIO_MESSAGE_ACCEPTED) != 0u &&
        ui->model.messages[1].kind ==
            DIO_MESSAGE_ASSISTANT &&
        (ui->model.messages[1].flags &
         DIO_MESSAGE_AUDIO_OUTPUT) == 0u &&
        ui->model.messages[2].kind ==
            DIO_MESSAGE_USER &&
        (ui->model.messages[2].flags &
         DIO_MESSAGE_ACCEPTED) == 0u;

    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_THINKING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_CHIP,
        fa ? L"\u0635\u062f\u0627" : L"Voice");
    event.chip = DIO_CHIP_TTS;
    dio_apply_event(ui, &event);
    verified =
        verified &&
        ui->pending_audio_output &&
        (ui->model.messages[1].flags &
         DIO_MESSAGE_AUDIO_OUTPUT) == 0u;
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        fa ? L"\u067e\u0627\u0633\u062e" : L"Reply");
    dio_apply_event(ui, &event);
    verified =
        verified &&
        ui->model.message_count == 4u &&
        ui->model.messages[3].kind ==
            DIO_MESSAGE_ASSISTANT &&
        (ui->model.messages[1].flags &
         DIO_MESSAGE_AUDIO_OUTPUT) == 0u &&
        (ui->model.messages[3].flags &
         DIO_MESSAGE_AUDIO_OUTPUT) != 0u &&
        !ui->pending_audio_output;
    wrap_verified = dio_smoke_wrap_boundary_policy(ui);
    verified = verified && wrap_verified;

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa
            ? L"\u062c\u0644\u0633\u0647\u0654 \u0627\u0645\u0631\u0648\u0632"
            : L"Summarize today's");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        fa
            ? L"\u062c\u0644\u0633\u0647\u0654 \u0627\u0645\u0631\u0648\u0632 \u0631\u0627 \u062e\u0644\u0627\u0635\u0647 \u06a9\u0646"
            : L"Summarize today's meeting");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_CHIP,
        fa
            ? L"\u062f\u0631\u06cc\u0627\u0641\u062a \u0634\u062f"
            : L"Received");
    event.chip = DIO_CHIP_ACK;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_THINKING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        fa ? L"\u0627\u0646\u062c\u0627\u0645" : L"Done");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        fa
            ? L"\u0627\u0646\u062c\u0627\u0645 \u0634\u062f."
            : L"Done.");
    dio_apply_event(ui, &event);
    before_metadata = dio_overlay_height(ui);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_CHIP,
        fa ? L"\u0635\u062f\u0627" : L"Voice");
    event.chip = DIO_CHIP_TTS;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_SPEAKING;
    dio_apply_event(ui, &event);
    return
        verified &&
        ui->model.message_count == 2u &&
        ui->model.messages[0].kind ==
            DIO_MESSAGE_USER &&
        ui->model.messages[1].kind ==
            DIO_MESSAGE_ASSISTANT &&
        (ui->model.messages[0].flags &
         (DIO_MESSAGE_MIC_INPUT |
          DIO_MESSAGE_ACCEPTED)) ==
            (DIO_MESSAGE_MIC_INPUT |
             DIO_MESSAGE_ACCEPTED) &&
        (ui->model.messages[1].flags &
         DIO_MESSAGE_AUDIO_OUTPUT) != 0u &&
        fabsf(
            dio_overlay_height(ui) -
            before_metadata) < 0.01f;
}

static bool dio_smoke_transcript_scroll_policy(
    DioUi *ui) {
    static const UINT matrix_dpis[] = {144u, 192u};
    static const wchar_t *const matrix_suffixes[] = {
        L"long-session-150",
        L"long-session-200"};
    const UINT original_dpi = ui->dpi;
    const float original_scale = ui->scale;
    const bool original_high_contrast =
        ui->high_contrast;
    const bool original_smoke = ui->smoke;
    const wchar_t *const chunk_phrase =
        ui->settings.persian
            ? L" این بخش از پاسخٔ طولانی بدون حذف در تاریخچه می‌ماند."
            : L" This part of the long answer remains in the session without truncation.";
    const wchar_t *const begin_marker =
        ui->settings.persian
            ? L" آغاز پاسخ بلند"
            : L" BEGIN-LONG-ANSWER";
    const wchar_t *const end_marker =
        ui->settings.persian
            ? L" پایان پاسخ بلند"
            : L" END-LONG-ANSWER";
    const wchar_t *const session_marker =
        ui->settings.persian
            ? L"آغاز جلسه · "
            : L"BEGIN-SESSION · ";
    const size_t chunk_target = 4600u;
    DioUiEvent event;
    RECT transcript;
    RECT stable_before;
    RECT stable_after_delta;
    RECT stable_after_state;
    POINT pointer;
    UINT wheel_lines = 3u;
    float before_y;
    float before_max;
    float half_y;
    bool verified = true;
    size_t turn;
    size_t chunk;
    size_t matrix;

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    verified =
        ui->model.message_count == 0u &&
        dio_view_scroll_y(ui->view) == 0.0f &&
        dio_view_scroll_max(ui->view) == 0.0f &&
        (GetWindowLongPtrW(
             ui->messages_semantic,
             GWL_STYLE) &
         WS_TABSTOP) == 0 &&
        !dio_uia_keyboard_focusable(
            ui->messages_semantic) &&
        dio_uia_name_equals(
            ui->messages_semantic,
            ui->settings.persian
                ? L"گفت‌وگو"
                : L"Conversation");
    (void)SendMessageW(
        ui->messages_semantic,
        WM_LBUTTONDOWN,
        MK_LBUTTON,
        MAKELPARAM(1, 1));
    (void)SendMessageW(
        ui->messages_semantic,
        WM_LBUTTONUP,
        0u,
        MAKELPARAM(1, 1));
    verified =
        GetFocus() != ui->messages_semantic &&
        verified;
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);

    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        ui->settings.persian
            ? L"یک پاسخ توضیحی بده"
            : L"Give me a detailed answer");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_DELTA,
        ui->settings.persian
            ? L"پاسخ از اینجا شروع می‌شود."
            : L"The answer starts here.");
    dio_apply_event(ui, &event);
    verified =
        GetWindowRect(ui->window, &stable_before) &&
        verified;
    dio_smoke_long_text(
        &event,
        ui->settings.persian);
    event.kind = DIO_UI_EVENT_ASSISTANT_DELTA;
    dio_apply_event(ui, &event);
    verified =
        GetWindowRect(
            ui->window,
            &stable_after_delta) &&
        EqualRect(
            &stable_before,
            &stable_after_delta) &&
        verified;
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_SPEAKING;
    event.text[0] = L'\0';
    dio_apply_event(ui, &event);
    verified =
        GetWindowRect(
            ui->window,
            &stable_after_state) &&
        EqualRect(
            &stable_before,
            &stable_after_state) &&
        dio_uia_name_equals(
            ui->status_semantic,
            ui->model.status) &&
        dio_capture_overlay_variant(
            ui,
            L"long-single-turn-stable") &&
        verified;

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);

    for (turn = 0u; turn < 8u; ++turn) {
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_UI_EVENT_USER_TEXT;
        (void)swprintf_s(
            event.text,
            _countof(event.text),
            ui->settings.persian
                ? L"%lsپرسش شمارهٔ %u از این جلسهٔ طولانی"
                : L"%lsQuestion %u from this long session",
            turn == 0u ? session_marker : L"",
            (unsigned int)(turn + 1u));
        dio_apply_event(ui, &event);

        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_UI_EVENT_ASSISTANT_TEXT;
        (void)swprintf_s(
            event.text,
            _countof(event.text),
            ui->settings.persian
                ? L"پاسخ شمارهٔ %u به‌طور کامل در تاریخچه نگه داشته می‌شود."
                : L"Answer %u is retained in full in the session history.",
            (unsigned int)(turn + 1u));
        dio_apply_event(ui, &event);
    }

    for (chunk = 0u; chunk < 2u; ++chunk) {
        const size_t phrase_length =
            wcslen(chunk_phrase);
        size_t used = 0u;
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_UI_EVENT_ASSISTANT_DELTA;
        if (chunk == 0u) {
            const size_t marker_length =
                wcslen(begin_marker);
            (void)memcpy(
                event.text,
                begin_marker,
                marker_length * sizeof(event.text[0]));
            used = marker_length;
        }
        while (used + phrase_length + 1u <
               chunk_target) {
            (void)memcpy(
                event.text + used,
                chunk_phrase,
                phrase_length * sizeof(event.text[0]));
            used += phrase_length;
        }
        if (chunk == 1u) {
            const size_t marker_length =
                wcslen(end_marker);
            (void)memcpy(
                event.text + used,
                end_marker,
                marker_length * sizeof(event.text[0]));
            used += marker_length;
        }
        event.text[used] = L'\0';
        dio_apply_event(ui, &event);
    }
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_SPEAKING;
    event.text[0] = L'\0';
    dio_apply_event(ui, &event);

    verified =
        verified &&
        ui->model.message_count == 16u &&
        ui->model.message_count >
            DIO_VIEW_INITIAL_MESSAGES &&
        wcsstr(
            ui->model.messages[0].text,
            ui->settings.persian
                ? L"آغاز جلسه"
                : L"BEGIN-SESSION") != NULL &&
        ui->model.messages[15].text_length >
            DIO_UI_EVENT_TEXT_CAP &&
        wcsstr(
            ui->model.messages[15].text,
            begin_marker) != NULL &&
        wcsstr(
            ui->model.messages[15].text,
            end_marker) != NULL &&
        (GetWindowLongPtrW(
             ui->messages_semantic,
             GWL_STYLE) &
         WS_TABSTOP) != 0 &&
        dio_uia_keyboard_focusable(
            ui->messages_semantic) &&
        GetWindowTextLengthW(
            ui->messages_semantic) >
            DIO_UI_EVENT_TEXT_CAP &&
        dio_uia_name_contains(
            ui->messages_semantic,
            ui->settings.persian
                ? L"آغاز جلسه"
                : L"BEGIN-SESSION",
            end_marker,
            DIO_UI_EVENT_TEXT_CAP) &&
        ui->transcript_provider != NULL &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            0.0,
            UIA_ScrollPatternNoScroll) ==
            UIA_E_INVALIDOPERATION &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            UIA_ScrollPatternNoScroll,
            101.0) ==
            DIO_E_ARGUMENT_OUT_OF_RANGE &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            101.0,
            UIA_ScrollPatternNoScroll) ==
            DIO_E_ARGUMENT_OUT_OF_RANGE &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            -2.0,
            UIA_ScrollPatternNoScroll) ==
            DIO_E_ARGUMENT_OUT_OF_RANGE &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            INFINITY,
            UIA_ScrollPatternNoScroll) ==
            DIO_E_ARGUMENT_OUT_OF_RANGE &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            UIA_ScrollPatternNoScroll,
            INFINITY) ==
            DIO_E_ARGUMENT_OUT_OF_RANGE &&
        IScrollProvider_SetScrollPercent(
            &ui->transcript_provider->scroll,
            UIA_ScrollPatternNoScroll,
            NAN) == E_INVALIDARG &&
        dio_view_scroll_max(ui->view) > 0.0f &&
        fabsf(
            dio_view_scroll_y(ui->view) -
            dio_view_scroll_max(ui->view)) < 0.5f &&
        dio_capture_overlay_variant(
            ui,
            L"long-session-tail");

    (void)SetFocus(ui->messages_semantic);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_IDLE;
    event.text[0] = L'\0';
    dio_apply_event(ui, &event);
    ui->smoke = false;
    (void)SendMessageW(
        ui->window,
        WM_TIMER,
        DIO_TIMER_HIDE,
        0u);
    ui->smoke = original_smoke;
    verified =
        verified &&
        IsWindowVisible(ui->window);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_SPEAKING;
    dio_apply_event(ui, &event);
    (void)SetFocus(NULL);

    verified =
        dio_uia_set_vertical_scroll_percent(
            ui->messages_semantic,
            0.0) &&
        fabsf(dio_view_scroll_y(ui->view)) < 0.5f &&
        dio_capture_overlay_variant(
            ui,
            L"long-session-top") &&
        verified;
    verified =
        dio_uia_set_vertical_scroll_percent(
            ui->messages_semantic,
            50.0) &&
        dio_capture_overlay_variant(
            ui,
            L"long-session-middle") &&
        verified;

    (void)SetFocus(NULL);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_IDLE;
    event.text[0] = L'\0';
    dio_apply_event(ui, &event);
    ui->smoke = false;
    (void)SendMessageW(
        ui->window,
        WM_TIMER,
        DIO_TIMER_HIDE,
        0u);
    ui->smoke = original_smoke;
    verified =
        verified &&
        IsWindowVisible(ui->window);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_SPEAKING;
    dio_apply_event(ui, &event);

    before_y = dio_view_scroll_y(ui->view);
    before_max = dio_view_scroll_max(ui->view);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_DELTA,
        ui->settings.persian
            ? L" موقعیت خواندن حفظ می‌شود و این بخش تازه در خط بعدی ادامه پیدا می‌کند."
            : L" PRESERVE-AWAY This appended response fragment wraps onto another line while the reader stays in place.");
    dio_apply_event(ui, &event);
    verified =
        verified &&
        fabsf(dio_view_scroll_y(ui->view) - before_y) <
            0.5f &&
        dio_view_scroll_max(ui->view) > before_max;

    ui->transcript_wheel_remainder = 0;
    if (!SystemParametersInfoW(
            SPI_GETWHEELSCROLLLINES,
            0u,
            &wheel_lines,
            0u)) {
        wheel_lines = 3u;
    }
    if (GetClientRect(
            ui->messages_semantic,
            &transcript)) {
        pointer.x = transcript.right / 2;
        pointer.y = transcript.bottom / 2;
        if (ClientToScreen(
                ui->messages_semantic,
                &pointer)) {
            before_y = dio_view_scroll_y(ui->view);
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, 60),
                MAKELPARAM(pointer.x, pointer.y));
            half_y = dio_view_scroll_y(ui->view);
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, 60),
                MAKELPARAM(pointer.x, pointer.y));
            verified =
                verified &&
                fabsf(half_y - before_y) < 0.5f &&
                (wheel_lines == 0u
                    ? fabsf(
                        dio_view_scroll_y(ui->view) -
                        before_y) < 0.5f
                    : dio_view_scroll_y(ui->view) <
                        before_y);
            (void)dio_scroll_transcript_to(
                ui,
                dio_view_scroll_max(ui->view) *
                    0.5f);
            ui->transcript_wheel_remainder = 0;
            before_y = dio_view_scroll_y(ui->view);
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, 60),
                MAKELPARAM(pointer.x, pointer.y));
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, (WORD)(SHORT)-60),
                MAKELPARAM(pointer.x, pointer.y));
            verified =
                verified &&
                ui->transcript_wheel_remainder == 0 &&
                fabsf(
                    dio_view_scroll_y(ui->view) -
                    before_y) < 0.5f;
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, (WORD)(SHORT)-60),
                MAKELPARAM(pointer.x, pointer.y));
            half_y = dio_view_scroll_y(ui->view);
            (void)SendMessageW(
                ui->messages_semantic,
                WM_MOUSEWHEEL,
                MAKEWPARAM(0u, (WORD)(SHORT)-60),
                MAKELPARAM(pointer.x, pointer.y));
            verified =
                verified &&
                fabsf(half_y - before_y) < 0.5f &&
                (wheel_lines == 0u
                    ? fabsf(
                        dio_view_scroll_y(ui->view) -
                        before_y) < 0.5f
                    : dio_view_scroll_y(ui->view) >
                        before_y);
        } else {
            verified = false;
        }
    } else {
        verified = false;
    }

    verified =
        dio_uia_set_vertical_scroll_percent(
            ui->messages_semantic,
            50.0) &&
        verified;
    {
        const CuiRect track =
            dio_view_scrollbar_track(ui->view);
        const CuiRect thumb =
            dio_view_scrollbar_thumb(ui->view);
        POINT down = {
            dio_px(ui, thumb.x + thumb.width * 0.5f),
            dio_px(ui, thumb.y + thumb.height * 0.5f)};
        POINT move = {
            dio_px(ui, track.x + track.width * 0.5f),
            dio_px(ui, track.y + track.height -
                thumb.height * 0.5f)};
        POINT hit;
        (void)MapWindowPoints(
            ui->window,
            ui->messages_semantic,
            &down,
            1u);
        (void)MapWindowPoints(
            ui->window,
            ui->messages_semantic,
            &move,
            1u);
        hit = down;
        verified =
            ClientToScreen(
                ui->messages_semantic,
                &hit) &&
            WindowFromPoint(hit) ==
                ui->messages_semantic &&
            verified;
        (void)SendMessageW(
            ui->messages_semantic,
            WM_LBUTTONDOWN,
            MK_LBUTTON,
            MAKELPARAM(down.x, down.y));
        verified =
            GetCapture() == ui->messages_semantic &&
            ui->transcript_dragging &&
            verified;
        (void)SendMessageW(
            ui->messages_semantic,
            WM_CANCELMODE,
            0u,
            0u);
        verified =
            GetCapture() != ui->messages_semantic &&
            !ui->transcript_dragging &&
            verified;
        (void)SendMessageW(
            ui->messages_semantic,
            WM_LBUTTONDOWN,
            MK_LBUTTON,
            MAKELPARAM(down.x, down.y));
        (void)SendMessageW(
            ui->messages_semantic,
            WM_MOUSEMOVE,
            MK_LBUTTON,
            MAKELPARAM(move.x, move.y));
        (void)SendMessageW(
            ui->messages_semantic,
            WM_LBUTTONUP,
            0u,
            MAKELPARAM(move.x, move.y));
        verified =
            verified &&
            fabsf(
                dio_view_scroll_y(ui->view) -
                dio_view_scroll_max(ui->view)) < 1.0f;
    }

    verified =
        dio_scroll_transcript_key(ui, VK_HOME) &&
        fabsf(dio_view_scroll_y(ui->view)) < 0.5f &&
        dio_scroll_transcript_key(ui, VK_END) &&
        verified;
    before_max = dio_view_scroll_max(ui->view);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_DELTA,
        ui->settings.persian
            ? L" در انتهای پاسخ، متن تازه به‌صورت خودکار دنبال می‌شود تا نوشته برای خواندن پایدار بماند."
            : L" FOLLOW-TAIL This appended response fragment wraps onto another line while the reader follows the answer.");
    dio_apply_event(ui, &event);
    (void)SetFocus(NULL);
    dio_view_set_scroll_hot(ui->view, false);
    verified =
        verified &&
        dio_view_scroll_max(ui->view) > before_max &&
        fabsf(
            dio_view_scroll_y(ui->view) -
            dio_view_scroll_max(ui->view)) < 0.5f &&
        dio_capture_overlay_variant(
            ui,
            L"long-session-follow-tail");

    for (matrix = 0u;
         matrix < _countof(matrix_dpis);
         ++matrix) {
        ui->dpi = matrix_dpis[matrix];
        ui->scale = (float)ui->dpi / 96.0f;
        verified =
            dio_create_tray_icons(ui) &&
            verified;
        dio_update_tray_tip(ui);
        dio_set_appearance(
            ui,
            original_high_contrast);
        dio_place_overlay(ui);
        (void)dio_view_scroll_to(
            ui->view,
            dio_view_scroll_max(ui->view));
        verified =
            dio_capture_overlay_variant(
                ui,
                matrix_suffixes[matrix]) &&
            verified;
    }

    ui->dpi = original_dpi;
    ui->scale = original_scale;
    verified =
        dio_create_tray_icons(ui) &&
        verified;
    dio_update_tray_tip(ui);
    dio_set_appearance(ui, true);
    dio_place_overlay(ui);
    (void)dio_view_scroll_to(
        ui->view,
        dio_view_scroll_max(ui->view));
    verified =
        ui->high_contrast &&
        dio_capture_overlay_variant(
            ui,
            L"long-session-high-contrast") &&
        verified;
    dio_set_appearance(
        ui,
        original_high_contrast);
    dio_place_overlay(ui);
    return verified;
}

static bool dio_smoke_shell_policy(DioUi *ui) {
    DioUiEvent event;
    RECT bounds;
    POINT inside;
    POINT outside;
    bool verified;

    dio_hide_overlay(ui, true);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        L"Late passive delta");
    dio_apply_event(ui, &event);
    verified =
        !IsWindowVisible(ui->window) &&
        ui->interaction_suppressed;

    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_THINKING;
    dio_apply_event(ui, &event);
    verified =
        verified &&
        !IsWindowVisible(ui->window);

    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);
    verified =
        verified &&
        IsWindowVisible(ui->window) &&
        !ui->interaction_suppressed;

    dio_menu_command(ui, DIO_TRAY_SHOW);
    verified =
        verified &&
        !IsWindowVisible(ui->window) &&
        ui->interaction_suppressed;
    dio_menu_command(ui, DIO_TRAY_SHOW);
    verified =
        verified &&
        IsWindowVisible(ui->window) &&
        !ui->interaction_suppressed;
    if (GetWindowRect(ui->window, &bounds)) {
        inside.x = bounds.left +
            (bounds.right - bounds.left) / 2;
        inside.y = bounds.top +
            (bounds.bottom - bounds.top) / 2;
        outside.x = bounds.left - 32;
        outside.y = bounds.top;
        verified =
            verified &&
            !dio_point_is_outside_overlay(ui, inside) &&
            dio_point_is_outside_overlay(ui, outside);
        dio_handle_pointer_press(ui, inside);
        verified =
            verified &&
            IsWindowVisible(ui->window);
        dio_handle_pointer_press(ui, outside);
        verified =
            verified &&
            !IsWindowVisible(ui->window) &&
            ui->interaction_suppressed;
        ZeroMemory(&event, sizeof(event));
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_THINKING;
        dio_apply_event(ui, &event);
        verified =
            verified &&
            !IsWindowVisible(ui->window);
        event.state = DIO_UI_LISTENING;
        dio_apply_event(ui, &event);
        verified =
            verified &&
            IsWindowVisible(ui->window) &&
            !ui->interaction_suppressed;
    } else {
        verified = false;
    }
    ui->smoke = false;
    verified =
        verified &&
        dio_sync_outside_input(ui) &&
        ui->outside_input_registered;
    ui->smoke = true;
    verified =
        verified &&
        dio_sync_outside_input(ui) &&
        !ui->outside_input_registered;
    ui->last_mouse_tray_toggle = 0u;
    verified =
        verified &&
        dio_accept_mouse_tray_toggle(ui, 1000u) &&
        !dio_accept_mouse_tray_toggle(
            ui,
            1000u +
                (ULONGLONG)GetDoubleClickTime() / 2u) &&
        dio_accept_mouse_tray_toggle(
            ui,
            1001u +
                (ULONGLONG)GetDoubleClickTime());
    ui->last_mouse_tray_toggle = 0u;

    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ERROR,
        L"First error");
    dio_apply_event(ui, &event);
    dio_hide_overlay(ui, true);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ASSISTANT_TEXT,
        L"Late passive delta");
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ERROR,
        L"First error");
    dio_apply_event(ui, &event);
    verified =
        verified &&
        !IsWindowVisible(ui->window);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_ERROR,
        L"Second error");
    dio_apply_event(ui, &event);
    verified =
        verified &&
        IsWindowVisible(ui->window) &&
        !ui->interaction_suppressed;
    ZeroMemory(&event, sizeof(event));
    event.kind = DIO_UI_EVENT_CLEAR;
    dio_apply_event(ui, &event);
    event.kind = DIO_UI_EVENT_STATE;
    event.state = DIO_UI_LISTENING;
    dio_apply_event(ui, &event);
    dio_smoke_text(
        &event,
        DIO_UI_EVENT_USER_TEXT,
        ui->settings.persian
            ? L"\u062c\u0644\u0633\u0647\u0654 \u0627\u0645\u0631\u0648\u0632 \u0631\u0627 \u062e\u0644\u0627\u0635\u0647 \u06a9\u0646"
            : L"Summarize today's meeting");
    dio_apply_event(ui, &event);
    ui->tray_menu_smoke_ok = false;
    (void)KillTimer(
        ui->window,
        DIO_TIMER_SMOKE);
    if (SetTimer(
            ui->window,
            DIO_TIMER_TRAY_MENU_SMOKE,
            80u,
            NULL) == 0u) {
        verified = false;
    } else {
        dio_show_tray_menu(ui);
    }
    return verified;
}

static void dio_smoke_step(DioUi *ui) {
    DioUiEvent event;
    const bool fa = ui->settings.persian;

    ZeroMemory(&event, sizeof(event));
    switch (ui->smoke_step++) {
    case 0u:
        event.kind = DIO_UI_EVENT_CLEAR;
        dio_apply_event(ui, &event);
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_LOADING;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            dio_capture_overlay_variant(
                ui,
                L"loading") &&
            ui->ui_smoke_ok;
        break;
    case 1u:
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_LISTENING;
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_USER_TEXT,
            fa
                ? L"\u0686\u0631\u0627\u063a \u0627\u0648\u0644"
                : L"Turn on the first");
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_USER_TEXT,
            fa
                ? L"\u0686\u0631\u0627\u063a \u0627\u0648\u0644 \u0631\u0627 \u0631\u0648\u0634\u0646 \u06a9\u0646"
                : L"Turn on the first light");
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            ui->model.message_count == 1u &&
            ui->model.messages[0].kind ==
                DIO_MESSAGE_USER &&
            (ui->model.messages[0].flags &
             DIO_MESSAGE_MIC_INPUT) != 0u &&
            dio_capture_overlay_variant(
                ui,
                L"listening") &&
            ui->ui_smoke_ok;
        break;
    case 2u: {
        const float before_metadata =
            dio_overlay_height(ui);
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_THINKING;
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_CHIP,
            fa ? L"\u0634\u0646\u06cc\u062f\u0645 \u00b7 \u06cc\u06a9 \u0644\u062d\u0638\u0647" : L"Heard \u00b7 Okay, one moment");
        event.chip = DIO_CHIP_ACK;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            ui->model.message_count == 1u &&
            (ui->model.messages[0].flags &
             DIO_MESSAGE_ACCEPTED) != 0u &&
            fabsf(
                dio_overlay_height(ui) -
                before_metadata) < 0.01f &&
            dio_capture_overlay_variant(
                ui,
                L"thinking") &&
            ui->ui_smoke_ok;
        break;
    }
    case 3u: {
        float before_metadata;
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_SPEAKING;
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_ASSISTANT_TEXT,
            fa
                ? L"\u062d\u062a\u0645\u0627\u064b\u060c \u0686\u0631\u0627\u063a \u0627\u0648\u0644"
                : L"Sure, the first light");
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_ASSISTANT_TEXT,
            fa
                ? L"\u062d\u062a\u0645\u0627\u064b\u060c \u0686\u0631\u0627\u063a \u0627\u0648\u0644 \u0631\u0648\u0634\u0646 \u0634\u062f."
                : L"Sure, the first light is on.");
        dio_apply_event(ui, &event);
        before_metadata = dio_overlay_height(ui);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_CHIP,
            fa ? L"\u0635\u062f\u0627" : L"Voice");
        event.chip = DIO_CHIP_TTS;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            ui->model.message_count == 2u &&
            ui->model.messages[0].kind ==
                DIO_MESSAGE_USER &&
            ui->model.messages[1].kind ==
                DIO_MESSAGE_ASSISTANT &&
            (ui->model.messages[1].flags &
             DIO_MESSAGE_AUDIO_OUTPUT) != 0u &&
            fabsf(
                dio_overlay_height(ui) -
                before_metadata) < 0.01f &&
            dio_capture_overlay_variant(
                ui,
                L"speaking") &&
            ui->ui_smoke_ok;
        break;
    }
    case 4u:
        {
            RECT before;
            RECT after;
            const bool before_valid =
                GetWindowRect(ui->window, &before) != FALSE;
            dio_smoke_long_text(&event, fa);
            dio_apply_event(ui, &event);
            ui->ui_smoke_ok =
                before_valid &&
                GetWindowRect(ui->window, &after) &&
                EqualRect(&before, &after) &&
                ui->ui_smoke_ok;
        }
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_FOLLOW_UP;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            GetWindowTextLengthW(
                ui->messages_semantic) > 0 &&
            !ui->semantic_message_pending &&
            dio_capture_overlay_variant(
                ui,
                L"long") &&
            dio_capture_overlay_variant(
                ui,
                L"follow-up") &&
            ui->ui_smoke_ok;
        break;
    case 5u:
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_ANNOUNCEMENT,
            fa
                ? L"\u06cc\u0627\u062f\u0622\u0648\u0631\u06cc: \u0633\u0627\u0639\u062a \u0646\u0647 \u062a\u0645\u0627\u0633 \u0628\u06af\u06cc\u0631."
                : L"Reminder: make the call at nine.");
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            dio_capture_overlay_variant(
                ui,
                L"announcement") &&
            ui->ui_smoke_ok;
        break;
    case 6u:
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_MUTED;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            dio_capture_overlay_variant(
                ui,
                L"muted") &&
            ui->ui_smoke_ok;
        break;
    case 7u:
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_ERROR,
            fa
                ? L"\u0633\u0631\u0648\u06cc\u0633 \u062f\u0631 \u062f\u0633\u062a\u0631\u0633 \u0646\u06cc\u0633\u062a."
                : L"The service is unavailable.");
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            dio_capture_overlay_variant(
                ui,
                L"error") &&
            ui->ui_smoke_ok;
        break;
    case 8u:
        event.kind = DIO_UI_EVENT_CLEAR;
        dio_apply_event(ui, &event);
        dio_smoke_text(
            &event,
            DIO_UI_EVENT_PROVIDER_REQUIRED,
            fa
                ? L"\u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647\u0654 \u0645\u062f\u0644 \u062a\u0646\u0638\u06cc\u0645 \u0646\u0634\u062f\u0647 \u0627\u0633\u062a."
                : L"Model provider is not configured.");
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            ui->provider_required &&
            IsWindowVisible(ui->provider_button) &&
            IsWindowEnabled(ui->provider_button) &&
            dio_accessible_name_present(ui->provider_button) &&
            dio_uia_control_type_equals(
                ui->provider_button,
                UIA_ButtonControlTypeId) &&
            dio_capture_overlay_variant(
                ui,
                L"provider-required") &&
            ui->ui_smoke_ok;
        break;
    case 9u:
        event.kind = DIO_UI_EVENT_STATE;
        event.state = DIO_UI_IDLE;
        dio_apply_event(ui, &event);
        ui->ui_smoke_ok =
            dio_capture_overlay_variant(
                ui,
                L"idle") &&
            ui->ui_smoke_ok;
        break;
    default:
        {
            const bool suppression_verified =
                dio_smoke_shell_policy(ui);
            const bool state_captures_verified =
                ui->ui_smoke_ok;
            const bool conversation_verified =
                dio_smoke_conversation_policy(ui);
            const bool accessibility_verified =
                conversation_verified &&
                dio_smoke_accessibility_policy(ui);
            const bool scroll_verified =
                accessibility_verified &&
                dio_smoke_transcript_scroll_policy(ui);
            const bool evidence_verified =
                dio_capture_shell_evidence(
                    ui,
                    suppression_verified,
                    conversation_verified,
                    accessibility_verified,
                    scroll_verified,
                    state_captures_verified);
            ui->ui_smoke_ok =
                ui->ui_smoke_ok &&
                conversation_verified &&
                scroll_verified &&
                evidence_verified;
        }
        (void)KillTimer(ui->window, DIO_TIMER_SMOKE);
        DestroyWindow(ui->window);
        break;
    }
}

static void dio_draw_close_button(
    DioUi *ui,
    HDC device_context,
    const RECT *rect) {
    CuiButtonPaint paint;
    CuiResult result;

    ZeroMemory(&paint, sizeof(paint));
    paint.variant = CUI_BUTTON_GHOST;
    paint.icon = CUI_ICON_CLOSE;
    paint.radius = CUI_RADIUS_MEDIUM;
    paint.parent_background = CUI_ROLE_CARD;
    paint.icon_size = 16.0f;
    if (IsWindowEnabled(ui->close_button)) {
        paint.state |= CUI_BUTTON_ENABLED;
    }
    if (dio_button_is_hot(ui->close_button)) {
        paint.state |= CUI_BUTTON_HOT;
    }
    if ((SendMessageW(
             ui->close_button,
             BM_GETSTATE,
             0u,
             0) & BST_PUSHED) != 0u) {
        paint.state |= CUI_BUTTON_PRESSED;
    }
    if (GetFocus() == ui->close_button) {
        paint.state |= CUI_BUTTON_FOCUSED;
    }
    result = cui_win32_draw_owner_button(
        ui->graphics,
        device_context,
        rect,
        &paint);
    ui->close_paint_count += 1u;
    ui->close_paint_result = result;
    if (result == CUI_TARGET_RECREATE) {
        cui_win32_context_discard_target(ui->graphics);
        InvalidateRect(ui->close_button, NULL, FALSE);
    }
}

static void dio_draw_provider_button(
    DioUi *ui,
    const DRAWITEMSTRUCT *draw) {
    CuiButtonPaint paint;

    ZeroMemory(&paint, sizeof(paint));
    paint.variant = CUI_BUTTON_PRIMARY;
    paint.icon = CUI_ICON_NONE;
    paint.radius = CUI_RADIUS_MEDIUM;
    paint.parent_background = CUI_ROLE_CARD;
    paint.label = ui->provider_label;
    if ((draw->itemState & ODS_DISABLED) == 0u) {
        paint.state |= CUI_BUTTON_ENABLED;
    }
    if ((draw->itemState & ODS_HOTLIGHT) != 0u ||
        dio_button_is_hot(draw->hwndItem)) {
        paint.state |= CUI_BUTTON_HOT;
    }
    if ((draw->itemState & ODS_SELECTED) != 0u) {
        paint.state |= CUI_BUTTON_PRESSED;
    }
    if ((draw->itemState & ODS_FOCUS) != 0u) {
        paint.state |= CUI_BUTTON_FOCUSED;
    }
    if (cui_win32_draw_owner_button(
            ui->graphics,
            draw->hDC,
            &draw->rcItem,
            &paint) == CUI_TARGET_RECREATE) {
        cui_win32_context_discard_target(ui->graphics);
        InvalidateRect(draw->hwndItem, NULL, FALSE);
    }
}

static LRESULT CALLBACK dio_close_visual_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference) {
    DioUi *ui = (DioUi *)reference;

    switch (message) {
    case WM_GETOBJECT:
        if ((LONG)lparam == UiaRootObjectId &&
            ui->close_provider != NULL) {
            return UiaReturnRawElementProvider(
                window,
                wparam,
                lparam,
                &ui->close_provider->simple);
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT rect;
        BeginPaint(window, &paint);
        if (GetClientRect(window, &rect)) {
            dio_draw_close_button(ui, paint.hdc, &rect);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: {
        RECT rect;
        if (GetClientRect(window, &rect)) {
            dio_draw_close_button(ui, (HDC)wparam, &rect);
        }
        return 0;
    }
    case BM_SETSTATE:
    case WM_ENABLE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result =
            DefSubclassProc(
                window,
                message,
                wparam,
                lparam);
        InvalidateRect(window, NULL, FALSE);
        return result;
    }
    case WM_NCDESTROY:
        dio_close_provider_disconnect(ui, window);
        (void)RemoveWindowSubclass(
            window,
            dio_close_visual_proc,
            subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(
        window,
        message,
        wparam,
        lparam);
}

static bool dio_rect_contains(
    CuiRect bounds,
    float x,
    float y) {
    return
        bounds.width > 0.0f &&
        bounds.height > 0.0f &&
        x >= bounds.x &&
        y >= bounds.y &&
        x < bounds.x + bounds.width &&
        y < bounds.y + bounds.height;
}

static POINT dio_transcript_parent_point(
    DioUi *ui,
    HWND window,
    LPARAM lparam) {
    POINT point = {
        GET_X_LPARAM(lparam),
        GET_Y_LPARAM(lparam)};
    (void)MapWindowPoints(
        window,
        ui->window,
        &point,
        1u);
    return point;
}

static void dio_transcript_hot(
    DioUi *ui,
    bool hot) {
    dio_view_set_scroll_hot(ui->view, hot);
    InvalidateRect(ui->window, NULL, FALSE);
}

static LRESULT CALLBACK dio_transcript_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR reference) {
    DioUi *ui = (DioUi *)reference;

    switch (message) {
    case WM_GETOBJECT:
        if ((LONG)lparam == UiaRootObjectId &&
            ui->transcript_provider != NULL) {
            return UiaReturnRawElementProvider(
                window,
                wparam,
                lparam,
                &ui->transcript_provider->simple);
        }
        break;
    case DIO_WM_TRANSCRIPT_QUERY: {
        DioTranscriptScrollInfo *info =
            (DioTranscriptScrollInfo *)lparam;
        if (info == NULL) {
            return FALSE;
        }
        dio_transcript_scroll_info(ui, info);
        return TRUE;
    }
    case DIO_WM_TRANSCRIPT_SCROLL:
        switch ((UINT)wparam) {
        case SB_LINEUP:
            (void)dio_scroll_transcript_key(ui, VK_UP);
            break;
        case SB_LINEDOWN:
            (void)dio_scroll_transcript_key(ui, VK_DOWN);
            break;
        case SB_PAGEUP:
            (void)dio_scroll_transcript_key(ui, VK_PRIOR);
            break;
        case SB_PAGEDOWN:
            (void)dio_scroll_transcript_key(ui, VK_NEXT);
            break;
        case SB_TOP:
            (void)dio_scroll_transcript_key(ui, VK_HOME);
            break;
        case SB_BOTTOM:
            (void)dio_scroll_transcript_key(ui, VK_END);
            break;
        default:
            return FALSE;
        }
        return TRUE;
    case DIO_WM_TRANSCRIPT_PERCENT: {
        const double *percent =
            (const double *)lparam;
        if (percent == NULL ||
            isfinite(*percent) == 0 ||
            *percent < 0.0 ||
            *percent > 100.0) {
            return FALSE;
        }
        (void)dio_scroll_transcript_to(
            ui,
            (float)(
                *percent / 100.0 *
                (double)dio_view_scroll_max(ui->view)));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        const POINT point = dio_transcript_parent_point(
            ui,
            window,
            lparam);
        const float x = (float)point.x / ui->scale;
        const float y = (float)point.y / ui->scale;
        const CuiRect track =
            dio_view_scrollbar_track(ui->view);
        const CuiRect thumb =
            dio_view_scrollbar_thumb(ui->view);
        if (ui->model.message_count == 0u) {
            return 0;
        }
        (void)SetForegroundWindow(ui->window);
        (void)SetFocus(window);
        if (dio_rect_contains(track, x, y)) {
            dio_transcript_hot(ui, true);
            if (dio_rect_contains(thumb, x, y)) {
                ui->transcript_dragging = true;
                ui->transcript_drag_offset = y - thumb.y;
                (void)SetCapture(window);
            } else {
                (void)dio_scroll_transcript_to(
                    ui,
                    dio_view_scroll_y(ui->view) +
                        (y < thumb.y
                            ? -dio_view_scroll_page(ui->view)
                            : dio_view_scroll_page(ui->view)));
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const POINT point = dio_transcript_parent_point(
            ui,
            window,
            lparam);
        const float x = (float)point.x / ui->scale;
        const float y = (float)point.y / ui->scale;
        const CuiRect track =
            dio_view_scrollbar_track(ui->view);
        const CuiRect thumb =
            dio_view_scrollbar_thumb(ui->view);
        TRACKMOUSEEVENT tracking;
        ZeroMemory(&tracking, sizeof(tracking));
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        (void)TrackMouseEvent(&tracking);
        dio_transcript_hot(
            ui,
            ui->transcript_dragging ||
                dio_rect_contains(track, x, y));
        if (ui->transcript_dragging &&
            GetCapture() == window) {
            const float travel =
                track.height - thumb.height;
            const float top = fminf(
                fmaxf(
                    y - ui->transcript_drag_offset,
                    track.y),
                track.y + travel);
            if (travel > 0.0f) {
                (void)dio_scroll_transcript_to(
                    ui,
                    dio_view_scroll_max(ui->view) *
                        (top - track.y) /
                        travel);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (ui->transcript_dragging) {
            ui->transcript_dragging = false;
            if (GetCapture() == window) {
                (void)ReleaseCapture();
            }
        }
        return 0;
    case WM_CANCELMODE:
        ui->transcript_dragging = false;
        if (GetCapture() == window) {
            (void)ReleaseCapture();
        }
        dio_transcript_hot(ui, false);
        return 0;
    case WM_CAPTURECHANGED:
        ui->transcript_dragging = false;
        dio_transcript_hot(ui, false);
        return 0;
    case WM_MOUSELEAVE:
        if (!ui->transcript_dragging) {
            dio_transcript_hot(ui, false);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (dio_scroll_transcript_wheel(
                ui,
                wparam,
                lparam)) {
            return 0;
        }
        break;
    case WM_SETFOCUS:
        ui->model.transcript_focused = true;
        (void)KillTimer(ui->window, DIO_TIMER_HIDE);
        InvalidateRect(ui->window, NULL, FALSE);
        break;
    case WM_KILLFOCUS:
        ui->model.transcript_focused = false;
        InvalidateRect(ui->window, NULL, FALSE);
        if (ui->model.state == DIO_UI_IDLE &&
            IsWindowVisible(ui->window) &&
            !ui->tray_menu_active &&
            !dio_transcript_reading(ui)) {
            (void)SetTimer(
                ui->window,
                DIO_TIMER_HIDE,
                4000u,
                NULL);
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_NCDESTROY:
        dio_transcript_provider_disconnect(
            ui,
            window);
        (void)RemoveWindowSubclass(
            window,
            dio_transcript_proc,
            subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(
        window,
        message,
        wparam,
        lparam);
}

static LRESULT CALLBACK dio_overlay_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    DioUi *ui = (DioUi *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        ui = (DioUi *)create->lpCreateParams;
        ui->window = window;
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (ui == NULL) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (ui->taskbar_created != 0u &&
        message == ui->taskbar_created) {
        ui->tray_added = false;
        (void)dio_tray_add(ui);
        dio_sync_tray_loading_timer(ui);
        return 0;
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        CuiResult result;
        BeginPaint(window, &paint);
        result = ui->view != NULL
            ? dio_view_draw(ui->view, &ui->model)
            : CUI_OK;
        EndPaint(window, &paint);
        if (result == CUI_TARGET_RECREATE) {
            cui_win32_context_discard_target(ui->graphics);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
    case WM_MEASUREITEM:
        if (dio_measure_menu_item(
                (MEASUREITEMSTRUCT *)lparam)) {
            return TRUE;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *draw = (const DRAWITEMSTRUCT *)lparam;
        if (dio_draw_menu_item(draw)) {
            return TRUE;
        }
        if (draw != NULL &&
            draw->CtlID == DIO_PROVIDER_BUTTON_ID) {
            dio_draw_provider_button(ui, draw);
            return TRUE;
        }
        if (draw != NULL &&
            (draw->CtlID == DIO_STATUS_ID ||
             draw->CtlID == DIO_MESSAGES_ID)) {
            return TRUE;
        }
        break;
    }
    case WM_MENUCHAR: {
        LRESULT result;
        if (dio_menu_char(wparam, lparam, &result)) {
            return result;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == DIO_CLOSE_ID &&
            HIWORD(wparam) == BN_CLICKED) {
            dio_hide_overlay(ui, true);
            return 0;
        }
        if (LOWORD(wparam) == DIO_CLOSE_ID &&
            (HIWORD(wparam) == BN_SETFOCUS ||
             HIWORD(wparam) == BN_KILLFOCUS)) {
            InvalidateRect(ui->close_button, NULL, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == DIO_TRAY_SETTINGS) {
            dio_show_settings(ui, false);
            return 0;
        }
        if (LOWORD(wparam) == DIO_PROVIDER_BUTTON_ID &&
            HIWORD(wparam) == BN_CLICKED) {
            dio_show_settings(ui, false);
            return 0;
        }
        break;
    case WM_SIZE:
        dio_prepare_view(ui);
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_DPICHANGED:
        ui->dpi = HIWORD(wparam);
        ui->scale = (float)ui->dpi / 96.0f;
        if (dio_create_tray_icons(ui)) {
            dio_update_tray_tip(ui);
        }
        dio_apply_appearance(ui);
        if (lparam != 0) {
            const RECT *suggested =
                (const RECT *)lparam;
            dio_place_overlay_on_monitor(
                ui,
                MonitorFromRect(
                    suggested,
                    MONITOR_DEFAULTTONEAREST));
        } else {
            dio_place_overlay(ui);
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        dio_apply_appearance(ui);
        dio_sync_tray_loading_timer(ui);
        dio_place_overlay(ui);
        return 0;
    case WM_TIMER:
        switch ((UINT_PTR)wparam) {
        case DIO_TIMER_TRAY_MENU_SMOKE: {
            wchar_t path[MAX_PATH];
            (void)KillTimer(
                window,
                DIO_TIMER_TRAY_MENU_SMOKE);
            ui->tray_menu_smoke_ok =
                ui->tray_menu_smoke_ok &&
                swprintf_s(
                    path,
                    _countof(path),
                    L"out\\ui-smoke\\tray-menu-%ls.bmp",
                    ui->settings.persian
                        ? L"fa"
                        : L"en") > 0 &&
                dio_capture_popup_menu(path);
            (void)EndMenu();
            return 0;
        }
        case DIO_TIMER_TRAY_LOADING:
            if (ui->tray_loading_timer &&
                ui->model.state == DIO_UI_LOADING &&
                !ui->smoke) {
                ui->tray_loading_frame =
                    (ui->tray_loading_frame + 1u) %
                    DIO_TRAY_LOADING_FRAME_COUNT;
                dio_update_tray_icon(ui);
            } else {
                dio_sync_tray_loading_timer(ui);
            }
            return 0;
        case DIO_TIMER_ANIMATION:
            ui->model.phase =
                (float)fmod(
                    (double)GetTickCount64() *
                        0.01375,
                    6.283185307179586);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        case DIO_TIMER_HIDE:
            (void)KillTimer(window, DIO_TIMER_HIDE);
            if (ui->model.state == DIO_UI_IDLE &&
                !ui->smoke &&
                !dio_transcript_reading(ui)) {
                dio_hide_overlay(ui, false);
            }
            return 0;
        case DIO_TIMER_SMOKE:
            dio_smoke_step(ui);
            return 0;
        default:
            break;
        }
        break;
    case WM_MOUSEWHEEL:
        if (dio_scroll_transcript_wheel(
                ui,
                wparam,
                lparam)) {
            return 0;
        }
        break;
    case WM_INPUT:
        dio_handle_raw_input(
            ui,
            (HRAWINPUT)lparam);
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);
    case DIO_WM_EVENTS:
        dio_drain_events(ui);
        return 0;
    case DIO_WM_VAULT_REQUIRED:
        dio_show_settings(ui, true);
        return 0;
    case DIO_WM_MODELS_READY: {
        wchar_t *models;
        unsigned long long generation;
        EnterCriticalSection(&ui->event_lock);
        models = ui->pending_models;
        generation = ui->pending_models_generation;
        ui->pending_models = NULL;
        ui->pending_models_generation = 0u;
        LeaveCriticalSection(&ui->event_lock);
        if (models != NULL) {
            if (IsWindow(ui->settings_window)) {
                (void)SendMessageW(
                    ui->settings_window,
                    DIO_WM_MODELS,
                    (WPARAM)generation,
                    (LPARAM)models);
            }
            free(models);
        }
        return 0;
    }
    case DIO_WM_EXIT:
        DestroyWindow(window);
        return 0;
    case DIO_WM_TRAY:
        switch (LOWORD(lparam)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            dio_show_tray_menu(ui);
            break;
        case WM_LBUTTONUP:
        case NIN_SELECT:
            if (dio_accept_mouse_tray_toggle(
                    ui,
                    GetTickCount64())) {
                dio_menu_command(ui, DIO_TRAY_SHOW);
            }
            break;
        case NIN_KEYSELECT:
            dio_menu_command(ui, DIO_TRAY_SHOW);
            break;
        default:
            break;
        }
        return 0;
    case WM_CLOSE:
        dio_hide_overlay(ui, true);
        return 0;
    case WM_DESTROY:
        (void)KillTimer(window, DIO_TIMER_ANIMATION);
        (void)KillTimer(window, DIO_TIMER_HIDE);
        (void)KillTimer(window, DIO_TIMER_SMOKE);
        (void)KillTimer(
            window,
            DIO_TIMER_TRAY_MENU_SMOKE);
        ui->animation_timer = false;
        (void)dio_set_outside_input(ui, false);
        dio_tray_remove(ui);
        ui->window = NULL;
        PostQuitMessage(
            ui->smoke &&
                !ui->settings_smoke &&
                !ui->ui_smoke_ok
                ? 2
                : 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool dio_ui_create(
    const DioUiOptions *options,
    DioUi **output) {
    DioUi *ui;
    CuiWin32ContextDesc graphics;
    INITCOMMONCONTROLSEX controls;

    if (options == NULL ||
        options->paths == NULL ||
        options->settings == NULL ||
        output == NULL) {
        return false;
    }
    *output = NULL;
    ui = (DioUi *)calloc(1u, sizeof(*ui));
    if (ui == NULL) {
        return false;
    }
    ui->instance = options->instance != NULL
        ? options->instance
        : GetModuleHandleW(NULL);
    ui->thread_id = GetCurrentThreadId();
    ui->paths = *options->paths;
    ui->settings = *options->settings;
    dio_vault_init(&ui->vault, ui->paths.secrets);
    dio_agent_profile_init(&ui->profile);
    if (ui->profile.system_prompt == NULL ||
        (options->profile != NULL &&
         !dio_agent_profile_copy(&ui->profile, options->profile))) {
        dio_agent_profile_free(&ui->profile);
        free(ui);
        return false;
    }
    ui->command = options->command;
    ui->command_context = options->command_context;
    ui->paused = false;
    ui->dpi = 96u;
    ui->scale = 1.0f;
    ui->model.rtl = ui->settings.persian;
    ui->model.state = DIO_UI_LOADING;
    dio_set_status(ui);
    if (!InitializeCriticalSectionEx(
            &ui->event_lock,
            4000u,
            0u)) {
        free(ui);
        return false;
    }
    ui->event_lock_initialized = true;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES;
    (void)InitCommonControlsEx(&controls);
    (void)SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        dio_ui_destroy(ui);
        return false;
    }
    if (!dio_register_overlay_class(ui->instance)) {
        dio_ui_destroy(ui);
        return false;
    }
    if (GetFileAttributesW(ui->paths.font) != INVALID_FILE_ATTRIBUTES) {
        ui->font_loaded =
            AddFontResourceExW(ui->paths.font, FR_PRIVATE, NULL) > 0;
    }
    ui->window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        DIO_OVERLAY_CLASS,
        L"DIO Voice",
        WS_POPUP | WS_CLIPCHILDREN,
        0,
        0,
        DIO_OVERLAY_WIDTH_DIP,
        DIO_OVERLAY_MIN_HEIGHT_DIP,
        NULL,
        NULL,
        ui->instance,
        ui);
    if (ui->window == NULL) {
        dio_ui_destroy(ui);
        return false;
    }
    ui->dpi = GetDpiForWindow(ui->window);
    if (ui->dpi == 0u) {
        ui->dpi = 96u;
    }
    ui->scale = (float)ui->dpi / 96.0f;
    ui->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    dio_view_theme_init(&ui->theme);
    if (!dio_create_tray_icons(ui) ||
        !cui_theme_validate(&ui->theme)) {
        dio_ui_destroy(ui);
        return false;
    }
    ZeroMemory(&graphics, sizeof(graphics));
    graphics.window = ui->window;
    graphics.theme = &ui->theme;
    graphics.icon_pack = cui_icons_default();
    graphics.persian_font_path =
        GetFileAttributesW(ui->paths.font) != INVALID_FILE_ATTRIBUTES
            ? ui->paths.font
            : NULL;
    graphics.scale = ui->scale;
    graphics.high_contrast = cui_win32_high_contrast();
    if (cui_win32_context_create(&graphics, &ui->graphics) != CUI_OK ||
        !dio_view_create(ui->graphics, &ui->view) ||
        !dio_update_provider_label(ui)) {
        dio_ui_destroy(ui);
        return false;
    }
    ui->close_button = CreateWindowExW(
        0u,
        L"BUTTON",
        L"Hide conversation",
        WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            BS_PUSHBUTTON |
            BS_NOTIFY,
        0,
        0,
        1,
        1,
        ui->window,
        (HMENU)(INT_PTR)DIO_CLOSE_ID,
        ui->instance,
        NULL);
    ui->provider_button = CreateWindowExW(
        0u,
        L"BUTTON",
        ui->settings.persian
            ? L"\u062a\u0646\u0638\u06cc\u0645 \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647"
            : L"Configure provider",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
        0,
        0,
        1,
        1,
        ui->window,
        (HMENU)(INT_PTR)DIO_PROVIDER_BUTTON_ID,
        ui->instance,
        NULL);
    ui->status_semantic = CreateWindowExW(
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0,
        0,
        1,
        1,
        ui->window,
        (HMENU)(INT_PTR)DIO_STATUS_ID,
        ui->instance,
        NULL);
    ui->messages_semantic = CreateWindowExW(
        WS_EX_TRANSPARENT,
        L"STATIC",
        L"",
        WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            SS_OWNERDRAW |
            SS_NOTIFY,
        0,
        0,
        1,
        1,
        ui->window,
        (HMENU)(INT_PTR)DIO_MESSAGES_ID,
        ui->instance,
        NULL);
    if (ui->close_button == NULL ||
        ui->provider_button == NULL ||
        ui->status_semantic == NULL ||
        ui->messages_semantic == NULL ||
        !SetWindowSubclass(
            ui->close_button,
            dio_button_hover_proc,
            DIO_BUTTON_HOVER_SUBCLASS,
            0u) ||
        !SetWindowSubclass(
            ui->provider_button,
            dio_button_hover_proc,
            DIO_BUTTON_HOVER_SUBCLASS,
            0u) ||
        !SetWindowSubclass(
            ui->close_button,
            dio_close_visual_proc,
            DIO_CLOSE_VISUAL_SUBCLASS,
            (DWORD_PTR)ui) ||
        !SetWindowSubclass(
            ui->messages_semantic,
            dio_transcript_proc,
            DIO_TRANSCRIPT_SUBCLASS,
            (DWORD_PTR)ui)) {
        dio_ui_destroy(ui);
        return false;
    }
    (void)SendMessageW(
        ui->provider_button,
        WM_SETFONT,
        (WPARAM)GetStockObject(DEFAULT_GUI_FONT),
        TRUE);
    ui->close_provider =
        dio_close_provider_create(
            ui->close_button);
    ui->transcript_provider =
        dio_transcript_provider_create(
            ui->messages_semantic);
    if (ui->close_provider == NULL ||
        ui->transcript_provider == NULL ||
        !dio_tray_add(ui)) {
        dio_ui_destroy(ui);
        return false;
    }
    dio_apply_appearance(ui);
    dio_refresh(ui, true, true);
    *output = ui;
    return true;
}

void dio_ui_request_exit(DioUi *ui) {
    if (ui == NULL ||
        InterlockedExchange(&ui->closing, 1) != 0) {
        return;
    }
    if (ui->window != NULL) {
        (void)PostMessageW(ui->window, DIO_WM_EXIT, 0u, 0);
    }
}

int dio_ui_run(
    DioUi *ui,
    bool ui_smoke,
    bool settings_smoke) {
    MSG message;
    int result;

    if (ui == NULL ||
        ui->window == NULL ||
        ui->thread_id != GetCurrentThreadId()) {
        return 1;
    }
    ui->smoke = ui_smoke;
    ui->ui_smoke_ok = !ui_smoke;
    ui->settings_smoke = settings_smoke;
    dio_sync_tray_loading_timer(ui);
    if (settings_smoke) {
        ui->smoke = true;
        dio_sync_tray_loading_timer(ui);
        ui->settings_smoke_ok = false;
        dio_show_settings(ui, false);
        result = ui->settings_smoke_ok ? 0 : 2;
        if (ui->window != NULL) {
            DestroyWindow(ui->window);
        }
        return result;
    }
    dio_show_overlay(ui, false);
    if (ui_smoke) {
        ui->ui_smoke_ok = true;
        ui->smoke_step = 0u;
        dio_smoke_step(ui);
        if (ui->window != NULL) {
            (void)SetTimer(ui->window, DIO_TIMER_SMOKE, 800u, NULL);
        }
    }
    for (;;) {
        result = GetMessageW(&message, NULL, 0u, 0u);
        if (result == 0) {
            return (int)message.wParam;
        }
        if (result < 0) {
            return 1;
        }
        if (message.message == WM_KEYDOWN &&
            (message.hwnd == ui->window ||
             IsChild(ui->window, message.hwnd))) {
            if (message.wParam == VK_TAB) {
                HWND targets[3];
                const bool reverse =
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const HWND focus = GetFocus();
                size_t count = 0u;
                size_t index = 0u;
                HWND target;
                targets[count++] = ui->close_button;
                if (ui->model.message_count > 0u) {
                    targets[count++] = ui->messages_semantic;
                }
                if (ui->provider_required) {
                    targets[count++] = ui->provider_button;
                }
                while (index < count && targets[index] != focus) {
                    ++index;
                }
                if (index == count) {
                    index = reverse ? 0u : count - 1u;
                }
                target = targets[
                    reverse
                        ? (index + count - 1u) % count
                        : (index + 1u) % count];
                (void)SetFocus(target);
                continue;
            }
            if (GetFocus() == ui->messages_semantic &&
                dio_scroll_transcript_key(
                    ui,
                    message.wParam)) {
                continue;
            }
        }
        if (message.message == WM_KEYDOWN &&
            message.wParam == VK_ESCAPE &&
            (message.hwnd == ui->window ||
             IsChild(ui->window, message.hwnd))) {
            if (dio_can_cancel(ui->model.state)) {
                dio_emit_command(
                    ui,
                    DIO_UI_COMMAND_CANCEL,
                    true);
            }
            dio_hide_overlay(ui, true);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void dio_ui_destroy(DioUi *ui) {
    if (ui == NULL) {
        return;
    }
    (void)InterlockedExchange(&ui->closing, 1);
    if (ui->close_provider != NULL) {
        dio_close_provider_disconnect(
            ui,
            ui->close_button);
    }
    if (ui->transcript_provider != NULL) {
        dio_transcript_provider_disconnect(
            ui,
            ui->messages_semantic);
    }
    if (ui->window != NULL) {
        DestroyWindow(ui->window);
    }
    dio_tray_remove(ui);
    dio_destroy_tray_icons(ui);
    dio_view_destroy(ui->view);
    cui_win32_text_layout_destroy(ui->provider_label);
    cui_win32_context_destroy(ui->graphics);
    dio_clear_messages(&ui->model);
    free(ui->model.messages);
    if (ui->font_loaded) {
        (void)RemoveFontResourceExW(ui->paths.font, FR_PRIVATE, NULL);
    }
    if (ui->event_lock_initialized) {
        EnterCriticalSection(&ui->event_lock);
        free(ui->pending_models);
        ui->pending_models = NULL;
        LeaveCriticalSection(&ui->event_lock);
        DeleteCriticalSection(&ui->event_lock);
    }
    dio_vault_lock(&ui->vault);
    dio_agent_profile_free(&ui->profile);
    free(ui);
}
