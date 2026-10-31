/* Real X11 Present regression: a newer, size-mismatched client frame must
 * not be replaced by an older resize snapshot on the next Configure.
 * The generation is an exact solid RGB value, not a geometry-dependent
 * image hash. Only this process's own window is read or resized. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xpresent.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int errors;
static int on_error(Display *d, XErrorEvent *e)
{ (void)d; (void)e; ++errors; return 0; }
static int64_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pause_ms(long ms)
{ struct timespec t={ms/1000,(ms%1000)*1000000}; nanosleep(&t,NULL); }
static int present(Display *d, Window w, Pixmap p, uint32_t serial, int opcode)
{
    XPresentPixmap(d,w,p,serial,None,None,0,0,None,None,None,
                   PresentOptionCopy,0,0,0,NULL,0);
    XFlush(d);
    int64_t stop=now_ms()+2000;
    while(now_ms()<stop && !errors) {
        while(XPending(d)) {
            XEvent event; XNextEvent(d,&event);
            if(event.type==GenericEvent && event.xcookie.extension==opcode &&
               XGetEventData(d,&event.xcookie)) {
                int done=0;
                if(event.xcookie.evtype==PresentCompleteNotify) {
                    XPresentCompleteNotifyEvent *e=event.xcookie.data;
                    done=e->window==w && e->serial_number==serial &&
                         e->kind==PresentCompleteKindPixmap;
                }
                XFreeEventData(d,&event.xcookie);
                if(done) return 1;
            }
        }
        pause_ms(2);
    }
    return 0;
}
static unsigned long pixel(Display *d,Window w)
{
    XImage *i=XGetImage(d,w,20,20,1,1,AllPlanes,ZPixmap);
    if(!i) return ~0UL;
    unsigned long value=XGetPixel(i,0,0)&0xffffff;
    XDestroyImage(i); return value;
}
static int resize_window(Display *d,Window w,unsigned width,unsigned height)
{
    XResizeWindow(d,w,width,height); XFlush(d);
    int64_t stop=now_ms()+1500;
    while(now_ms()<stop && !errors) {
        XWindowAttributes a;
        if(XGetWindowAttributes(d,w,&a) &&
           a.width==(int)width && a.height==(int)height) return 1;
        pause_ms(2);
    }
    return 0;
}
int main(void)
{
    XSetErrorHandler(on_error);
    Display *d=XOpenDisplay(NULL); if(!d)return 2;
    int opcode,event_base,error_base;
    if(!XPresentQueryExtension(d,&opcode,&event_base,&error_base))return 2;
    int s=DefaultScreen(d);
    Window w=XCreateSimpleWindow(d,RootWindow(d,s),80,80,400,400,0,0,0);
    const char tag[]="s38-present-generation-test";
    XChangeProperty(d,w,XInternAtom(d,"_MESA_DRV",False),XA_STRING,8,
                    PropModeReplace,(const unsigned char *)tag,sizeof(tag)-1);
    XStoreName(d,w,"Alroot isolated Present generation test");
    XID eid=XPresentSelectInput(d,w,PresentCompleteNotifyMask);
    XMapWindow(d,w); XSync(d,False); pause_ms(60);
    Pixmap p[2]; const unsigned long color[2]={0x2040e0,0x20e040};
    for(int n=0;n<2;n++) {
        p[n]=XCreatePixmap(d,w,400+n*20,400+n*20,DefaultDepth(d,s));
        GC gc=XCreateGC(d,p[n],0,NULL); XSetForeground(d,gc,color[n]);
        XFillRectangle(d,p[n],gc,0,0,400+n*20,400+n*20); XFreeGC(d,gc);
    }
    int ok=present(d,w,p[0],1,opcode);
    unsigned long initial=pixel(d,w);
    ok=ok && resize_window(d,w,500,500);
    ok=ok && present(d,w,p[1],2,opcode);
    unsigned long newer=pixel(d,w);
    ok=ok && resize_window(d,w,560,520);
    unsigned long after_resize=pixel(d,w);
    printf("S38_PRESENT_GENERATION initial=%06lx newer=%06lx after_resize=%06lx expected=%06lx errors=%d\n",
            initial,newer,after_resize,color[1],errors);
    ok=ok && initial==color[0] && newer==color[1] && after_resize==color[1] && !errors;
    XPresentFreeInput(d,w,eid);
    for(int n=0;n<2;n++)XFreePixmap(d,p[n]);
    XDestroyWindow(d,w); XSync(d,False); XCloseDisplay(d);
    puts(ok?"S38_LATEST_PRESENT_SURVIVES_RESIZE=PASS":"S38_LATEST_PRESENT_SURVIVES_RESIZE=FAIL");
    return ok?0:1;
}
