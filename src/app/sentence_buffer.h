#ifndef DIO_VOICE_APP_SENTENCE_BUFFER_H
#define DIO_VOICE_APP_SENTENCE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

enum {
    DIO_SENTENCE_BUFFER_CAPACITY = 16 * 1024,
    DIO_SENTENCE_CLAUSE_LIMIT = 96,
    DIO_SENTENCE_SOFT_LIMIT = 600
};

typedef bool (*DioSentenceCallback)(
    void *context,
    const char *utf8,
    size_t length);

typedef struct DioSentenceBuffer {
    char data[DIO_SENTENCE_BUFFER_CAPACITY];
    size_t length;
} DioSentenceBuffer;

void dio_sentence_buffer_init(DioSentenceBuffer *buffer);
bool dio_sentence_buffer_store(
    DioSentenceBuffer *buffer,
    const char *utf8,
    size_t length);
bool dio_sentence_buffer_append(
    DioSentenceBuffer *buffer,
    const char *utf8,
    size_t length,
    DioSentenceCallback callback,
    void *context);
bool dio_sentence_buffer_flush(
    DioSentenceBuffer *buffer,
    DioSentenceCallback callback,
    void *context);

#endif
