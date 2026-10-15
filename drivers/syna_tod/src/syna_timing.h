#pragma once
#include <glib.h>
/* Millisecond USB timeout, capped at 1s and rounded up; zero means expired. */
static inline guint syna_remaining_timeout(gint64 deadline, gint64 now)
{
    if (now >= deadline) return 0;
    return (guint)MIN((deadline - now + 999) / 1000, 1000);
}
