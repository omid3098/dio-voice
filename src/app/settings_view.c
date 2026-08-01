#include "settings_view.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    DIO_SETTINGS_HEADER_HEIGHT = 128,
    DIO_SETTINGS_FOOTER_HEIGHT = 64,
    DIO_SETTINGS_BODY_BOTTOM = 534
};

typedef enum DioSettingsText {
    DIO_SETTINGS_TEXT_TITLE = 0,
    DIO_SETTINGS_TEXT_SUBTITLE,
    DIO_SETTINGS_TEXT_INPUT_SECTION,
    DIO_SETTINGS_TEXT_LANGUAGE,
    DIO_SETTINGS_TEXT_LANGUAGE_DESCRIPTION,
    DIO_SETTINGS_TEXT_MICROPHONE,
    DIO_SETTINGS_TEXT_MICROPHONE_DESCRIPTION,
    DIO_SETTINGS_TEXT_SILENCE,
    DIO_SETTINGS_TEXT_SILENCE_DESCRIPTION,
    DIO_SETTINGS_TEXT_CONVERSATION_SECTION,
    DIO_SETTINGS_TEXT_FOLLOW_UP_ENABLED,
    DIO_SETTINGS_TEXT_FOLLOW_UP_ENABLED_DESCRIPTION,
    DIO_SETTINGS_TEXT_FOLLOW_UP_SECONDS,
    DIO_SETTINGS_TEXT_FOLLOW_UP_SECONDS_DESCRIPTION,
    DIO_SETTINGS_TEXT_ACCESSIBILITY_SECTION,
    DIO_SETTINGS_TEXT_REDUCED_MOTION,
    DIO_SETTINGS_TEXT_REDUCED_MOTION_DESCRIPTION,
    DIO_SETTINGS_TEXT_CANCEL,
    DIO_SETTINGS_TEXT_SAVE,
    DIO_SETTINGS_TEXT_LOCALE_VALUE,
    DIO_SETTINGS_TEXT_MICROPHONE_VALUE,
    DIO_SETTINGS_TEXT_COUNT
} DioSettingsText;

struct DioSettingsView {
    CuiWin32Context *graphics;
    CuiWin32TextLayout *text[DIO_SETTINGS_TEXT_COUNT];
    CuiRect controls[DIO_SETTINGS_CONTROL_COUNT];
    unsigned int control_state[DIO_SETTINGS_CONTROL_COUNT];
    bool persian;
    DioSettingsPage page;
    float width;
    float viewport_height;
    float scroll_y;
    float label_x;
    float label_width;
    float control_width;
};

static bool dio_settings_is_section(
    DioSettingsText slot) {
    return slot == DIO_SETTINGS_TEXT_INPUT_SECTION ||
        slot == DIO_SETTINGS_TEXT_CONVERSATION_SECTION ||
        slot == DIO_SETTINGS_TEXT_ACCESSIBILITY_SECTION;
}

static bool dio_settings_is_description(
    DioSettingsText slot) {
    return slot == DIO_SETTINGS_TEXT_LANGUAGE_DESCRIPTION ||
        slot == DIO_SETTINGS_TEXT_MICROPHONE_DESCRIPTION ||
        slot == DIO_SETTINGS_TEXT_SILENCE_DESCRIPTION ||
        slot ==
            DIO_SETTINGS_TEXT_FOLLOW_UP_ENABLED_DESCRIPTION ||
        slot ==
            DIO_SETTINGS_TEXT_FOLLOW_UP_SECONDS_DESCRIPTION ||
        slot ==
            DIO_SETTINGS_TEXT_REDUCED_MOTION_DESCRIPTION;
}

static bool dio_settings_is_button_text(
    DioSettingsText slot) {
    return slot == DIO_SETTINGS_TEXT_CANCEL ||
        slot == DIO_SETTINGS_TEXT_SAVE ||
        slot == DIO_SETTINGS_TEXT_LOCALE_VALUE ||
        slot == DIO_SETTINGS_TEXT_MICROPHONE_VALUE;
}

