/* Private test-only readback of our own mapped vkcube window.
 * Measures whether a colored, rotating cube remains visible DURING
 * XCB resize vs substituted flat background. No input/other window reads. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static long long now_ms(void){struct timespec s;clock_gettime(CLOCK_MONOTONIC,&s);return (long long)s.tv_sec*1000+s.tv_nsec/1000000;}
int main(int argc,char**argv){
 if(argc!=3)return 2;
 unsigned long id=strtoul(argv[1],0,0);int seconds=atoi(argv[2]);
 if(!id||seconds<2||seconds>45)return 2;
 Display*d=XOpenDisplay(0);if(!d)return 3;
 int frames=0,colored=0,blank=0,black=0,changed=0,geometry=0,invalid=0,maxblank=0,run=0;
 unsigned long long last=0;int lastw=0,lasth=0;
 long long stop=now_ms()+seconds*1000;
 while(now_ms()<stop){
  XWindowAttributes a;
  if(!XGetWindowAttributes(d,id,&a)||a.width<50||a.height<50||a.map_state!=IsViewable){invalid++;goto next;}
  if(a.width!=lastw||a.height!=lasth){geometry++;lastw=a.width;lasth=a.height;}
  int w=a.width/3,h=a.height/3;
  XImage*im=XGetImage(d,id,a.width/2-w/2,a.height/2-h/2,w,h,AllPlanes,ZPixmap);
  if(!im){invalid++;goto next;}
  unsigned long long hash=1469598103934665603ULL;int nongrey=0,pureblack=0;
  for(int y=0;y<15;y++)for(int x=0;x<15;x++){
   unsigned long p=XGetPixel(im,(w-1)*x/14,(h-1)*y/14)&0xffffff;
   int r=p>>16&255,g=p>>8&255,b=p&255;
   if(r<35||r>75||g<35||g>75||b<35||b>75)nongrey++;
   if(p==0)pureblack++;
   hash^=p;hash*=1099511628211ULL;
  }
  XDestroyImage(im);
  if(frames&&hash!=last)changed++;
  last=hash;frames++;
  if(nongrey>=8){colored++;run=0;}else{blank++;run++;if(run>maxblank)maxblank=run;}
  if(pureblack>8)black++;
next:;
  struct timespec pause={.tv_sec=0,.tv_nsec=40000000};nanosleep(&pause,0);
 }
 printf("S31_MOTION frames=%d geometry_changes=%d drawn=%d blank=%d black=%d hash_changes=%d max_blank_samples=%d invalid=%d\n",frames,geometry,colored,blank,black,changed,maxblank,invalid);
 XCloseDisplay(d);return 0;
}
