#pragma once
#include <glib.h>
#include "syna_proto.h"
#define SYNA_TEMPLATE_PIXELS (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES)
#define SYNA_ENROLL_STAGES 8
#define SYNA_MAX_TEMPLATE_SAMPLES 16
/* Version 1: (uint32 version, array of arrays of doubles). Legacy: native float bytes. */
GVariant *syna_template_encode(GPtrArray *samples);
GPtrArray *syna_template_decode(GVariant *data);
