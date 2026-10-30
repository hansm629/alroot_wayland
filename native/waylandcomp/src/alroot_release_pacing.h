/* Alroot Wayland — bounded automatic buffer-release scheduling.
 *
 * On the phone's automatic refresh mode, clients can briefly produce more
 * buffers than the display can consume. The former formula increased the
 * maximum deadline by every pending buffer, so a 130 FPS producer on a
 * 120 Hz output could accumulate hundreds of milliseconds of latency.
 *
 * Keep explicit user-specified FPS limits unchanged, but never allow the
 * *automatic* compositor release deadline to drift past the next VSync.
 * This is invoked only after the old buffer is no longer in active use;
 * GPU-owned buffers follow their existing deferred-release/fence path.
 */
#ifndef ALROOT_RELEASE_PACING_H
#define ALROOT_RELEASE_PACING_H

#include <stdint.h>

static inline int64_t alroot_release_deadline_ns(int64_t now,
                                                  int64_t previous,
                                                  int64_t interval,
                                                  unsigned pending,
                                                  int automatic,
                                                  int *capped)
{
    if (capped) *capped = 0;
    if (interval <= 0) return now;
    int64_t candidate = previous > 0 ? previous + interval : now;
    if (candidate < now) candidate = now;

    /* Preserve the legacy explicit FPS cap, including its queued pacing.
     * Only the automatic physical-refresh lane receives the latency bound.
     */
    int64_t max_ahead = interval;
    if (!automatic && pending < 1000000u)
        max_ahead = interval * (int64_t)(pending + 1u);
    int64_t latest = now + max_ahead;
    if (candidate > latest) {
        candidate = latest;
        if (capped) *capped = 1;
    }
    return candidate;
}

#endif
