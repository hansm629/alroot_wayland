/* Test-owned, authenticated, disposable Xwayland ONLY. Sample Xfwm4's
 * genuinely composited XComposite overlay at the VACATED window location.
 * Unlike the older S28-S32 client-center probes, this catches persistent
 * colored gear trails outside the X11 application's current geometry.
 * X11 overlay correctness does NOT prove the Android display is correct.
 * The Python runner must refuse DISPLAY=:0 and the user's real XFCE. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void nap_ms(int ms) {
  struct timespec t = {.tv_sec=ms/1000,.tv_nsec=(ms%1000)*1000000L};
  nanosleep(&t,NULL);
}
static int red_green_blue(Display *d, Window w, int cx, int cy, int *nongray) {
  if(cx<65||cy<65)return -1;
  XImage *img=XGetImage(d,w,cx-64,cy-64,128,128,AllPlanes,ZPixmap);
  if(!img)return -1;
  int chroma=0,non=0;
  for(int y=3;y<128;y+=7)for(int x=3;x<128;x+=7){
    unsigned p=XGetPixel(img,x,y)&0xffffff;
    int r=p>>16&255,g=p>>8&255,b=p&255;
    if((r>g+38&&r>b+38&&r>90)||(g>r+35&&g>b+35&&g>90)||
       (b>r+35&&b>g+35&&b>90))chroma++;
    if(r<25||r>90||g<25||g>90||b<25||b>90)non++;
  }
  XDestroyImage(img);
  if(nongray)*nongray=non;
  return chroma;
}
static int current_center(Display*d,Window win,int *x,int *y){
  XWindowAttributes a;
  Window child=None;
  if(!XGetWindowAttributes(d,win,&a))return 0;
  return XTranslateCoordinates(d,win,DefaultRootWindow(d),a.width/2,a.height/2,x,y,&child);
}
int main(int argc,char**argv){
  if(argc!=3)return 2;
  Window win=strtoul(argv[1],NULL,0);
  int moves=atoi(argv[2]);
  if(!win||moves<4||moves>70)return 2;
  Display*d=XOpenDisplay(NULL);if(!d)return 3;
  Window root=DefaultRootWindow(d);
  int ev,er;
  if(!XCompositeQueryExtension(d,&ev,&er)){XCloseDisplay(d);return 4;}
  Window overlay=XCompositeGetOverlayWindow(d,root);
  if(!overlay){XCloseDisplay(d);return 4;}
  XMoveWindow(d,win,70,150);XSync(d,False);nap_ms(210);
  int oldx=0,oldy=0;current_center(d,win,&oldx,&oldy);
  int bad15=0,bad50=0,bad100=0,seen_after=0,observed=0;
  for(int i=0;i<moves;i++){
    XMoveWindow(d,win,(i%2)?70:1110,150);XSync(d,False);
    nap_ms(15);
    int curx=0,cury=0;if(!current_center(d,win,&curx,&cury))break;
    int empty=0;
    if(abs(curx-oldx)<600){nap_ms(80);continue;}
    int rgb15=red_green_blue(d,overlay,oldx,oldy,&empty);
    nap_ms(35);
    int rgb50=red_green_blue(d,overlay,oldx,oldy,&empty);
    nap_ms(50);
    int rgb100=red_green_blue(d,overlay,oldx,oldy,&empty);
    int newc=red_green_blue(d,overlay,curx,cury,&empty);
    observed++;
    if(rgb15>3)bad15++;
    if(rgb50>3)bad50++;
    if(rgb100>3)bad100++;
    if(newc>5)seen_after++;
    printf("S35_VACATED_REGION step=%d oldpos=%d,%d newpos=%d,%d RGB15=%d RGB50=%d RGB100=%d NEW=%d\n",
      i,oldx,oldy,curx,cury,rgb15,rgb50,rgb100,newc);
    oldx=curx;oldy=cury;
  }
  printf("S35_XCOMPOSITE_TRAIL_SUMMARY moves=%d traced=%d ghost_at_15ms=%d ghost_at_50ms=%d ghost_at_100ms=%d current_window_chroma=%d\n",
    moves,observed,bad15,bad50,bad100,seen_after);
  XCompositeReleaseOverlayWindow(d,root);
  XCloseDisplay(d);
  if(observed<4||seen_after<observed/2){
    puts("S35_XCOMPOSITE_AUDIT=INCONCLUSIVE_MISSING_MOVING_GEAR_CONTENT");
    return 5;
  }
  if(bad100){
    puts("S35_XCOMPOSITE_AUDIT=FAIL_GEAR_GHOST_PERSISTS_AFTER_100MS");
    return 6;
  }
  puts("S35_XCOMPOSITE_AUDIT=PASS_NO_LONG_LIVED_X11_OVERLAY_GHOST");
  puts("S35_ANDROID_PHYSICAL_DISPLAY=NOT_VERIFIED_BY_X11_PIXEL_READBACK");
  return 0;
}
