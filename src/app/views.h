#ifndef DIO_VOICE_APP_VIEWS_H
#define DIO_VOICE_APP_VIEWS_H

#include <stdbool.h>
#include <stddef.h>

#include <cui/cui_win32.h>

enum {
    DIO_VIEW_INITIAL_MESSAGES = 12,
    DIO_VIEW_TEXT_CAP = 8193
};

typedef enum DioUiState {
    DIO_UI_LOADING = 0,
    DIO_UI_IDLE,
    DIO_UI_LISTENING,
    DIO_UI_FOLLOW_UP,
    DIO_UI_THINKING,
    DIO_UI_SPEAKING,
    DIO_UI_ANNOUNCEMENT,
    DIO_UI_MUTED,
    DIO_UI_ERROR
} DioUiState;

typedef enum DioMessageKind {
    DIO_MESSAGE_USER = 0,
    DIO_MESSAGE_ASSISTANT,
    DIO_MESSAGE_ANNOUNCEMENT,
    DIO_MESSAGE_ERROR
} DioMessageKind;

typedef enum DioChipKind {
    DIO_CHIP_ACK = 0,
    DIO_CHIP_AGENT,
    DIO_CHIP_TTS
} DioChipKind;

enum {
    DIO_MESSAGE_MIC_INPUT = 1u << 0u,
    DIO_MESSAGE_ACCEPTED = 1u << 1u,
    DIO_MESSAGE_AUDIO_OUTPUT = 1u << 2u
};

typedef struct DioViewMessage {
    DioMessageKind kind;
    unsigned int flags;
    wchar_t *text;
    size_t text_length;
    size_t text_capacity;
} DioViewMessage;

typedef struct DioViewModel {
    bool rtl;
    bool transcript_focused;
    DioUiState state;
    wchar_t status[128];
    DioViewMessage *messages;
    size_t message_count;
    size_t message_capacity;
    float level;
    float phase;
    float content_bottom_inset;
} DioViewModel;

typedef struct DioView DioView;

void dio_view_theme_init(CuiTheme *theme);
float dio_view_message_text_width(
    float view_width,
    DioMessageKind kind);
bool dio_view_create(CuiWin32Context *graphics, DioView **output);
void dio_view_destroy(DioView *view);
bool dio_view_prepare(
    DioView *view,
    const DioViewModel *model,
    float width,
    float height);
CuiResult dio_view_draw(DioView *view, const DioViewModel *model);
CuiRect dio_view_close_bounds(const DioView *view);
float dio_view_scroll_y(const DioView *view);
float dio_view_scroll_max(const DioView *view);
float dio_view_scroll_page(const DioView *view);
bool dio_view_scroll_to(DioView *view, float position);
CuiRect dio_view_scrollbar_track(const DioView *view);
CuiRect dio_view_scrollbar_thumb(const DioView *view);
void dio_view_set_scroll_hot(DioView *view, bool hot);

#endif
