#!/data/data/com.termux/files/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Verify S40 glxgears event batching with the actual source event loops.

This deterministic event-queue regression checks viewport selection, event
backlog, Escape, pause/unpause, and arrow input. It does not simulate GLX,
Vulkan, or displayed pixels. Temporary compiler outputs are removed on exit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / '.source/gpu-stage1/s40-demo-events/source'
EXPECTED = {
    'baseline': '344a03aff01708350d90603fd6b841bccd295157670f519b459bbf3874acf847',
    'coalesced': 'd4ca3fe86499fc4618dcba03332351d99e35bcee281c6dac60a0904f0f6ac3a3',
}

PREFIX = r'''
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
typedef int Display;
typedef unsigned long Window;
typedef int GLboolean;
#define GL_FALSE 0
#define GL_TRUE 1
#define NOP 0
#define EXIT 1
#define DRAW 2
enum {ConfigureNotify=22, Expose=12, KeyPress=2, Other=99};
typedef struct { int type; struct { int width, height; } xconfigure; int key; } XEvent;
static GLboolean animate;
static XEvent queue[32];
static int begin, end, width, height, reshapes, draws, views, wait_calls;
static jmp_buf escaped;
static void push(int type,int w,int h,int key) {
    queue[end++] = (XEvent){.type=type,.xconfigure={w,h},.key=key};
}
static int XPending(Display *dpy) { (void)dpy; return end-begin; }
static void XNextEvent(Display *dpy,XEvent *event) {
    (void)dpy;
    if (begin == end) { wait_calls++; longjmp(escaped,2); }
    *event=queue[begin++];
}
static void reshape(int w,int h) { width=w; height=h; reshapes++; }
static int handle_event(Display *dpy,Window win,XEvent *event) {
    (void)dpy;(void)win;
    switch (event->type) {
    case Expose:return DRAW;
    case ConfigureNotify:reshape(event->xconfigure.width,event->xconfigure.height);return NOP;
    case KeyPress:
        if(event->key==27)return EXIT;
        if(event->key=='a')animate=!animate;
        if(event->key=='l')views++;
        return DRAW;
    default:return NOP;
    }
}
static void draw_frame(Display *dpy,Window win) { (void)dpy;(void)win;draws++;longjmp(escaped,1); }
'''
SUFFIX = r'''
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    animate=GL_TRUE;width=height=300;
    if(!strcmp(argv[1],"burst")) {
        push(ConfigureNotify,400,400,0);push(Expose,0,0,0);
        push(ConfigureNotify,100,100,0);push(Expose,0,0,0);
        push(ConfigureNotify,300,300,0);push(Expose,0,0,0);
    } else if(!strcmp(argv[1],"escape")) {
        push(ConfigureNotify,400,400,0);push(Expose,0,0,0);
        push(ConfigureNotify,300,300,0);push(KeyPress,0,0,27);
    } else if(!strcmp(argv[1],"paused_resize")) {
        animate=GL_FALSE;push(ConfigureNotify,420,450,0);push(Expose,0,0,0);
    } else if(!strcmp(argv[1],"paused_idle")) {
        animate=GL_FALSE;push(Other,0,0,0);
    } else if(!strcmp(argv[1],"pause")) {
        push(KeyPress,0,0,'a');
    } else if(!strcmp(argv[1],"unpause_and_arrows")) {
        animate=GL_FALSE;push(KeyPress,0,0,'a');push(KeyPress,0,0,'l');
        push(ConfigureNotify,450,460,0);push(KeyPress,0,0,'l');push(Expose,0,0,0);
    } else return 3;
    int jump=setjmp(escaped);
    if(!jump)event_loop(NULL,0);
    printf("{\"width\":%d,\"height\":%d,\"remaining\":%d,\"draws\":%d,"
           "\"reshapes\":%d,\"views\":%d,\"waits\":%d,\"animate\":%d}\n",
           width,height,end-begin,draws,reshapes,views,wait_calls,animate);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, default=SOURCE / 'glxgears.baseline.c')
    parser.add_argument('--candidate', type=Path, default=SOURCE / 'glxgears.coalesced.c')
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc') or 'cc')
    args = parser.parse_args()
    inputs = {'baseline': args.baseline, 'coalesced': args.candidate}
    results, identities = {}, {}
    with tempfile.TemporaryDirectory(prefix='s40-glx-events-') as temporary:
        work = Path(temporary)
        for variant, path in inputs.items():
            source = path.read_text()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != EXPECTED[variant]:
                raise RuntimeError('pinned source mismatch: ' + str(path))
            begin = source.index('static void\nevent_loop(Display *dpy, Window win)')
            end = source.index('\n\n\nstatic void\nusage(void)', begin)
            loop = source[begin:end]
            target = work / f'event-check-{variant}.c'
            target.write_text(PREFIX + loop + SUFFIX)
            executable = target.with_suffix('')
            subprocess.run([args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(target), '-o', str(executable)], check=True, timeout=30)
            identities[variant] = {'path': str(path.resolve()), 'sha256': digest,
                                   'loop_sha256': hashlib.sha256(loop.encode()).hexdigest()}
            results[variant] = {
                case: json.loads(subprocess.check_output([str(executable), case],
                                                        text=True, timeout=3))
                for case in ('burst', 'escape', 'paused_resize', 'paused_idle',
                             'pause', 'unpause_and_arrows')
            }
    baseline, candidate = results['baseline'], results['coalesced']
    checks = {
        'baseline_expose_leaves_stale_viewport_and_backlog':
            baseline['burst']['width'] == 400 and baseline['burst']['remaining'] == 4,
        'candidate_latest_viewport_no_backlog_one_reshape':
            candidate['burst']['width'] == 300 and candidate['burst']['remaining'] == 0
            and candidate['burst']['reshapes'] == 1,
        'escape_before_additional_draw':
            candidate['escape']['draws'] == 0 and candidate['escape']['remaining'] == 0,
        'paused_resize_redraw':
            candidate['paused_resize']['width'] == 420
            and candidate['paused_resize']['height'] == 450
            and candidate['paused_resize']['draws'] == 1
            and not candidate['paused_resize']['animate'],
        'paused_idle_blocks_without_draw':
            candidate['paused_idle']['draws'] == 0 and candidate['paused_idle']['waits'] == 1,
        'pause_key_preserved':
            candidate['pause']['draws'] == 1 and not candidate['pause']['animate'],
        'unpause_arrow_input_and_latest_resize_preserved':
            candidate['unpause_and_arrows']['views'] == 2
            and bool(candidate['unpause_and_arrows']['animate'])
            and candidate['unpause_and_arrows']['width'] == 450
            and candidate['unpause_and_arrows']['remaining'] == 0,
    }
    result = {'result': 'PASS' if all(checks.values()) else 'FAIL',
              'scope': 'deterministic_event_loop_only_not_graphics',
              'sources': identities, 'checks': checks, 'cases': results}
    print(json.dumps(result, sort_keys=True))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({'result': 'ERROR', 'error': str(error)}))
        raise SystemExit(2)
