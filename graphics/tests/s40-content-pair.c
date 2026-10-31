/* SPDX-License-Identifier: MIT
 * Diagnostic readback of an owned client on an isolated compositing X server.
 * Name the WM frame's redirected pixmap on every sample; the named reference
 * keeps that allocation and its extent valid while resizing replaces it.
 * Capture the compositor overlay separately. These are NOT same-frame pairs.
 * No motion pass/fail is inferred from changing geometry or image hashes.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#define SAMPLE_MS 400
#define MAX_SIDE 320u
#define OUTPUT_LIMIT (24u * 1024u * 1024u)
static int xerror;
static int on_error(Display *d, XErrorEvent *e) { (void)d; xerror=e->error_code; return 0; }
static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void until(int64_t at) { struct timespec t={at/1000,(at%1000)*1000000}; while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL)==EINTR){} }
static unsigned char component(unsigned long p,unsigned long mask) {
    if(!mask)return 0;
    unsigned shift=0;while(!(mask&1)){mask>>=1;shift++;}
    return (unsigned char)((((p>>shift)&mask)*255+mask/2)/mask);
}
static int use_visual_masks(XImage *image,const XWindowAttributes *attributes) {
    /* XGetImage on a Pixmap has no Visual and Xlib leaves its RGB masks zero.
     * A named backing pixmap uses the immutable Visual/depth of its window.
     * Do not silently encode zero RGB or guess masks from host byte order. */
    if(!image||!attributes->visual||attributes->visual->class!=TrueColor||
       image->depth!=attributes->depth)return 0;
    image->red_mask=attributes->visual->red_mask;
    image->green_mask=attributes->visual->green_mask;
    image->blue_mask=attributes->visual->blue_mask;
    return image->red_mask&&image->green_mask&&image->blue_mask;
}
static int save_ppm(const char *path,XImage *im,size_t *bytes) {
    unsigned w=(unsigned)im->width,h=(unsigned)im->height;
    unsigned longest=w>h?w:h;
    if(longest>MAX_SIDE){w=w*MAX_SIDE/longest;h=h*MAX_SIDE/longest;}
    if(!w)w=1;
    if(!h)h=1;
    FILE*f=fopen(path,"wb");if(!f)return 0;
    int n=fprintf(f,"P6\n%u %u\n255\n",w,h);if(n<0){fclose(f);return 0;}
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        int sx=(int)((uint64_t)(2*x+1)*(unsigned)im->width/(2*w));
        int sy=(int)((uint64_t)(2*y+1)*(unsigned)im->height/(2*h));
        unsigned long p=XGetPixel(im,sx,sy);
        unsigned char rgb[3]={component(p,im->red_mask),component(p,im->green_mask),component(p,im->blue_mask)};
        if(fwrite(rgb,1,3,f)!=3){fclose(f);return 0;}
    }
    *bytes+=(size_t)n+(size_t)w*h*3;return fclose(f)==0;
}
static Window root_child(Display*d,Window w,Window root) {
    for(unsigned depth=0;depth<32;depth++){
        Window found_root,parent,*children=NULL;unsigned n;
        if(!XQueryTree(d,w,&found_root,&parent,&children,&n))return None;
        if(children)XFree(children);
        if(parent==root)return w;
        if(parent==None||parent==w)return None;
        w=parent;
    }
    return None;
}
static void color_metrics(XImage*im,const unsigned long ext[4],unsigned index) {
    int l=(int)ext[0],r=im->width-(int)ext[1],t=(int)ext[2],b=im->height-(int)ext[3];
    if(l<0||t<0||r<=l||b<=t||r>im->width||b>im->height)return;
    unsigned red=0,green=0,blue=0,black=0;int minblue=INT_MAX,maxblue=-1;
    /* Fixed stride is a color-visibility diagnostic, never a rotation metric. */
    for(int y=t;y<b;y+=3)for(int x=l;x<r;x+=3){
        unsigned long p=XGetPixel(im,x,y);
        unsigned rr=component(p,im->red_mask),gg=component(p,im->green_mask),bb=component(p,im->blue_mask);
        if(rr>40&&rr>gg*3/2&&rr>bb*3/2)red++;
        if(gg>40&&gg>rr*3/2&&gg>bb*3/2)green++;
        if(bb>40&&bb>rr*3/2&&bb>gg*3/2){blue++;if(y-t<minblue)minblue=y-t;if(y-t>maxblue)maxblue=y-t;}
        if(rr<10&&gg<10&&bb<10)black++;
    }
    printf("S40_CONTENT_COLOR index=%u client_extent=%dx%d red=%u green=%u blue=%u black=%u blue_min_y=%d blue_max_y=%d interpretation=DIAGNOSTIC_ONLY\n",
           index,r-l,b-t,red,green,blue,black,minblue==INT_MAX?-1:minblue,maxblue);
}
int main(int argc,char**argv) {
    if(argc<3||argc>4)return 2;
    char*end;Window window=strtoul(argv[1],&end,0);if(!window||*end)return 2;
    double seconds=argc==4?strtod(argv[3],&end):17.0;
    if((argc==4&&*end)||!(seconds>0&&seconds<=20))return 2;
    struct stat st;if(stat(argv[2],&st)||!S_ISDIR(st.st_mode))return 2;
    Display*d=XOpenDisplay(NULL);if(!d)return 3;XSetErrorHandler(on_error);
    int ev,err;if(!XCompositeQueryExtension(d,&ev,&err)){XCloseDisplay(d);return 3;}
    Window root=DefaultRootWindow(d),frame=root_child(d,window,root),child;
    XWindowAttributes root_a,frame_a,overlay_a;
    if(!frame||!XGetWindowAttributes(d,root,&root_a)||!XGetWindowAttributes(d,frame,&frame_a)){XCloseDisplay(d);return 3;}
    Window overlay=XCompositeGetOverlayWindow(d,root);XSync(d,False);
    if(xerror||!XGetWindowAttributes(d,overlay,&overlay_a)){XCloseDisplay(d);return 3;}
    unsigned long ext[4]={0,0,0,0},n,left;unsigned char*data=NULL;Atom type;int format;
    if(XGetWindowProperty(d,window,XInternAtom(d,"_NET_FRAME_EXTENTS",False),0,4,False,XA_CARDINAL,
        &type,&format,&n,&left,&data)==Success&&format==32&&n==4){
        for(unsigned j=0;j<4;j++)ext[j]=((unsigned long*)data)[j];
    }
    if(data)XFree(data);
    const int64_t start=now_ms(),finish=start+(int64_t)(seconds*1000);
    unsigned attempts=0,saved=0,failed=0;size_t bytes=0;
    printf("S40_CONTENT_READY xid=0x%lx frame=0x%lx overlay=0x%lx start_monotonic_ms=%"PRId64" period_ms=%d extents=%lu,%lu,%lu,%lu frame_depth=%d frame_visual=0x%lx rgb_masks=0x%lx,0x%lx,0x%lx cpu_readback=DIAGNOSTIC same_frame=NOT_GUARANTEED\n",
           window,frame,overlay,start,SAMPLE_MS,ext[0],ext[1],ext[2],ext[3],
           frame_a.depth,frame_a.visual->visualid,frame_a.visual->red_mask,frame_a.visual->green_mask,frame_a.visual->blue_mask);fflush(stdout);
    while(now_ms()<finish){
        unsigned index=attempts++;int64_t sample_start=now_ms();
        Pixmap pix=None;XImage*backing=NULL,*composite=NULL;
        Window gr;int gx,gy,root_x=0,root_y=0;unsigned w=0,h=0,border,depth;
        XWindowAttributes before={0},after={0};xerror=0;
        if(!XGetWindowAttributes(d,window,&before)||before.map_state!=IsViewable||
           !XTranslateCoordinates(d,frame,root,0,0,&root_x,&root_y,&child))goto bad;
        pix=XCompositeNameWindowPixmap(d,frame);XSync(d,False);
        if(xerror){pix=None;goto bad;}
        if(!pix||!XGetGeometry(d,pix,&gr,&gx,&gy,&w,&h,&border,&depth)||!w||!h)goto bad;
        backing=XGetImage(d,pix,0,0,w,h,AllPlanes,ZPixmap);
        int64_t backing_ms=now_ms();
        if(!backing||xerror||!use_visual_masks(backing,&frame_a))goto bad;
        /* The overlay has the fixed root extent; concurrent child resizing
         * cannot invalidate this checked rectangle. A geometry mismatch is
         * retained in metadata instead of discarding the whole sample. */
        int l=root_x<0?0:root_x,t=root_y<0?0:root_y;
        int r=root_x+(int)w,b=root_y+(int)h;
        if(r>root_a.width)r=root_a.width;
        if(b>root_a.height)b=root_a.height;
        if(r<=l||b<=t)goto bad;
        composite=XGetImage(d,overlay,l,t,(unsigned)(r-l),(unsigned)(b-t),AllPlanes,ZPixmap);
        int64_t overlay_ms=now_ms();
        if(!composite||xerror||!use_visual_masks(composite,&overlay_a)||!XGetWindowAttributes(d,window,&after))goto bad;
        if(bytes+2*(MAX_SIDE*MAX_SIDE*3+40)>OUTPUT_LIMIT)goto done;
        char a[PATH_MAX],o[PATH_MAX];
        int na=snprintf(a,sizeof(a),"%s/sample-%03u-frame.ppm",argv[2],index);
        int no=snprintf(o,sizeof(o),"%s/sample-%03u-overlay.ppm",argv[2],index);
        if(na<0||no<0||(size_t)na>=sizeof(a)||(size_t)no>=sizeof(o)||!save_ppm(a,backing,&bytes)||!save_ppm(o,composite,&bytes)){xerror=-1;goto bad;}
        saved++;
        printf("S40_CONTENT index=%u monotonic_ms=%"PRId64" elapsed_ms=%"PRId64" pixmap=0x%lx backing=%ux%u before=%dx%d after=%dx%d root=%d,%d overlay_crop=%d,%d,%d,%d backing_ms=%"PRId64" overlay_ms=%"PRId64" span_ms=%"PRId64" geometry_changed=%d\n",
            index,sample_start,sample_start-start,pix,w,h,before.width,before.height,after.width,after.height,
            root_x,root_y,l,t,r-l,b-t,backing_ms,overlay_ms,overlay_ms-sample_start,
            before.width!=after.width||before.height!=after.height);
        color_metrics(backing,ext,index);
        goto next;
bad:
        failed++;printf("S40_CONTENT_SAMPLE_ERROR index=%u xerror=%d elapsed_ms=%"PRId64"\n",index,xerror,now_ms()-start);
next:
        if(backing)XDestroyImage(backing);
        if(composite)XDestroyImage(composite);
        if(pix)XFreePixmap(d,pix);
        fflush(stdout);
        if(xerror&&xerror!=BadMatch&&xerror!=BadPixmap)break;
        int64_t wake=start+(int64_t)attempts*SAMPLE_MS;if(wake<now_ms())wake=now_ms();if(wake>finish)wake=finish;until(wake);
        continue;
done:
        if(backing)XDestroyImage(backing);
        if(composite)XDestroyImage(composite);
        if(pix)XFreePixmap(d,pix);
        break;
    }
    printf("S40_CONTENT_SUMMARY saved=%u failures=%u attempts=%u bytes=%zu same_frame=NOT_GUARANTEED motion=NOT_AUTOMATICALLY_CLASSIFIED\n",saved,failed,attempts,bytes);
    XCompositeReleaseOverlayWindow(d,root);XCloseDisplay(d);return saved?0:4;
}
