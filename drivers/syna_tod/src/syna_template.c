#include "syna_template.h"
#include <math.h>
#include <string.h>

GVariant *syna_template_encode(GPtrArray *samples)
{
    GVariantBuilder outer;
    g_variant_builder_init(&outer, G_VARIANT_TYPE("aad"));
    for (guint s = 0; s < samples->len; s++) {
        GVariantBuilder inner;
        g_variant_builder_init(&inner, G_VARIANT_TYPE("ad"));
        const float *pixels = samples->pdata[s];
        for (guint i = 0; i < SYNA_TEMPLATE_PIXELS; i++)
            g_variant_builder_add(&inner, "d", (double)pixels[i]);
        g_variant_builder_add_value(&outer, g_variant_builder_end(&inner));
    }
    return g_variant_new("(u@aad)", 1u, g_variant_builder_end(&outer));
}

GPtrArray *syna_template_decode(GVariant *data)
{
    if (!data || !g_variant_is_normal_form(data)) return NULL;
    GPtrArray *samples = g_ptr_array_new_with_free_func(g_free);
    if (g_variant_is_of_type(data, G_VARIANT_TYPE("ay"))) {
        gsize size = 0;
        const void *bytes = g_variant_get_fixed_array(data, &size, 1);
        if (size != SYNA_TEMPLATE_PIXELS * sizeof(float)) goto invalid;
        float *pixels = g_memdup2(bytes, size);
        g_ptr_array_add(samples, pixels);
    } else if (g_variant_is_of_type(data, G_VARIANT_TYPE("(uaad)"))) {
        guint version;
        GVariant *arrays;
        g_variant_get(data, "(u@aad)", &version, &arrays);
        gsize count = g_variant_n_children(arrays);
        if (version != 1 || count < 1 || count > SYNA_ENROLL_STAGES) {
            g_variant_unref(arrays);
            goto invalid;
        }
        for (gsize s = 0; s < count; s++) {
            GVariant *row = g_variant_get_child_value(arrays, s);
            gsize n;
            const double *values = g_variant_get_fixed_array(row, &n, sizeof(double));
            if (n != SYNA_TEMPLATE_PIXELS) {
                g_variant_unref(row);
                g_variant_unref(arrays);
                goto invalid;
            }
            float *pixels = g_new(float, SYNA_TEMPLATE_PIXELS);
            for (gsize i = 0; i < n; i++) pixels[i] = (float)values[i];
            g_ptr_array_add(samples, pixels);
            g_variant_unref(row);
        }
        g_variant_unref(arrays);
    } else goto invalid;
    for (guint s = 0; s < samples->len; s++) {
        const float *pixels = samples->pdata[s];
        for (guint i = 0; i < SYNA_TEMPLATE_PIXELS; i++)
            if (!isfinite(pixels[i]) || fabsf(pixels[i]) > 1e6f) goto invalid;
    }
    return samples;
invalid:
    g_ptr_array_unref(samples);
    return NULL;
}