static bool dio_settings_text(
    DioSettingsView *view,
    DioSettingsText slot,
    const wchar_t *text,
    float width,
    float size,
    float line_height,
    CuiTextWeight weight,
    CuiTextAlign align) {
    CuiTextLayoutDesc desc;
    CuiResult result;

    ZeroMemory(&desc, sizeof(desc));
    desc.text = text;
    desc.length = wcslen(text);
    desc.max_width = width;
    desc.max_height = line_height + 4.0f;
    desc.font_size = size;
    desc.line_height = line_height;
    desc.weight = weight;
    desc.align = align;
    desc.wrap = CUI_TEXT_NO_WRAP;
    desc.direction =
        view->persian ? CUI_TEXT_RTL : CUI_TEXT_LTR;
    result = view->text[slot] == NULL
        ? cui_win32_text_layout_create(
            view->graphics,
            &desc,
            &view->text[slot])
        : cui_win32_text_layout_update(
            view->text[slot],
            &desc);
    return result == CUI_OK;
}

static void dio_settings_surface(
    DioSettingsView *view,
    CuiRect bounds,
    CuiColorRole fill,
    CuiColorRole border,
    CuiRadiusRole radius,
    float border_width,
    unsigned int flags) {
    CuiSurfacePaint paint;
    ZeroMemory(&paint, sizeof(paint));
    paint.bounds = bounds;
    paint.fill = fill;
    paint.border = border;
    paint.radius = radius;
    paint.border_width = border_width;
    paint.flags = flags;
    cui_win32_draw_surface(view->graphics, &paint);
}

static void dio_settings_draw_field(
    DioSettingsView *view,
    DioSettingsControl control) {
    const unsigned int state =
        view->control_state[control];
    CuiColorRole border =
        (state & DIO_SETTINGS_CONTROL_STATE_DISABLED) != 0u
            ? CUI_ROLE_INPUT
            : CUI_ROLE_BORDER;
    float border_width = 1.0f;

    if ((state & DIO_SETTINGS_CONTROL_STATE_INVALID) != 0u) {
        border = CUI_ROLE_DESTRUCTIVE;
        border_width = 2.0f;
    } else if ((state &
                DIO_SETTINGS_CONTROL_STATE_FOCUSED) != 0u) {
        border = CUI_ROLE_RING;
        border_width = 2.0f;
    }
    dio_settings_surface(
        view,
        dio_settings_view_control_bounds(view, control),
        CUI_ROLE_MUTED,
        border,
        CUI_RADIUS_MEDIUM,
        border_width,
        CUI_SURFACE_FILL | CUI_SURFACE_BORDER);
}

bool dio_settings_view_create(
    CuiWin32Context *graphics,
    DioSettingsView **output) {
    DioSettingsView *view;
    if (graphics == NULL || output == NULL) {
        return false;
    }
    view = (DioSettingsView *)calloc(1u, sizeof(*view));
    if (view == NULL) {
        return false;
    }
    view->graphics = graphics;
    *output = view;
    return true;
}

void dio_settings_view_destroy(
    DioSettingsView *view) {
    size_t index;
    if (view == NULL) {
        return;
    }
    for (index = 0u;
         index < DIO_SETTINGS_TEXT_COUNT;
         ++index) {
        cui_win32_text_layout_destroy(view->text[index]);
    }
    free(view);
}

float dio_settings_view_scroll_max(
    float viewport_height) {
    const float visible_bottom =
        viewport_height -
        (float)DIO_SETTINGS_FOOTER_HEIGHT -
        12.0f;
    const float scroll_max =
        (float)DIO_SETTINGS_BODY_BOTTOM - visible_bottom;
    return scroll_max > 0.0f ? scroll_max : 0.0f;
}

