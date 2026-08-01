#include "views.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef struct DioViewRow {
    CuiRect bounds;
    CuiWin32TextLayout *text;
    bool rtl;
    wchar_t *source;
    wchar_t *rendered;
    size_t source_capacity;
    size_t rendered_capacity;
    float layout_width;
    float font_size;
    float line_height;
    CuiTextWeight weight;
} DioViewRow;

struct DioView {
    CuiWin32Context *graphics;
    CuiWin32TextLayout *status;
    DioViewRow *messages;
    size_t message_count;
    size_t message_capacity;
    CuiRect content_bounds;
    CuiRect close_bounds;
    CuiPoint status_origin;
    float scroll_y;
    float scroll_max;
    bool scroll_hot;
    float width;
    float height;
};

static float dio_maxf(float left, float right) {
    return left > right ? left : right;
}

static float dio_clampf(float value, float minimum, float maximum) {
    return value < minimum
        ? minimum
        : value > maximum
            ? maximum
            : value;
}

static size_t dio_wide_length(const wchar_t *text) {
    return text != NULL ? wcslen(text) : 0u;
}

static bool dio_first_strong_rtl(
    const wchar_t *text,
    bool fallback) {
    size_t index;
    if (text == NULL) {
        return fallback;
    }
    for (index = 0u; text[index] != L'\0'; ++index) {
        WORD bidi_class = 0u;
        if (!GetStringTypeW(
                CT_CTYPE2,
                &text[index],
                1,
                &bidi_class)) {
            continue;
        }
        if (bidi_class == C2_RIGHTTOLEFT) {
            return true;
        }
        if (bidi_class == C2_LEFTTORIGHT) {
            return false;
        }
    }
    return fallback;
}

static bool dio_ascii_word(wchar_t value) {
    return (value >= L'a' && value <= L'z') ||
        (value >= L'A' && value <= L'Z') ||
        (value >= L'0' && value <= L'9');
}

static bool dio_technical_ascii(
    const wchar_t *text,
    size_t start,
    size_t end) {
    size_t index;
    for (index = start; index < end; ++index) {
        if (text[index] == L'\\' ||
            text[index] == L'/' ||
            text[index] == L':' ||
            text[index] == L'?' ||
            text[index] == L'=' ||
            text[index] == L'&') {
            return true;
        }
    }
    return false;
}

static size_t dio_copy_bidi_safe(
    wchar_t *output,
    size_t output_count,
    const wchar_t *text,
    bool isolate_ascii) {
    size_t input = 0u;
    size_t used = 0u;
    while (text[input] != L'\0' && used + 1u < output_count) {
        if (isolate_ascii && dio_ascii_word(text[input])) {
            size_t end = input + 1u;
            size_t trimmed;
            size_t index;
            size_t joiners = 0u;
            bool technical;
            while (text[end] >= L'\x20' && text[end] <= L'\x7e') {
                ++end;
            }
            trimmed = end;
            while (trimmed > input && iswspace(text[trimmed - 1u]) != 0) {
                --trimmed;
            }
            technical = dio_technical_ascii(
                text,
                input,
                trimmed);
            if (technical && trimmed > input) {
                joiners = trimmed - input - 1u;
            }
            if (used + (trimmed - input) + joiners + 3u >=
                output_count) {
                break;
            }
            output[used++] = L'\x2066';
            for (index = input; index < trimmed; ++index) {
                output[used++] = text[input++];
                if (technical && index + 1u < trimmed) {
                    output[used++] = L'\x2060';
                }
            }
            output[used++] = L'\x2069';
            continue;
        }
        output[used++] = text[input++];
    }
    output[used] = L'\0';
    return used;
}

static bool dio_reserve_wide(
    wchar_t **buffer,
    size_t *capacity,
    size_t needed) {
    wchar_t *replacement;
    if (needed <= *capacity) {
        return true;
    }
    if (needed > (size_t)-1 / sizeof(**buffer)) {
        return false;
    }
    replacement = (wchar_t *)realloc(
        *buffer,
        needed * sizeof(**buffer));
    if (replacement == NULL) {
        return false;
    }
    *buffer = replacement;
    *capacity = needed;
    return true;
}

