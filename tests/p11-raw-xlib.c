#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
int main(int argc,char **argv) {
  int ascii_only = argc > 1 && strcmp(argv[1],"ascii")==0;
  Display *d=XOpenDisplay(NULL);if(!d) return 2;
  int screen=DefaultScreen(d);
  Window w=XCreateSimpleWindow(d,RootWindow(d,screen),72,72,360,80,0,0,0);
  XStoreName(d,w,"P11 raw Xlib without Fcitx frontend");
  XSelectInput(d,w,KeyPressMask|KeyReleaseMask|StructureNotifyMask);
  XMapRaised(d,w);
  XFlush(d);
  usleep(220000);
  XSetInputFocus(d,w,RevertToParent,CurrentTime);
  XFlush(d);
  int hangul=0,backspace=0,latin=0;
  time_t deadline=time(NULL)+3;
  while(time(NULL)<deadline) {
      while(XPending(d)) {
          XEvent e;XNextEvent(d,&e);
          if(e.type==KeyPress) {
              KeySym s=XLookupKeysym(&e.xkey,0);
              if(s>=0x01001100 && s<=0x0110ffff)hangul++;
              if(s==XK_BackSpace)backspace++;
              if(s==XK_h || s==XK_i)latin++;
          }
      }
      usleep(10000);
  }
  int good = ascii_only ? latin==2 && hangul==0 && backspace==0
                        : hangul>=2 && backspace>=1;
  printf("P11_RAW_XTEST_FALLBACK hangul_keypresses=%d backspace=%d ascii_keys=%d success=%d\n",
      hangul,backspace,latin,good);
  fflush(stdout);
  XDestroyWindow(d,w);
  XCloseDisplay(d);
  return good ? 0 : 1;
}