bool dio_settings_view_prepare(
    DioSettingsView *view,
    bool persian,
    float width,
    float viewport_height,
    float scroll_y) {
    static const wchar_t *const english
        [DIO_SETTINGS_TEXT_COUNT] = {
            L"Settings",
            L"Voice, model provider, prompt, and tools",
            L"Input",
            L"Language",
            L"Interface and recognition",
            L"Microphone",
            L"Recording device",
            L"End-of-command delay",
            L"0.1\u201360 seconds",
            L"Conversation",
            L"Follow-up listening",
            L"Continue after each reply",
            L"Follow-up time",
            L"1\u201360 seconds",
            L"Accessibility",
            L"Motion",
            L"Limit non-essential animation",
            L"Cancel",
            L"Save",
            L"",
            L""};
    static const wchar_t *const farsi
        [DIO_SETTINGS_TEXT_COUNT] = {
            L"\u062a\u0646\u0638\u06cc\u0645\u0627\u062a",
            L"\u0635\u062f\u0627\u060c \u0627\u0631\u0627\u0626\u0647\u200c\u062f\u0647\u0646\u062f\u0647\u060c \u067e\u0631\u0627\u0645\u067e\u062a \u0648 \u0627\u0628\u0632\u0627\u0631\u0647\u0627",
            L"\u0648\u0631\u0648\u062f\u06cc",
            L"\u0632\u0628\u0627\u0646",
            L"\u0631\u0627\u0628\u0637 \u0648 \u062a\u0634\u062e\u06cc\u0635 \u06af\u0641\u062a\u0627\u0631",
            L"\u0645\u06cc\u06a9\u0631\u0648\u0641\u0648\u0646",
            L"\u062f\u0633\u062a\u06af\u0627\u0647 \u0636\u0628\u0637",
            L"\u0645\u06a9\u062b \u067e\u0627\u06cc\u0627\u0646 \u0641\u0631\u0645\u0627\u0646",
            L"\u06f0\u066b\u06f1 \u062a\u0627 \u06f6\u06f0 \u062b\u0627\u0646\u06cc\u0647",
            L"\u06af\u0641\u062a\u200c\u0648\u06af\u0648",
            L"\u067e\u0631\u0633\u0634 \u0628\u0639\u062f\u06cc",
            L"\u067e\u0633 \u0627\u0632 \u0647\u0631 \u067e\u0627\u0633\u062e \u06af\u0648\u0634 \u0628\u062f\u0647",
            L"\u0645\u0647\u0644\u062a \u067e\u0631\u0633\u0634",
            L"\u06f1 \u062a\u0627 \u06f6\u06f0 \u062b\u0627\u0646\u06cc\u0647",
            L"\u062f\u0633\u062a\u0631\u0633\u200c\u067e\u0630\u06cc\u0631\u06cc",
            L"\u062d\u0631\u06a9\u062a",
            L"\u062d\u0631\u06a9\u062a\u200c\u0647\u0627\u06cc \u063a\u06cc\u0631\u0636\u0631\u0648\u0631\u06cc \u0631\u0627 \u0645\u062d\u062f\u0648\u062f \u06a9\u0646",
            L"\u0627\u0646\u0635\u0631\u0627\u0641",
            L"\u0630\u062e\u06cc\u0631\u0647",
            L"",
            L""};
    const wchar_t *const *copy = persian ? farsi : english;
    const float pad = 24.0f;
    const float inner_width = width - pad * 2.0f;
    const float gap = width < 480.0f ? 16.0f : 20.0f;
    const float label_width =
        width < 480.0f ? 148.0f : 172.0f;
    const float control_width =
        inner_width - label_width - gap;
    const float control_x = persian
        ? pad
        : pad + label_width + gap;
    const float label_x = persian
        ? pad + control_width + gap
        : pad;
    const float numeric_width =
        control_width < 132.0f
            ? control_width
            : 132.0f;
    const float footer_y =
        viewport_height -
        (float)DIO_SETTINGS_FOOTER_HEIGHT;
    const float buttons_x =
        width - pad - 228.0f;
    const float maximum_scroll =
        dio_settings_view_scroll_max(viewport_height);
    size_t index;

    if (view == NULL || width < 340.0f ||
        viewport_height < 220.0f) {
        return false;
    }
    view->persian = persian;
    view->width = width;
    view->viewport_height = viewport_height;
    view->scroll_y = scroll_y < 0.0f
        ? 0.0f
        : scroll_y > maximum_scroll
            ? maximum_scroll
            : scroll_y;
    view->label_x = label_x;
    view->label_width = label_width;
    view->control_width = control_width;

    view->controls[DIO_SETTINGS_CONTROL_LOCALE] =
        (CuiRect){control_x, 164.0f, control_width, 36.0f};
    view->controls[DIO_SETTINGS_CONTROL_MICROPHONE] =
        (CuiRect){control_x, 214.0f, control_width, 36.0f};
    view->controls[DIO_SETTINGS_CONTROL_SILENCE] =
        (CuiRect){control_x, 264.0f, numeric_width, 36.0f};
    view->controls[DIO_SETTINGS_CONTROL_FOLLOW_UP_ENABLED] =
        (CuiRect){control_x, 354.0f, control_width, 32.0f};
    view->controls[DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS] =
        (CuiRect){control_x, 400.0f, numeric_width, 36.0f};
    view->controls[DIO_SETTINGS_CONTROL_REDUCED_MOTION] =
        (CuiRect){control_x, 490.0f, control_width, 32.0f};
    view->controls[DIO_SETTINGS_CONTROL_CANCEL] =
        (CuiRect){buttons_x, footer_y + 14.0f, 100.0f, 36.0f};
    view->controls[DIO_SETTINGS_CONTROL_SAVE] =
        (CuiRect){
            buttons_x + 108.0f,
            footer_y + 14.0f,
            120.0f,
            36.0f};

    for (index = 0u;
         index < DIO_SETTINGS_TEXT_COUNT;
         ++index) {
        const DioSettingsText slot =
            (DioSettingsText)index;
        const bool title =
            slot == DIO_SETTINGS_TEXT_TITLE;
        const bool subtitle =
            slot == DIO_SETTINGS_TEXT_SUBTITLE;
        const bool section =
            dio_settings_is_section(slot);
        const bool description =
            dio_settings_is_description(slot);
        const bool button =
            dio_settings_is_button_text(slot);
        const bool choice =
            slot == DIO_SETTINGS_TEXT_LOCALE_VALUE ||
            slot == DIO_SETTINGS_TEXT_MICROPHONE_VALUE;
        const float text_width =
            title || subtitle
                ? width - 64.0f
                : button
                    ? choice
                        ? control_width - 32.0f
                        : slot == DIO_SETTINGS_TEXT_SAVE
                            ? 104.0f
                            : 84.0f
                    : section
                        ? inner_width
                        : label_width;
        if (!dio_settings_text(
                view,
                slot,
                copy[index],
                text_width,
                title
                    ? 20.0f
                    : subtitle || description
                        ? 12.0f
                        : 14.0f,
                title
                    ? 28.0f
                    : subtitle || description
                        ? 16.0f
                        : 20.0f,
                title
                    ? CUI_WEIGHT_SEMIBOLD
                    : section || (!description && !subtitle)
                        ? CUI_WEIGHT_MEDIUM
                        : CUI_WEIGHT_REGULAR,
                button
                    ? CUI_TEXT_CENTER
                    : CUI_TEXT_LEADING)) {
            return false;
        }
    }
    return true;
}