static bool dio_prepare_row_text(
    DioViewRow *row,
    const wchar_t *text,
    bool rtl,
    bool *changed) {
    const wchar_t *source = text != NULL ? text : L"";
    const size_t length = wcslen(source);
    size_t rendered_capacity;

    if (row->source != NULL &&
        row->rtl == rtl &&
        wcscmp(row->source, source) == 0) {
        *changed = false;
        return true;
    }
    if (length > ((size_t)-1 - 3u) / 2u) {
        return false;
    }
    rendered_capacity = rtl
        ? length * 2u + 3u
        : length + 1u;
    if (!dio_reserve_wide(
            &row->rendered,
            &row->rendered_capacity,
            rendered_capacity)) {
        return false;
    }
    if (!dio_reserve_wide(
            &row->source,
            &row->source_capacity,
            length + 1u)) {
        return false;
    }
    (void)memcpy(
        row->source,
        source,
        (length + 1u) * sizeof(source[0]));
    if (rtl) {
        (void)dio_copy_bidi_safe(
            row->rendered,
            row->rendered_capacity,
            source,
            true);
    } else {
        (void)memcpy(
            row->rendered,
            source,
            (length + 1u) * sizeof(source[0]));
    }
    row->rtl = rtl;
    *changed = true;
    return true;
}

static bool dio_text_helpers_self_test(void) {
    DioViewRow row;
    wchar_t long_text[2049];
    bool changed = false;
    bool valid;
    size_t index;

    ZeroMemory(&row, sizeof(row));
    valid = dio_prepare_row_text(
        &row,
        L"\x0635 C:\\tmp\\x.txt",
        true,
        &changed) &&
        changed &&
        wcschr(row.rendered, L'\x2066') != NULL &&
        wcschr(row.rendered, L'\x2069') != NULL &&
        wcschr(row.rendered, L'\x2060') != NULL;
    for (index = 0u; index + 1u < _countof(long_text); ++index) {
        long_text[index] = L'a';
    }
    long_text[index] = L'\0';
    valid = valid &&
        dio_prepare_row_text(
            &row,
            long_text,
            false,
            &changed) &&
        changed &&
        wcscmp(row.rendered, long_text) == 0;
    free(row.source);
    free(row.rendered);
    return valid;
}

static bool dio_update_layout(
    CuiWin32Context *graphics,
    CuiWin32TextLayout **slot,
    const wchar_t *text,
    float max_width,
    float max_height,
    float size,
    float line_height,
    CuiTextWeight weight,
    CuiTextAlign align,
    CuiTextWrap wrap,
    CuiTextDirection direction) {
    CuiTextLayoutDesc desc;
    CuiResult result;

    ZeroMemory(&desc, sizeof(desc));
    desc.text = text != NULL ? text : L"";
    desc.length = dio_wide_length(desc.text);
    desc.max_width = dio_maxf(max_width, 1.0f);
    desc.max_height = dio_maxf(max_height, 1.0f);
    desc.font_size = size;
    desc.line_height = line_height;
    desc.weight = weight;
    desc.align = align;
    desc.wrap = wrap;
    desc.direction = direction;
    result = *slot == NULL
        ? cui_win32_text_layout_create(graphics, &desc, slot)
        : cui_win32_text_layout_update(*slot, &desc);
    return result == CUI_OK;
}

static CuiColorRole dio_state_role(DioUiState state) {
    switch (state) {
    case DIO_UI_LOADING:
    case DIO_UI_SPEAKING:
        return CUI_ROLE_CHART_1;
    case DIO_UI_LISTENING:
    case DIO_UI_FOLLOW_UP:
        return CUI_ROLE_SUCCESS;
    case DIO_UI_THINKING:
        return CUI_ROLE_CHART_3;
    case DIO_UI_ANNOUNCEMENT:
        return CUI_ROLE_CHART_4;
    case DIO_UI_ERROR:
        return CUI_ROLE_DESTRUCTIVE;
    case DIO_UI_IDLE:
    case DIO_UI_MUTED:
    default:
        return CUI_ROLE_MUTED_FOREGROUND;
    }
}

static CuiColorRole dio_message_role(DioMessageKind kind) {
    switch (kind) {
    case DIO_MESSAGE_USER:
        return CUI_ROLE_SUCCESS;
    case DIO_MESSAGE_ASSISTANT:
        return CUI_ROLE_CHART_1;
    case DIO_MESSAGE_ANNOUNCEMENT:
        return CUI_ROLE_CHART_4;
    case DIO_MESSAGE_ERROR:
    default:
        return CUI_ROLE_DESTRUCTIVE;
    }
}

