/* Repro for S25 Wayland compositor release-timer backlog.
 * Source test includes the exact helper used by native/compositor.c.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "alroot_release_pacing.h"

static int64_t simulate(unsigned producer_fps, unsigned display_fps,
                        int automatic, unsigned frames, unsigned *caps)
{
    const int64_t interval = 1000000000LL / display_fps;
    int64_t previous = 0;
    int64_t max_ahead = 0;
    unsigned pending = 0;
    int64_t release_times[4096] = {0};
    assert(frames <= 4096);
    for (unsigned i=0; i<frames; i++) {
        const int64_t now = (int64_t)i * 1000000000LL / producer_fps;
        for (unsigned j=0; j<i; j++) {
            if (release_times[j] >= 0 && release_times[j] <= now) {
                --pending;
                release_times[j] = -1;
            }
        }
        int capped=0;
        int64_t at=alroot_release_deadline_ns(now, previous, interval,
                                               pending, automatic, &capped);
        if (capped && caps) ++*caps;
        assert(at >= now);
        int64_t ahead=at-now;
        if (ahead > max_ahead) max_ahead=ahead;
        previous=at;
        release_times[i]=at;
        pending++;
    }
    return max_ahead;
}

int main(void)
{
    unsigned caps=0;
    int64_t high=simulate(130,120,1,1300,&caps);
    printf("S25_AUTO_130FPS_ON_120HZ_MAX_DEADLINE=%.3fms caps=%u\n",
           high/1000000.,caps);
    assert(high <= 1000000000LL / 120);
    assert(caps > 15);

    caps=0;
    high=simulate(150,60,1,1500,&caps);
    printf("S25_AUTO_150FPS_ON_60HZ_MAX_DEADLINE=%.3fms caps=%u\n",
           high/1000000.,caps);
    assert(high <= 1000000000LL / 60);
    assert(caps > 100);

    caps=0;
    high=simulate(30,120,1,300,&caps);
    assert(high <= 1000000000LL / 120);
    assert(caps == 0);

    caps=0;
    high=simulate(130,30,0,1300,&caps);
    assert(caps == 0);
    printf("S25_EXPLICIT_30FPS_130FPS_PRODUCER_MAX_DEADLINE=%.3fms\n",
           high/1000000.);
    assert(high > 100000000LL); /* old explicit FPS semantics retained */

    int capped=0;
    assert(alroot_release_deadline_ns(100,0,0,0,1,&capped)==100);
    puts("S25_RELEASE_DEADLINE_BOUNDED_AUTOMATIC=PASS");
    puts("S25_EXPLICIT_FPS_QUEUE_SEMANTICS=UNCHANGED");
    return 0;
}