CuiRect dio_settings_view_scroll_viewport(
    const DioSettingsView *view) {
    if (view == NULL) {
        return (CuiRect){0};
    }
    return (CuiRect){
        0.0f,
        (float)DIO_SETTINGS_HEADER_HEIGHT,
        view->width,
        view->viewport_height -
            (float)DIO_SETTINGS_HEADER_HEIGHT -
            (float)DIO_SETTINGS_FOOTER_HEIGHT};
}

CuiResult dio_settings_view_draw(
    DioSettingsView *view) {
    static const struct {
        DioSettingsText text;
        float y;
        bool section;
        bool description;
    } labels[] = {
        {DIO_SETTINGS_TEXT_INPUT_SECTION, 136.0f, true, false},
        {DIO_SETTINGS_TEXT_LANGUAGE, 161.0f, false, false},
        {DIO_SETTINGS_TEXT_LANGUAGE_DESCRIPTION, 182.0f, false, true},
        {DIO_SETTINGS_TEXT_MICROPHONE, 211.0f, false, false},
        {DIO_SETTINGS_TEXT_MICROPHONE_DESCRIPTION, 232.0f, false, true},
        {DIO_SETTINGS_TEXT_SILENCE, 261.0f, false, false},
        {DIO_SETTINGS_TEXT_SILENCE_DESCRIPTION, 282.0f, false, true},
        {DIO_SETTINGS_TEXT_CONVERSATION_SECTION, 326.0f, true, false},
        {DIO_SETTINGS_TEXT_FOLLOW_UP_ENABLED, 349.0f, false, false},
        {DIO_SETTINGS_TEXT_FOLLOW_UP_ENABLED_DESCRIPTION, 370.0f, false, true},
        {DIO_SETTINGS_TEXT_FOLLOW_UP_SECONDS, 397.0f, false, false},
        {DIO_SETTINGS_TEXT_FOLLOW_UP_SECONDS_DESCRIPTION, 418.0f, false, true},
        {DIO_SETTINGS_TEXT_ACCESSIBILITY_SECTION, 462.0f, true, false},
        {DIO_SETTINGS_TEXT_REDUCED_MOTION, 485.0f, false, false},
        {DIO_SETTINGS_TEXT_REDUCED_MOTION_DESCRIPTION, 506.0f, false, true}};
    const float pad = 24.0f;
    const float footer_y =
        view != NULL
            ? view->viewport_height -
                (float)DIO_SETTINGS_FOOTER_HEIGHT
            : 0.0f;
    const float header_text_x =
        view != NULL && view->persian ? pad : 40.0f;
    CuiResult result;
    size_t index;

    if (view == NULL) {
        return CUI_INVALID_ARGUMENT;
    }
    result = cui_win32_begin_frame(
        view->graphics,
        CUI_ROLE_BACKGROUND);
    if (result != CUI_OK) {
        return result;
    }

    if (view->page == DIO_SETTINGS_PAGE_GENERAL) {
        for (index = 0u; index < _countof(labels); ++index) {
            cui_win32_draw_text(
                view->graphics,
                view->text[labels[index].text],
                (CuiPoint){
                    labels[index].section
                        ? pad
                        : view->label_x,
                    labels[index].y - view->scroll_y},
                labels[index].description
                    ? CUI_ROLE_MUTED_FOREGROUND
                    : CUI_ROLE_FOREGROUND);
        }
        dio_settings_draw_field(
            view,
            DIO_SETTINGS_CONTROL_SILENCE);
        dio_settings_draw_field(
            view,
            DIO_SETTINGS_CONTROL_FOLLOW_UP_SECONDS);
    }

    dio_settings_surface(
        view,
        (CuiRect){
            0.0f,
            0.0f,
            view->width,
            (float)DIO_SETTINGS_HEADER_HEIGHT},
        CUI_ROLE_BACKGROUND,
        CUI_ROLE_BACKGROUND,
        CUI_RADIUS_NONE,
        0.0f,
        CUI_SURFACE_FILL);
    dio_settings_surface(
        view,
        (CuiRect){
            view->persian ? view->width - 28.0f : pad,
            20.0f,
            4.0f,
            42.0f},
        CUI_ROLE_PRIMARY,
        CUI_ROLE_PRIMARY,
        CUI_RADIUS_FULL,
        0.0f,
        CUI_SURFACE_FILL);
    cui_win32_draw_text(
        view->graphics,
        view->text[DIO_SETTINGS_TEXT_TITLE],
        (CuiPoint){header_text_x, 17.0f},
        CUI_ROLE_FOREGROUND);
    cui_win32_draw_text(
        view->graphics,
        view->text[DIO_SETTINGS_TEXT_SUBTITLE],
        (CuiPoint){header_text_x, 49.0f},
        CUI_ROLE_MUTED_FOREGROUND);

    dio_settings_surface(
        view,
        (CuiRect){
            0.0f,
            footer_y,
            view->width,
            (float)DIO_SETTINGS_FOOTER_HEIGHT},
        CUI_ROLE_CARD,
        CUI_ROLE_CARD,
        CUI_RADIUS_NONE,
        0.0f,
        CUI_SURFACE_FILL);
    cui_win32_draw_line(
        view->graphics,
        (CuiPoint){0.0f, footer_y},
        (CuiPoint){view->width, footer_y},
        CUI_ROLE_INPUT,
        1.0f);
    return cui_win32_end_frame(view->graphics);
}

