#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

static int errors;
static int on_error(Display *d, XErrorEvent *e) { (void)d; errors=e->error_code; return 0; }
static int64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000000000+t.tv_nsec; }
static void until(int64_t n) { struct timespec t={n/1000000000,n%1000000000}; while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL)==EINTR){} }
static int triangle(int n, int period) { int at=n%(2*period); return at<=period?at:2*period-at; }

int main(int argc,char **argv) {
    if(argc<4||argc>5)return 2;
    Window w=strtoul(argv[1],NULL,0); int duration=atoi(argv[2]),step=atoi(argv[3]);
    if(!w||duration<250||duration>15000||step<2||step>200)return 2;
    const char*pattern=argc==5?argv[4]:"aspect";
    if(strcmp(pattern,"aspect")&&strcmp(pattern,"mixed")&&strcmp(pattern,"original"))return 2;
    Display*d=XOpenDisplay(NULL);if(!d)return 3;XSetErrorHandler(on_error);
    int ev,err,major,minor;if(!XTestQueryExtension(d,&ev,&err,&major,&minor))return 4;
    XWindowAttributes original;if(!XGetWindowAttributes(d,w,&original)||original.map_state!=IsViewable)return 5;
    XRaiseWindow(d,w);XMoveWindow(d,w,200,150);XSync(d,False);until(now_ns()+100000000);
    Window child;int x,y;XTranslateCoordinates(d,w,DefaultRootWindow(d),0,0,&x,&y,&child);
    XWindowAttributes a;XGetWindowAttributes(d,w,&a);
    Atom type;int format;unsigned long n,left;unsigned char *data=NULL;
    unsigned long right=3,bottom=3;
    if(XGetWindowProperty(d,w,XInternAtom(d,"_NET_FRAME_EXTENTS",False),0,4,False,XA_CARDINAL,
        &type,&format,&n,&left,&data)==Success&&format==32&&n==4){
        unsigned long *ext=(unsigned long*)data;right=ext[1];bottom=ext[3];
    }
    if(data)XFree(data);
    const int sx=x+a.width+(int)right-1,sy=y+a.height+(int)bottom-1;
    XWindowAttributes root_attr;XGetWindowAttributes(d,DefaultRootWindow(d),&root_attr);
    XTestFakeMotionEvent(d,DefaultScreen(d),sx,sy,CurrentTime);
    XTestFakeButtonEvent(d,1,True,CurrentTime);XSync(d,False);
    XEvent event={0};event.xclient.type=ClientMessage;event.xclient.window=w;
    event.xclient.message_type=XInternAtom(d,"_NET_WM_MOVERESIZE",False);event.xclient.format=32;
    event.xclient.data.l[0]=sx;event.xclient.data.l[1]=sy;event.xclient.data.l[2]=4;
    event.xclient.data.l[3]=1;event.xclient.data.l[4]=2;
    XSendEvent(d,DefaultRootWindow(d),False,SubstructureRedirectMask|SubstructureNotifyMask,&event);
    XSync(d,False);until(now_ns()+80000000);
    Window qr,qc;int rx,ry,wx,wy;unsigned mask;
    XQueryPointer(d,DefaultRootWindow(d),&qr,&qc,&rx,&ry,&wx,&wy,&mask);
    printf("S39_WM_DRAG_BEGIN requested=%d,%d pointer=%d,%d mask=%u pattern=%s\n",sx,sy,rx,ry,mask,pattern);fflush(stdout);
    int changes=0,lastw=a.width,lasth=a.height,steps=0;
    int64_t start=now_ns(),stop=start+(int64_t)duration*1000000;
    while(now_ns()<stop&&!errors){
        int dx,dy;
        if(!strcmp(pattern,"original")){
            dx=4*triangle(steps,55);dy=3*triangle(steps,45);
        }else if(!strcmp(pattern,"aspect")){
            /* Grow AND shrink at a fixed aspect ratio. This keeps glxgears'
             * legitimate perspective cropping separate from stale framing. */
            int elapsed=(int)((now_ns()-start)/1000000);
            dx=triangle(elapsed+650,1300)*400/1300-200;
            dy=dx*original.height/original.width;
        }else{
            /* Different-axis, different-aspect corners; no pause between them. */
            static const int targets[][2]={{500,500},{900,700},{300,250},{800,350},{450,700},{1000,550},{500,500}};
            int elapsed=(int)((now_ns()-start)/1000000),leg=(elapsed/650)%6,at=elapsed%650;
            int width=targets[leg][0]+(targets[leg+1][0]-targets[leg][0])*at/650;
            int height=targets[leg][1]+(targets[leg+1][1]-targets[leg][1])*at/650;
            dx=width-original.width;dy=height-original.height;
        }
        int tx=sx+dx,ty=sy+dy;
        if(tx<8)tx=8;if(ty<8)ty=8;
        if(tx>=root_attr.width)tx=root_attr.width-1;
        if(ty>=root_attr.height)ty=root_attr.height-1;
        XTestFakeMotionEvent(d,DefaultScreen(d),tx,ty,CurrentTime);XSync(d,False);
        if(XGetWindowAttributes(d,w,&a)&&(a.width!=lastw||a.height!=lasth)){changes++;lastw=a.width;lasth=a.height;}
        steps++;int64_t next=start+(int64_t)steps*step*1000000;if(next>stop)next=stop;
        if(next>now_ns())until(next);
    }
    XTestFakeButtonEvent(d,1,False,CurrentTime);XSync(d,False);
    int64_t resize_end=now_ns();
    printf("S39_RESIZE_TIMING start_ms=%lld end_ms=%lld\n",
        (long long)(start/1000000),(long long)(resize_end/1000000));
    printf("S39_WM_DRAG steps=%d geometry_changes=%d final=%dx%d elapsed_ms=%lld xerror=%d origin=%d,%d border=%lu,%lu\n",
        steps,changes,lastw,lasth,(long long)((now_ns()-start)/1000000),errors,sx,sy,right,bottom);
    XResizeWindow(d,w,original.width,original.height);XSync(d,False);XCloseDisplay(d);
    return errors||changes<5?6:0;
}
