#ifndef DIO_VOICE_APP_ANNOUNCEMENT_H
#define DIO_VOICE_APP_ANNOUNCEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

typedef struct DioAnnouncementInbox {
    wchar_t directory[MAX_PATH];
} DioAnnouncementInbox;

bool dio_announcement_inbox_open(
    DioAnnouncementInbox *inbox,
    const wchar_t *directory);
bool dio_announcement_schedule(
    const wchar_t *directory,
    unsigned int delay_seconds,
    const wchar_t *text);
bool dio_announcement_take(
    DioAnnouncementInbox *inbox,
    wchar_t *text,
    size_t text_capacity,
    wchar_t *receipt,
    size_t receipt_capacity);
void dio_announcement_finish(
    const wchar_t *receipt,
    bool spoken);

#endif
