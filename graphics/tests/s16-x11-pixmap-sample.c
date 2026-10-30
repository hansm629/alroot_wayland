/* Diagnostic: tiny XGetImage samples from an existing Xwayland client window.
 * Read-only. Does not write images to disk or capture personal window content. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
static int errors;
static int xerr(Display *d,XErrorEvent*e){char buf[150];XGetErrorText(d,e->error_code,buf,sizeof(buf));fprintf(stderr,"S16_X11_XIMAGE_ERROR=%d %s\n",e->error_code,buf);errors++;return 0;}
int main(int argc,char**argv){
 if(argc!=2){puts("usage: x11-pixmap-sample 0xXID");return 2;}
 errno=0;unsigned long win=strtoul(argv[1],0,0);if(errno || !win)return 2;
 XSetErrorHandler(xerr);
 Display*d=XOpenDisplay(NULL);if(!d){puts("S16_X11_GETIMAGE_NO_DISPLAY");return 2;}
 XWindowAttributes a;if(!XGetWindowAttributes(d,win,&a)){puts("S16_X11_GETIMAGE_NO_WINDOW");XCloseDisplay(d);return 3;}
 printf("S16_X11_GETIMAGE_WINDOW=0x%lx mapped:%d width:%d height:%d depth:%d\n",win,a.map_state,a.width,a.height,a.depth);
 if(a.width<2 || a.height<2 || a.map_state!=IsViewable)return 4;
 int x[]={a.width/8,a.width/2,7*a.width/8,a.width/2,a.width/3};
 int y[]={a.height/8,a.height/2,7*a.height/8,a.height/8,2*a.height/3};
 unsigned long colors[5]={0};int success=0;
 for(int i=0;i<5;i++){
   XImage*im=XGetImage(d,win,x[i],y[i],1,1,AllPlanes,ZPixmap);
   if(im){colors[i]=XGetPixel(im,0,0);success++;XDestroyImage(im);}
   printf("S16_X11_GETIMAGE_SAMPLE_%d=0x%lx pos:%d,%d %s\n",i,colors[i],x[i],y[i],im?"PASS":"FAIL");
 }
 XSync(d,False);
 printf("S16_X11_GETIMAGE_READBACK=%d/5 errors:%d\n",success,errors);
 XCloseDisplay(d);
 return success==5 && errors==0 ? 0 : 5;
}
