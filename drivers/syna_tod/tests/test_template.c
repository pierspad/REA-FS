#include <glib.h>
#include <math.h>
#include "syna_template.h"

int main(void)
{
    float sample[SYNA_TEMPLATE_PIXELS] = {0};
    sample[0] = 1.25f;
    GPtrArray *samples = g_ptr_array_new();
    g_ptr_array_add(samples, sample);
    GVariant *encoded = g_variant_ref_sink(syna_template_encode(samples));
    GPtrArray *decoded = syna_template_decode(encoded);
    g_assert_nonnull(decoded);
    g_assert_cmpuint(decoded->len, ==, 1);
    g_assert_cmpfloat(((float *)decoded->pdata[0])[0], ==, 1.25f);
    g_ptr_array_unref(decoded);
    g_variant_unref(encoded);

    GVariant *short_data = g_variant_ref_sink(g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, "x", 1, 1));
    g_assert_null(syna_template_decode(short_data));
    g_variant_unref(short_data);
    sample[0] = NAN;
    GVariant *bad = g_variant_ref_sink(g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, sample, sizeof(sample), 1));
    g_assert_null(syna_template_decode(bad));
    g_variant_unref(bad);
    sample[0] = 2.5f;
    GVariant *legacy = g_variant_ref_sink(g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, sample, sizeof(sample), 1));
    decoded = syna_template_decode(legacy);
    g_assert_nonnull(decoded);
    g_assert_cmpfloat(((float *)decoded->pdata[0])[0], ==, 2.5f);
    g_ptr_array_unref(decoded);
    g_variant_unref(legacy);
    g_ptr_array_unref(samples);
    return 0;
}
