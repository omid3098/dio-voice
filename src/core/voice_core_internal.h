#ifndef DIO_VOICE_VOICE_CORE_INTERNAL_H
#define DIO_VOICE_VOICE_CORE_INTERNAL_H

#include <stdbool.h>

#include "dio_voice/voice_core.h"

bool dio_voice_vad_update(
    float probability,
    float threshold,
    float hysteresis,
    bool *active);

void dio_voice_test_latch_failure(
    DioVoiceCore *voice);

void dio_voice_test_mark_recognition_speaking(
    DioVoiceCore *voice);

#endif
