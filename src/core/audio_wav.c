#include "dio_voice/audio.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DioWaveFormat {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} DioWaveFormat;

static uint16_t dio_read_u16_le(const unsigned char *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t dio_read_u32_le(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void dio_audio_error(
    char *destination,
    size_t capacity,
    const char *message)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }

    (void)strncpy_s(destination, capacity, message, _TRUNCATE);
}

static bool dio_skip_bytes(FILE *file, uint32_t count)
{
    unsigned char discard[4096];
    uint32_t remaining = count;

    while (remaining > 0u) {
        const size_t requested =
            remaining < sizeof(discard) ? (size_t)remaining : sizeof(discard);
        if (fread(discard, 1u, requested, file) != requested) {
            return false;
        }
        remaining -= (uint32_t)requested;
    }
    return true;
}

static int16_t dio_clamp_i16(int value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static DioAudioResult dio_convert_pcm(
    const int16_t *input,
    size_t input_frame_count,
    const DioWaveFormat *format,
    DioAudioPcm *output)
{
    int16_t *mono = NULL;
    int16_t *converted = NULL;
    size_t output_count;
    size_t index;

    if (input_frame_count > SIZE_MAX / sizeof(*mono)) {
        return DIO_AUDIO_OUT_OF_MEMORY;
    }
    mono = (int16_t *)malloc(input_frame_count * sizeof(*mono));
    if (mono == NULL && input_frame_count != 0u) {
        return DIO_AUDIO_OUT_OF_MEMORY;
    }

    for (index = 0u; index < input_frame_count; ++index) {
        int64_t sum = 0;
        uint16_t channel;
        for (channel = 0u; channel < format->channels; ++channel) {
            sum += input[(index * format->channels) + channel];
        }
        mono[index] = dio_clamp_i16(
            (int)(sum / (int64_t)format->channels));
    }

    if (format->sample_rate == DIO_VOICE_SAMPLE_RATE) {
        output->samples = mono;
        output->sample_count = input_frame_count;
        output->sample_rate = DIO_VOICE_SAMPLE_RATE;
        output->channel_count = 1u;
        return DIO_AUDIO_OK;
    }

    if (input_frame_count >
        (SIZE_MAX - (format->sample_rate / 2u)) / DIO_VOICE_SAMPLE_RATE) {
        free(mono);
        return DIO_AUDIO_OUT_OF_MEMORY;
    }
    output_count =
        ((input_frame_count * DIO_VOICE_SAMPLE_RATE) +
         (format->sample_rate / 2u)) /
        format->sample_rate;

    if (output_count > SIZE_MAX / sizeof(*converted)) {
        free(mono);
        return DIO_AUDIO_OUT_OF_MEMORY;
    }
    converted = (int16_t *)malloc(output_count * sizeof(*converted));
    if (converted == NULL && output_count != 0u) {
        free(mono);
        return DIO_AUDIO_OUT_OF_MEMORY;
    }

    for (index = 0u; index < output_count; ++index) {
        const uint64_t numerator = (uint64_t)index * format->sample_rate;
        const size_t left = (size_t)(numerator / DIO_VOICE_SAMPLE_RATE);
        const uint32_t remainder =
            (uint32_t)(numerator % DIO_VOICE_SAMPLE_RATE);
        const size_t right =
            left + 1u < input_frame_count ? left + 1u : left;
        const int64_t weighted =
            ((int64_t)mono[left] *
             (DIO_VOICE_SAMPLE_RATE - remainder)) +
            ((int64_t)mono[right] * remainder);
        converted[index] =
            dio_clamp_i16((int)(weighted / DIO_VOICE_SAMPLE_RATE));
    }

    free(mono);
    output->samples = converted;
    output->sample_count = output_count;
    output->sample_rate = DIO_VOICE_SAMPLE_RATE;
    output->channel_count = 1u;
    return DIO_AUDIO_OK;
}

DioAudioResult dio_audio_read_wav_mono_16k(
    const wchar_t *path,
    DioAudioPcm *output,
    char *error_text,
    size_t error_text_capacity)
{
    FILE *file = NULL;
    unsigned char riff[12];
    DioWaveFormat format = {0};
    bool have_format = false;
    long data_offset = 0L;
    uint32_t data_size = 0u;
    int16_t *input = NULL;
    size_t input_frame_count;
    DioAudioResult result;

    if (path == NULL || output == NULL) {
        return DIO_AUDIO_INVALID_ARGUMENT;
    }
    (void)memset(output, 0, sizeof(*output));
    dio_audio_error(error_text, error_text_capacity, "");

    if (_wfopen_s(&file, path, L"rb") != 0 || file == NULL) {
        dio_audio_error(error_text, error_text_capacity, "could not open WAV");
        return DIO_AUDIO_IO_FAILURE;
    }

    if (fread(riff, 1u, sizeof(riff), file) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4u) != 0 ||
        memcmp(riff + 8u, "WAVE", 4u) != 0) {
        dio_audio_error(error_text, error_text_capacity, "invalid RIFF/WAVE header");
        result = DIO_AUDIO_UNSUPPORTED_FORMAT;
        goto cleanup;
    }

    for (;;) {
        unsigned char chunk[8];
        uint32_t chunk_size;

        if (fread(chunk, 1u, sizeof(chunk), file) != sizeof(chunk)) {
            break;
        }
        chunk_size = dio_read_u32_le(chunk + 4u);

        if (memcmp(chunk, "fmt ", 4u) == 0) {
            unsigned char base[16];
            if (chunk_size < sizeof(base) ||
                fread(base, 1u, sizeof(base), file) != sizeof(base)) {
                dio_audio_error(
                    error_text,
                    error_text_capacity,
                    "invalid WAV format chunk");
                result = DIO_AUDIO_UNSUPPORTED_FORMAT;
                goto cleanup;
            }
            format.format = dio_read_u16_le(base);
            format.channels = dio_read_u16_le(base + 2u);
            format.sample_rate = dio_read_u32_le(base + 4u);
            format.byte_rate = dio_read_u32_le(base + 8u);
            format.block_align = dio_read_u16_le(base + 12u);
            format.bits_per_sample = dio_read_u16_le(base + 14u);
            have_format = true;
            if (!dio_skip_bytes(file, chunk_size - (uint32_t)sizeof(base))) {
                result = DIO_AUDIO_IO_FAILURE;
                goto cleanup;
            }
        } else if (memcmp(chunk, "data", 4u) == 0) {
            data_offset = ftell(file);
            if (data_offset < 0L) {
                result = DIO_AUDIO_IO_FAILURE;
                goto cleanup;
            }
            data_size = chunk_size;
            if (!dio_skip_bytes(file, chunk_size)) {
                result = DIO_AUDIO_IO_FAILURE;
                goto cleanup;
            }
        } else if (!dio_skip_bytes(file, chunk_size)) {
            result = DIO_AUDIO_IO_FAILURE;
            goto cleanup;
        }

        if ((chunk_size & 1u) != 0u && fgetc(file) == EOF) {
            result = DIO_AUDIO_IO_FAILURE;
            goto cleanup;
        }
    }

    if (!have_format || data_offset == 0L || data_size == 0u ||
        format.format != 1u || format.bits_per_sample != 16u ||
        format.channels == 0u || format.sample_rate == 0u ||
        format.block_align == 0u ||
        (uint32_t)format.channels *
                (format.bits_per_sample / 8u) >
            UINT16_MAX ||
        format.block_align !=
            (uint16_t)((uint32_t)format.channels *
                       (format.bits_per_sample / 8u)) ||
        data_size % format.block_align != 0u) {
        dio_audio_error(
            error_text,
            error_text_capacity,
            "only uncompressed PCM16 WAV input is supported");
        result = DIO_AUDIO_UNSUPPORTED_FORMAT;
        goto cleanup;
    }

    input_frame_count = data_size / format.block_align;
    input = (int16_t *)malloc((size_t)data_size);
    if (input == NULL) {
        result = DIO_AUDIO_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (fseek(file, data_offset, SEEK_SET) != 0 ||
        fread(input, 1u, data_size, file) != data_size) {
        dio_audio_error(error_text, error_text_capacity, "could not read WAV samples");
        result = DIO_AUDIO_IO_FAILURE;
        goto cleanup;
    }

    result = dio_convert_pcm(input, input_frame_count, &format, output);
    if (result != DIO_AUDIO_OK) {
        dio_audio_error(error_text, error_text_capacity, "could not convert WAV");
    }

cleanup:
    free(input);
    if (file != NULL) {
        (void)fclose(file);
    }
    return result;
}

void dio_audio_pcm_free(DioAudioPcm *pcm)
{
    if (pcm == NULL) {
        return;
    }
    free(pcm->samples);
    (void)memset(pcm, 0, sizeof(*pcm));
}