CuiRect dio_settings_view_control_bounds(
    const DioSettingsView *view,
    DioSettingsControl control) {
    CuiRect bounds;
    if (view == NULL ||
        control < DIO_SETTINGS_CONTROL_LOCALE ||
        control >= DIO_SETTINGS_CONTROL_COUNT) {
        return (CuiRect){0};
    }
    bounds = view->controls[control];
    if (control != DIO_SETTINGS_CONTROL_CANCEL &&
        control != DIO_SETTINGS_CONTROL_SAVE) {
        bounds.y -= view->scroll_y;
    }
    return bounds;
}

bool dio_settings_view_control_visible(
    const DioSettingsView *view,
    DioSettingsControl control) {
    CuiRect bounds;
    CuiRect viewport;
    if (view == NULL ||
        control < DIO_SETTINGS_CONTROL_LOCALE ||
        control >= DIO_SETTINGS_CONTROL_COUNT) {
        return false;
    }
    if (control == DIO_SETTINGS_CONTROL_CANCEL ||
        control == DIO_SETTINGS_CONTROL_SAVE) {
        return true;
    }
    bounds = dio_settings_view_control_bounds(view, control);
    viewport = dio_settings_view_scroll_viewport(view);
    return bounds.y >= viewport.y &&
        bounds.y + bounds.height <=
            viewport.y + viewport.height;
}