static CuiColorRole dio_message_text_role(DioMessageKind kind) {
    switch (kind) {
    case DIO_MESSAGE_USER:
        return CUI_ROLE_MUTED_FOREGROUND;
    case DIO_MESSAGE_ASSISTANT:
    case DIO_MESSAGE_ANNOUNCEMENT:
        return CUI_ROLE_FOREGROUND;
    case DIO_MESSAGE_ERROR:
    default:
        return CUI_ROLE_DESTRUCTIVE;
    }
}

static bool dio_message_reserves_tag(DioMessageKind kind) {
    return
        kind == DIO_MESSAGE_USER ||
        kind == DIO_MESSAGE_ASSISTANT;
}

float dio_view_message_text_width(
    float view_width,
    DioMessageKind kind) {
    const float row_width = view_width - 38.0f;
    return row_width -
        (dio_message_reserves_tag(kind)
            ? 56.0f
            : 28.0f);
}

static bool dio_message_has_tag(const DioViewMessage *message) {
    return
        (message->kind == DIO_MESSAGE_USER &&
         (message->flags & DIO_MESSAGE_MIC_INPUT) != 0u) ||
        (message->kind == DIO_MESSAGE_ASSISTANT &&
         (message->flags & DIO_MESSAGE_AUDIO_OUTPUT) != 0u);
}

void dio_view_theme_init(CuiTheme *theme) {
    if (theme == NULL) {
        return;
    }
    cui_theme_init(theme, CUI_THEME_DARK);
    theme->colors[CUI_ROLE_BACKGROUND] = cui_color_rgb8(28u, 28u, 31u);
    theme->colors[CUI_ROLE_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_CARD] = cui_color_rgb8(35u, 35u, 39u);
    theme->colors[CUI_ROLE_CARD_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_POPOVER] = cui_color_rgb8(35u, 35u, 39u);
    theme->colors[CUI_ROLE_POPOVER_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_PRIMARY] = cui_color_rgb8(229u, 138u, 92u);
    theme->colors[CUI_ROLE_PRIMARY_FOREGROUND] = cui_color_rgb8(33u, 19u, 13u);
    theme->colors[CUI_ROLE_SECONDARY] = cui_color_rgb8(43u, 43u, 48u);
    theme->colors[CUI_ROLE_SECONDARY_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_MUTED] = cui_color_rgb8(43u, 43u, 48u);
    theme->colors[CUI_ROLE_MUTED_FOREGROUND] = cui_color_rgb8(185u, 186u, 193u);
    theme->colors[CUI_ROLE_ACCENT] = cui_color_rgb8(58u, 48u, 43u);
    theme->colors[CUI_ROLE_ACCENT_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_DESTRUCTIVE] = cui_color_rgb8(242u, 119u, 126u);
    theme->colors[CUI_ROLE_DESTRUCTIVE_FOREGROUND] = cui_color_rgb8(36u, 16u, 18u);
    theme->colors[CUI_ROLE_SUCCESS] = cui_color_rgb8(67u, 208u, 138u);
    theme->colors[CUI_ROLE_SUCCESS_FOREGROUND] = cui_color_rgb8(11u, 35u, 25u);
    /*
     * BORDER is intentionally the 3:1 interactive edge. INPUT is the quieter
     * decorative separator/outline used by passive surfaces.
     */
    theme->colors[CUI_ROLE_BORDER] = cui_color_rgb8(116u, 118u, 128u);
    theme->colors[CUI_ROLE_INPUT] = cui_color_rgb8(58u, 58u, 64u);
    theme->colors[CUI_ROLE_RING] = cui_color_rgb8(240u, 162u, 126u);
    theme->colors[CUI_ROLE_CHART_1] = cui_color_rgb8(102u, 167u, 242u);
    theme->colors[CUI_ROLE_CHART_2] = cui_color_rgb8(67u, 208u, 138u);
    theme->colors[CUI_ROLE_CHART_3] = cui_color_rgb8(225u, 179u, 86u);
    theme->colors[CUI_ROLE_CHART_4] = cui_color_rgb8(180u, 154u, 230u);
    theme->colors[CUI_ROLE_CHART_5] = cui_color_rgb8(229u, 138u, 92u);
    theme->colors[CUI_ROLE_SIDEBAR] = cui_color_rgb8(35u, 35u, 39u);
    theme->colors[CUI_ROLE_SIDEBAR_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_SIDEBAR_PRIMARY] = cui_color_rgb8(229u, 138u, 92u);
    theme->colors[CUI_ROLE_SIDEBAR_PRIMARY_FOREGROUND] = cui_color_rgb8(33u, 19u, 13u);
    theme->colors[CUI_ROLE_SIDEBAR_ACCENT] = cui_color_rgb8(58u, 48u, 43u);
    theme->colors[CUI_ROLE_SIDEBAR_ACCENT_FOREGROUND] = cui_color_rgb8(243u, 243u, 245u);
    theme->colors[CUI_ROLE_SIDEBAR_BORDER] = cui_color_rgb8(58u, 58u, 64u);
    theme->colors[CUI_ROLE_SIDEBAR_RING] = cui_color_rgb8(240u, 162u, 126u);
    theme->radii[CUI_RADIUS_SMALL] = 4.0f;
    theme->radii[CUI_RADIUS_MEDIUM] = 6.0f;
    theme->radii[CUI_RADIUS_LARGE] = 8.0f;
    theme->radii[CUI_RADIUS_EXTRA_LARGE] = 12.0f;
    theme->radii[CUI_RADIUS_FULL] = 999.0f;
}

