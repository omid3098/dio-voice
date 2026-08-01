#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "earcon.h"
#include "pcm_stream.h"

static void count_started(void *context) {
    unsigned int *count = (unsigned int *)context;
    ++*count;
}

int main(void) {
    int16_t samples[480];
    char error[256] = "";
    unsigned int started = 0u;

    for (size_t index = 0u;
         index < sizeof(samples) / sizeof(samples[0]);
         ++index) {
        samples[index] = (int16_t)(index + 1u);
    }
    DioPcmStream *stream = dio_pcm_stream_create(24000u);
    assert(stream != NULL);
    assert(dio_pcm_stream_write(
        stream,
        samples,
        sizeof(samples) / sizeof(samples[0])));
    assert(dio_pcm_stream_finish(stream));
    const DioTtsResult result = dio_pcm_stream_play(
        stream,
        true,
        NULL,
        NULL,
        count_started,
        &started,
        error,
        sizeof(error));
    if (result != DIO_TTS_SUCCEEDED) {
        (void)fprintf(stderr, "PCM null playback failed: %s\n", error);
    }
    assert(result == DIO_TTS_SUCCEEDED);
    assert(started == 1u);
    dio_pcm_stream_destroy(stream);

    stream = dio_pcm_stream_create(24000u);
    assert(stream != NULL);
    assert(dio_pcm_stream_write(stream, samples, 1u));
    started = 0u;
    dio_pcm_stream_cancel(stream);
    assert(dio_pcm_stream_play(
        stream,
        true,
        NULL,
        NULL,
        count_started,
        &started,
        error,
        sizeof(error)) == DIO_TTS_CANCELLED);
    assert(started == 0u);
    dio_pcm_stream_destroy(stream);

    {
        /*
         * One warm null-backend device must play both cues. This catches
         * regressions back to unreliable one-device-per-cue playback.
         */
        DioEarcon *earcon =
            dio_earcon_open(NULL, error, sizeof(error));
        unsigned int latency_ms = UINT_MAX;
        const ULONGLONG started_at = GetTickCount64();
        assert(earcon != NULL);
        assert(dio_earcon_play_listening(
            earcon,
            started_at,
            &latency_ms,
            error,
            sizeof(error)));
        assert(latency_ms != UINT_MAX);

        latency_ms = UINT_MAX;
        assert(dio_earcon_play_processing(
            earcon,
            GetTickCount64(),
            &latency_ms,
            error,
            sizeof(error)));
        {
            const ULONGLONG elapsed_ms =
                GetTickCount64() - started_at;
            assert(latency_ms != UINT_MAX);
            assert(elapsed_ms < 4000ull);
        }
        dio_earcon_close(earcon);
    }

    (void)printf("dio_pcm_stream: passed\n");
    return 0;
}