void dio_settings_view_set_control_state(
    DioSettingsView *view,
    DioSettingsControl control,
    unsigned int state) {
    const unsigned int valid =
        DIO_SETTINGS_CONTROL_STATE_FOCUSED |
        DIO_SETTINGS_CONTROL_STATE_DISABLED |
        DIO_SETTINGS_CONTROL_STATE_INVALID;
    if (view == NULL ||
        control < DIO_SETTINGS_CONTROL_LOCALE ||
        control >= DIO_SETTINGS_CONTROL_COUNT) {
        return;
    }
    view->control_state[control] = state & valid;
}

void dio_settings_view_set_page(
    DioSettingsView *view,
    DioSettingsPage page) {
    if (view != NULL && page >= DIO_SETTINGS_PAGE_GENERAL &&
        page < DIO_SETTINGS_PAGE_COUNT) {
        view->page = page;
    }
}

const CuiWin32TextLayout *dio_settings_view_button_label(
    const DioSettingsView *view,
    DioSettingsControl control) {
    if (view == NULL) {
        return NULL;
    }
    if (control == DIO_SETTINGS_CONTROL_CANCEL) {
        return view->text[DIO_SETTINGS_TEXT_CANCEL];
    }
    if (control == DIO_SETTINGS_CONTROL_SAVE) {
        return view->text[DIO_SETTINGS_TEXT_SAVE];
    }
    if (control == DIO_SETTINGS_CONTROL_LOCALE) {
        return view->text[DIO_SETTINGS_TEXT_LOCALE_VALUE];
    }
    if (control == DIO_SETTINGS_CONTROL_MICROPHONE) {
        return view->text[DIO_SETTINGS_TEXT_MICROPHONE_VALUE];
    }
    return NULL;
}

bool dio_settings_view_set_choice_text(
    DioSettingsView *view,
    DioSettingsControl control,
    const wchar_t *text) {
    DioSettingsText slot;
    CuiTextLayoutDesc desc;
    CuiResult result;
    if (view == NULL || text == NULL) {
        return false;
    }
    if (control == DIO_SETTINGS_CONTROL_LOCALE) {
        slot = DIO_SETTINGS_TEXT_LOCALE_VALUE;
    } else if (control == DIO_SETTINGS_CONTROL_MICROPHONE) {
        slot = DIO_SETTINGS_TEXT_MICROPHONE_VALUE;
    } else {
        return false;
    }
    ZeroMemory(&desc, sizeof(desc));
    desc.text = text;
    desc.length = wcslen(text);
    desc.max_width =
        view->controls[control].width - 32.0f;
    desc.max_height = 24.0f;
    desc.font_size = 13.0f;
    desc.line_height = 20.0f;
    desc.weight = CUI_WEIGHT_REGULAR;
    desc.align = CUI_TEXT_CENTER;
    desc.wrap = CUI_TEXT_NO_WRAP;
    desc.direction = CUI_TEXT_AUTO;
    result = cui_win32_text_layout_update(
        view->text[slot],
        &desc);
    return result == CUI_OK;
}