bool dio_view_create(CuiWin32Context *graphics, DioView **output) {
    DioView *view;
    if (graphics == NULL ||
        output == NULL ||
        !dio_text_helpers_self_test()) {
        return false;
    }
    view = (DioView *)calloc(1u, sizeof(*view));
    if (view == NULL) {
        return false;
    }
    view->graphics = graphics;
    *output = view;
    return true;
}

void dio_view_destroy(DioView *view) {
    size_t index;
    if (view == NULL) {
        return;
    }
    cui_win32_text_layout_destroy(view->status);
    for (index = 0u; index < view->message_capacity; ++index) {
        cui_win32_text_layout_destroy(view->messages[index].text);
        free(view->messages[index].source);
        free(view->messages[index].rendered);
    }
    free(view->messages);
    free(view);
}

static bool dio_view_reserve_messages(
    DioView *view,
    size_t needed) {
    DioViewRow *replacement;
    size_t capacity = view->message_capacity > 0u
        ? view->message_capacity
        : DIO_VIEW_INITIAL_MESSAGES;
    if (needed <= view->message_capacity) {
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
    replacement = (DioViewRow *)realloc(
        view->messages,
        capacity * sizeof(*replacement));
    if (replacement == NULL) {
        return false;
    }
    ZeroMemory(
        replacement + view->message_capacity,
        (capacity - view->message_capacity) *
            sizeof(*replacement));
    view->messages = replacement;
    view->message_capacity = capacity;
    return true;
}

bool dio_view_prepare(
    DioView *view,
    const DioViewModel *model,
    float width,
    float height) {
    const float row_x = 12.0f;
    const float row_width = width - 38.0f;
    const CuiTextDirection chrome_direction = model != NULL && model->rtl
        ? CUI_TEXT_RTL
        : CUI_TEXT_LTR;
    const CuiTextAlign align = CUI_TEXT_LEADING;
    const float content_top = 52.0f;
    const float bottom_inset = model != NULL
        ? dio_clampf(
            model->content_bottom_inset,
            0.0f,
            dio_maxf(height - content_top - 9.0f, 0.0f))
        : 0.0f;
    const float content_bottom =
        height - 8.0f - bottom_inset;
    const bool followed_tail =
        view != NULL &&
        view->scroll_y >= view->scroll_max - 0.5f;
    float y = content_top + 4.0f;
    float content_height;
    float viewport_height;
    size_t index;

    if (view == NULL ||
        model == NULL ||
        (model->message_count > 0u && model->messages == NULL) ||
        width < 100.0f ||
        height < 60.0f ||
        !dio_view_reserve_messages(
            view,
            model->message_count)) {
        return false;
    }
    view->width = width;
    view->height = height;
    view->message_count = 0u;
    view->content_bounds = (CuiRect){0};
    view->close_bounds.x = width - 44.0f;
    view->close_bounds.y = 10.0f;
    view->close_bounds.width = 32.0f;
    view->close_bounds.height = 32.0f;
    view->status_origin.x = model->rtl ? width - 288.0f : 28.0f;
    view->status_origin.y = 17.0f;
    if (!dio_update_layout(
            view->graphics,
            &view->status,
            model->status,
            220.0f,
            20.0f,
            13.0f,
            18.0f,
            CUI_WEIGHT_MEDIUM,
            align,
            CUI_TEXT_NO_WRAP,
            chrome_direction)) {
        return false;
    }

    for (index = 0u; index < model->message_count; ++index) {
        const DioViewMessage *message = &model->messages[index];
        DioViewRow *row = &view->messages[index];
        const bool rtl = dio_first_strong_rtl(
            message->text,
            model->rtl);
        const CuiTextWeight weight =
            message->kind == DIO_MESSAGE_ASSISTANT
                ? CUI_WEIGHT_MEDIUM
                : CUI_WEIGHT_REGULAR;
        bool text_changed = false;
        float font_size;
        float line_height;
        float text_width;
        float max_height;
        CuiSize size;
        float row_height;

        text_width = dio_view_message_text_width(
            width,
            message->kind);
        font_size = message->kind == DIO_MESSAGE_ASSISTANT ? 14.0f : 13.0f;
        line_height = message->kind == DIO_MESSAGE_ASSISTANT ? 21.0f : 19.0f;
        if (model->rtl) {
            font_size += 0.5f;
            line_height += 1.0f;
        }
        if (!dio_prepare_row_text(
            row,
            message->text,
            rtl,
            &text_changed)) {
            return false;
        }
        max_height = dio_maxf(
            line_height,
            (float)(message->text_length + 1u) * line_height);
        if ((text_changed ||
             row->text == NULL ||
             fabsf(row->layout_width - text_width) > 0.01f ||
             fabsf(row->font_size - font_size) > 0.01f ||
             fabsf(row->line_height - line_height) > 0.01f ||
             row->weight != weight) &&
            !dio_update_layout(
                view->graphics,
                &row->text,
                row->rendered,
                text_width,
                max_height,
                font_size,
                line_height,
                weight,
                align,
                CUI_TEXT_WORD_WRAP,
                row->rtl ? CUI_TEXT_RTL : CUI_TEXT_LTR)) {
            row->layout_width = -1.0f;
            return false;
        }
        row->layout_width = text_width;
        row->font_size = font_size;
        row->line_height = line_height;
        row->weight = weight;
        size = cui_win32_text_layout_size(row->text);
        row_height = dio_maxf(34.0f, size.height + 12.0f);
        row->bounds.x = row_x;
        row->bounds.y = y;
        row->bounds.width = row_width;
        row->bounds.height = row_height;
        y += row_height;
    }
    if (model->message_count > 0u) {
        view->content_bounds = (CuiRect){
            8.0f,
            content_top,
            width - 16.0f,
            height - content_top - 8.0f - bottom_inset};
    }
    viewport_height = content_bottom - content_top;
    content_height = model->message_count > 0u
        ? y - content_top + 4.0f
        : 0.0f;
    view->scroll_max = dio_maxf(
        content_height - viewport_height,
        0.0f);
    view->scroll_y = followed_tail
        ? view->scroll_max
        : dio_clampf(
            view->scroll_y,
            0.0f,
            view->scroll_max);
    view->message_count = model->message_count;
    return true;
}

static void dio_draw_activity(DioView *view, const DioViewModel *model) {
    const CuiColorRole role = dio_state_role(model->state);
    float x;
    size_t index;

    if (model->state == DIO_UI_IDLE ||
        model->state == DIO_UI_MUTED ||
        model->state == DIO_UI_ERROR ||
        model->state == DIO_UI_ANNOUNCEMENT) {
        return;
    }
    x = model->rtl ? 16.0f : view->width - 108.0f;
    for (index = 0u; index < 5u; ++index) {
        const float wave = (sinf(model->phase + (float)index * 1.1f) + 1.0f) * 0.5f;
        const bool meter_state =
            model->state == DIO_UI_LISTENING ||
            model->state == DIO_UI_FOLLOW_UP ||
            model->state == DIO_UI_SPEAKING;
        const float reactive =
            meter_state && model->level > 0.02f
                ? model->level
                : wave;
        const float height =
            4.0f + reactive * (6.0f + (float)(index % 3u) * 1.5f);
        cui_win32_draw_line(
            view->graphics,
            (CuiPoint){x + (float)index * 7.0f, 26.0f - height * 0.5f},
            (CuiPoint){x + (float)index * 7.0f, 26.0f + height * 0.5f},
            role,
            2.5f);
    }
}

CuiResult dio_view_draw(DioView *view, const DioViewModel *model) {
    CuiSurfacePaint surface;
    CuiColorRole state_role;
    size_t index;
    CuiResult result;
    bool clipped = false;

    if (view == NULL || model == NULL) {
        return CUI_INVALID_ARGUMENT;
    }
    result = cui_win32_begin_frame(view->graphics, CUI_ROLE_BACKGROUND);
    if (result != CUI_OK) {
        return result;
    }
    surface.bounds = (CuiRect){0.5f, 0.5f, view->width - 1.0f, view->height - 1.0f};
    surface.fill = CUI_ROLE_CARD;
    surface.border = CUI_ROLE_INPUT;
    surface.radius = CUI_RADIUS_LARGE;
    surface.border_width = 1.0f;
    surface.flags = CUI_SURFACE_FILL | CUI_SURFACE_BORDER;
    cui_win32_draw_surface(view->graphics, &surface);

    if (view->content_bounds.height > 0.0f) {
        surface.bounds = view->content_bounds;
        surface.fill = CUI_ROLE_MUTED;
        surface.border = CUI_ROLE_INPUT;
        surface.radius = CUI_RADIUS_MEDIUM;
        surface.border_width = 1.0f;
        surface.flags = CUI_SURFACE_FILL | CUI_SURFACE_BORDER;
        cui_win32_draw_surface(view->graphics, &surface);
    }

    state_role = dio_state_role(model->state);
    cui_win32_draw_line(
        view->graphics,
        (CuiPoint){model->rtl ? view->width - 56.0f : 16.0f, 17.0f},
        (CuiPoint){model->rtl ? view->width - 56.0f : 16.0f, 35.0f},
        state_role,
        3.0f);
    cui_win32_draw_text(
        view->graphics,
        view->status,
        view->status_origin,
        CUI_ROLE_FOREGROUND);
    dio_draw_activity(view, model);

    if (view->content_bounds.height > 0.0f) {
        const CuiRect clip = {
            view->content_bounds.x + 1.0f,
            view->content_bounds.y + 1.0f,
            view->content_bounds.width - 2.0f,
            view->content_bounds.height - 2.0f};
        result = cui_win32_push_clip(
            view->graphics,
            clip);
        if (result != CUI_OK) {
            (void)cui_win32_end_frame(view->graphics);
            return result;
        }
        clipped = true;
    }
    for (index = 0u;
         index < model->message_count &&
         index < view->message_count;
         ++index) {
        CuiRect bounds = view->messages[index].bounds;
        const DioViewMessage *message =
            &model->messages[index];
        const CuiColorRole rail = dio_message_role(model->messages[index].kind);
        const bool reserves_tag =
            dio_message_reserves_tag(message->kind);
        bounds.y -= view->scroll_y;
        const float rail_x = view->messages[index].rtl
            ? bounds.x + bounds.width - 4.0f
            : bounds.x + 4.0f;
        if (bounds.height <= 0.0f ||
            bounds.y + bounds.height <=
                view->content_bounds.y + 1.0f ||
            bounds.y >=
                view->content_bounds.y +
                    view->content_bounds.height - 1.0f) {
            continue;
        }
        if (index > 0u) {
            cui_win32_draw_line(
                view->graphics,
                (CuiPoint){bounds.x + 12.0f, bounds.y},
                (CuiPoint){bounds.x + bounds.width - 12.0f, bounds.y},
                CUI_ROLE_INPUT,
                1.0f);
        }
        cui_win32_draw_line(
            view->graphics,
            (CuiPoint){rail_x, bounds.y + 6.0f},
            (CuiPoint){rail_x, bounds.y + bounds.height - 6.0f},
            rail,
            2.0f);
        if (dio_message_has_tag(message)) {
            const CuiRect tag = {
                view->messages[index].rtl
                    ? bounds.x + 8.0f
                    : bounds.x + bounds.width - 34.0f,
                bounds.y + 5.0f,
                26.0f,
                24.0f};
            cui_win32_draw_icon(
                view->graphics,
                message->kind == DIO_MESSAGE_USER
                    ? CUI_ICON_MICROPHONE
                    : CUI_ICON_VOLUME,
                (CuiRect){
                    tag.x + 7.0f,
                    tag.y + 6.0f,
                    12.0f,
                    12.0f},
                CUI_ROLE_BORDER,
                1.25f);
        }
        cui_win32_draw_text(
            view->graphics,
            view->messages[index].text,
            (CuiPoint){
                bounds.x +
                    (reserves_tag &&
                     view->messages[index].rtl
                        ? 42.0f
                        : 14.0f),
                bounds.y + 6.0f},
            dio_message_text_role(model->messages[index].kind));
    }
    if (view->scroll_max > 0.0f) {
        const CuiRect thumb =
            dio_view_scrollbar_thumb(view);
        const float x = thumb.x + thumb.width * 0.5f;
        cui_win32_draw_line(
            view->graphics,
            (CuiPoint){x, thumb.y},
            (CuiPoint){x, thumb.y + thumb.height},
            view->scroll_hot
                ? CUI_ROLE_MUTED_FOREGROUND
                : CUI_ROLE_BORDER,
            view->scroll_hot ? 3.0f : 2.0f);
    }
    if (clipped) {
        result = cui_win32_pop_clip(view->graphics);
        if (result != CUI_OK) {
            (void)cui_win32_end_frame(view->graphics);
            return result;
        }
    }
    if (model->transcript_focused &&
        view->content_bounds.height > 0.0f) {
        surface.bounds = view->content_bounds;
        surface.border = CUI_ROLE_RING;
        surface.radius = CUI_RADIUS_MEDIUM;
        surface.border_width = 2.0f;
        surface.flags = CUI_SURFACE_BORDER;
        cui_win32_draw_surface(
            view->graphics,
            &surface);
    }
    return cui_win32_end_frame(view->graphics);
}

CuiRect dio_view_close_bounds(const DioView *view) {
    return view != NULL ? view->close_bounds : (CuiRect){0};
}

float dio_view_scroll_y(const DioView *view) {
    return view != NULL ? view->scroll_y : 0.0f;
}

float dio_view_scroll_max(const DioView *view) {
    return view != NULL ? view->scroll_max : 0.0f;
}

float dio_view_scroll_page(const DioView *view) {
    return view != NULL
        ? dio_maxf(
            view->content_bounds.height - 40.0f,
            34.0f)
        : 0.0f;
}

bool dio_view_scroll_to(DioView *view, float position) {
    float clamped;
    if (view == NULL || isfinite(position) == 0) {
        return false;
    }
    clamped = dio_clampf(
        position,
        0.0f,
        view->scroll_max);
    if (fabsf(clamped - view->scroll_y) < 0.01f) {
        return false;
    }
    view->scroll_y = clamped;
    return true;
}

CuiRect dio_view_scrollbar_track(const DioView *view) {
    if (view == NULL ||
        view->scroll_max <= 0.0f ||
        view->content_bounds.height <= 16.0f) {
        return (CuiRect){0};
    }
    return (CuiRect){
        view->content_bounds.x +
            view->content_bounds.width - 14.0f,
        view->content_bounds.y + 8.0f,
        14.0f,
        view->content_bounds.height - 16.0f};
}

CuiRect dio_view_scrollbar_thumb(const DioView *view) {
    const CuiRect track =
        dio_view_scrollbar_track(view);
    CuiRect thumb = track;
    float height;
    float travel;
    if (track.height <= 0.0f) {
        return (CuiRect){0};
    }
    height = track.height *
        (view->content_bounds.height /
         (view->content_bounds.height + view->scroll_max));
    height = dio_clampf(height, 24.0f, track.height);
    travel = track.height - height;
    thumb.y = track.y +
        (view->scroll_max > 0.0f
            ? travel * view->scroll_y /
                view->scroll_max
            : 0.0f);
    thumb.height = height;
    return thumb;
}

void dio_view_set_scroll_hot(DioView *view, bool hot) {
    if (view != NULL) {
        view->scroll_hot = hot;
    }
}
