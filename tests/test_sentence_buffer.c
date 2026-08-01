#include "sentence_buffer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct Capture {
    char text[5][128];
    size_t length[5];
    size_t count;
} Capture;

static bool capture_sentence(
    void *context,
    const char *text,
    size_t length) {
    Capture *capture = (Capture *)context;
    assert(capture->count < 5u);
    assert(length < sizeof(capture->text[0]));
    memcpy(capture->text[capture->count], text, length);
    capture->text[capture->count][length] = '\0';
    capture->length[capture->count] = length;
    ++capture->count;
    return true;
}

static void test_clause_boundaries(void) {
    static const char *const punctuation[] = {
        ",",
        ";",
        "\xD8\x8C",
        "\xD8\x9B"
    };
    size_t punctuation_index;

    for (punctuation_index = 0u;
         punctuation_index <
             sizeof(punctuation) / sizeof(punctuation[0]);
         ++punctuation_index) {
        DioSentenceBuffer buffer;
        Capture capture = {0};
        char input[DIO_SENTENCE_CLAUSE_LIMIT + 2u];
        const size_t punctuation_length =
            strlen(punctuation[punctuation_index]);
        const size_t prefix_length =
            DIO_SENTENCE_CLAUSE_LIMIT - punctuation_length;

        memset(input, 'a', prefix_length);
        memcpy(
            input + prefix_length,
            punctuation[punctuation_index],
            punctuation_length);
        input[DIO_SENTENCE_CLAUSE_LIMIT] = ' ';
        input[DIO_SENTENCE_CLAUSE_LIMIT + 1u] = 'b';

        dio_sentence_buffer_init(&buffer);
        assert(dio_sentence_buffer_append(
            &buffer,
            input,
            sizeof(input),
            capture_sentence,
            &capture));
        assert(capture.count == 1u);
        assert(
            capture.length[0] ==
            DIO_SENTENCE_CLAUSE_LIMIT);
        assert(memcmp(
            capture.text[0] + prefix_length,
            punctuation[punctuation_index],
            punctuation_length) == 0);
        assert(dio_sentence_buffer_flush(
            &buffer,
            capture_sentence,
            &capture));
        assert(capture.count == 2u);
        assert(strcmp(capture.text[1], "b") == 0);
    }

    {
        DioSentenceBuffer buffer;
        Capture capture = {0};
        const char early[] =
            "short, clause; \xD8\xB2\xD9\x88\xD8\xAF\xD8\x8C "
            "\xD8\xA8\xD8\xA7\xD8\xB2\xD9\x87\xD8\x9B rest";

        dio_sentence_buffer_init(&buffer);
        assert(dio_sentence_buffer_append(
            &buffer,
            early,
            sizeof(early) - 1u,
            capture_sentence,
            &capture));
        assert(capture.count == 0u);
    }

    {
        DioSentenceBuffer buffer;
        Capture capture = {0};
        char joined[DIO_SENTENCE_CLAUSE_LIMIT + 1u];

        memset(
            joined,
            'a',
            DIO_SENTENCE_CLAUSE_LIMIT - 1u);
        joined[DIO_SENTENCE_CLAUSE_LIMIT - 1u] = ',';
        joined[DIO_SENTENCE_CLAUSE_LIMIT] = 'b';
        dio_sentence_buffer_init(&buffer);
        assert(dio_sentence_buffer_append(
            &buffer,
            joined,
            sizeof(joined),
            capture_sentence,
            &capture));
        assert(capture.count == 0u);
    }
}

int main(void) {
    DioSentenceBuffer buffer;
    Capture capture = {0};
    const char persian[] = " \xD8\xAE\xD9\x88\xD8\xA8\xDB\x8C\xD8\x9F ";

    dio_sentence_buffer_init(&buffer);
    assert(dio_sentence_buffer_append(
        &buffer,
        "First",
        5u,
        capture_sentence,
        &capture));
    assert(dio_sentence_buffer_append(
        &buffer,
        " sentence. Next",
        strlen(" sentence. Next"),
        capture_sentence,
        &capture));
    assert(capture.count == 1u);
    assert(strcmp(capture.text[0], "First sentence.") == 0);
    assert(dio_sentence_buffer_append(
        &buffer,
        persian,
        sizeof(persian) - 1u,
        capture_sentence,
        &capture));
    assert(capture.count == 2u);
    assert(strcmp(capture.text[1], "Next \xD8\xAE\xD9\x88\xD8\xA8\xDB\x8C\xD8\x9F") == 0);
    assert(dio_sentence_buffer_append(
        &buffer,
        "tail",
        4u,
        capture_sentence,
        &capture));
    assert(dio_sentence_buffer_flush(&buffer, capture_sentence, &capture));
    assert(capture.count == 3u);
    assert(strcmp(capture.text[2], "tail") == 0);
    assert(buffer.length == 0u);
    assert(dio_sentence_buffer_store(
        &buffer,
        "Deferred. Whole response.",
        strlen("Deferred. Whole response.")));
    assert(capture.count == 3u);
    assert(dio_sentence_buffer_flush(
        &buffer,
        capture_sentence,
        &capture));
    assert(strcmp(
        capture.text[3],
        "Deferred. Whole response.") == 0);
    test_clause_boundaries();
    return 0;
}
