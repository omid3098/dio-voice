#include "sentence_buffer.h"

#include <string.h>

static bool dio_is_space(unsigned char value) {
    return
        value == (unsigned char)' ' ||
        value == (unsigned char)'\t' ||
        value == (unsigned char)'\r' ||
        value == (unsigned char)'\n';
}
static bool dio_is_boundary(
    const char *data,
    size_t length,
    size_t index,
    size_t *boundary) {
    const unsigned char value = (unsigned char)data[index];
    size_t clause_end = 0u;

    if (value == (unsigned char)'.' ||
        value == (unsigned char)'!' ||
        value == (unsigned char)'?' ||
        value == (unsigned char)'\n') {
        *boundary = index + 1u;
        return true;
    }
    if (value == 0xD8u &&
        index + 1u < length &&
        (unsigned char)data[index + 1u] == 0x9Fu) {
        *boundary = index + 2u;
        return true;
    }
    if (value == (unsigned char)',' ||
        value == (unsigned char)';') {
        clause_end = index + 1u;
    } else if (
        value == 0xD8u &&
        index + 1u < length &&
        ((unsigned char)data[index + 1u] == 0x8Cu ||
         (unsigned char)data[index + 1u] == 0x9Bu)) {
        clause_end = index + 2u;
    }
    if (clause_end >= DIO_SENTENCE_CLAUSE_LIMIT &&
        clause_end < length &&
        dio_is_space((unsigned char)data[clause_end])) {
        *boundary = clause_end + 1u;
        return true;
    }
    return false;
}

static bool dio_emit_prefix(
    DioSentenceBuffer *buffer,
    size_t count,
    DioSentenceCallback callback,
    void *context) {
    size_t begin = 0u;
    size_t end = count;

    while (begin < end && dio_is_space((unsigned char)buffer->data[begin])) {
        ++begin;
    }
    while (end > begin && dio_is_space((unsigned char)buffer->data[end - 1u])) {
        --end;
    }
    if (end > begin &&
        callback != NULL &&
        !callback(context, buffer->data + begin, end - begin)) {
        return false;
    }
    memmove(buffer->data, buffer->data + count, buffer->length - count);
    buffer->length -= count;
    return true;
}

void dio_sentence_buffer_init(DioSentenceBuffer *buffer) {
    if (buffer != NULL) {
        buffer->length = 0u;
    }
}

bool dio_sentence_buffer_store(
    DioSentenceBuffer *buffer,
    const char *utf8,
    size_t length) {
    if (buffer == NULL ||
        (length != 0u && utf8 == NULL) ||
        length > sizeof(buffer->data) - buffer->length) {
        return false;
    }
    if (length != 0u) {
        (void)memcpy(
            buffer->data + buffer->length,
            utf8,
            length);
        buffer->length += length;
    }
    return true;
}

bool dio_sentence_buffer_append(
    DioSentenceBuffer *buffer,
    const char *utf8,
    size_t length,
    DioSentenceCallback callback,
    void *context) {
    size_t index;

    if (buffer == NULL ||
        (length != 0u && utf8 == NULL) ||
        length > sizeof(buffer->data) - buffer->length) {
        return false;
    }
    if (length != 0u) {
        memcpy(buffer->data + buffer->length, utf8, length);
        buffer->length += length;
    }
    index = 0u;
    while (index < buffer->length) {
        size_t boundary = 0u;
        if (dio_is_boundary(buffer->data, buffer->length, index, &boundary)) {
            while (boundary < buffer->length &&
                   (buffer->data[boundary] == '"' ||
                    buffer->data[boundary] == '\'' ||
                    (unsigned char)buffer->data[boundary] == 0xBBu)) {
                ++boundary;
            }
            if (!dio_emit_prefix(buffer, boundary, callback, context)) {
                return false;
            }
            index = 0u;
            continue;
        }
        if (index >= DIO_SENTENCE_SOFT_LIMIT &&
            dio_is_space((unsigned char)buffer->data[index])) {
            if (!dio_emit_prefix(buffer, index + 1u, callback, context)) {
                return false;
            }
            index = 0u;
            continue;
        }
        ++index;
    }
    return true;
}

bool dio_sentence_buffer_flush(
    DioSentenceBuffer *buffer,
    DioSentenceCallback callback,
    void *context) {
    if (buffer == NULL) {
        return false;
    }
    return buffer->length == 0u ||
        dio_emit_prefix(buffer, buffer->length, callback, context);
}
