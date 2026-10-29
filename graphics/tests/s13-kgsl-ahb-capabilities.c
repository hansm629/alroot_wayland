#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "alroot_kgsl_ahb_fd.h"

static int vendor_sample_ahb(void *sys, EGLDisplay display,
                             const AHardwareBuffer *ahb,
                             unsigned width,unsigned height,GLubyte pixel[4]) {
    void *gles=dlopen("/system/lib64/libGLESv2.so",RTLD_NOW|RTLD_LOCAL);
    if (!gles) return -1;
    EGLBoolean (*bind)(EGLenum)=dlsym(sys,"eglBindAPI");
    EGLBoolean (*choose)(EGLDisplay,const EGLint *,EGLConfig *,EGLint,EGLint *)=
        dlsym(sys,"eglChooseConfig");
    EGLContext (*create_ctx)(EGLDisplay,EGLConfig,EGLContext,const EGLint *)=
        dlsym(sys,"eglCreateContext");
    EGLSurface (*create_pb)(EGLDisplay,EGLConfig,const EGLint *)=
        dlsym(sys,"eglCreatePbufferSurface");
    EGLBoolean (*make)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=
        dlsym(sys,"eglMakeCurrent");
    EGLBoolean (*destroy_ctx)(EGLDisplay,EGLContext)=dlsym(sys,"eglDestroyContext");
    EGLBoolean (*destroy_pb)(EGLDisplay,EGLSurface)=dlsym(sys,"eglDestroySurface");
    EGLClientBuffer (*get_buf)(const AHardwareBuffer *)=
        dlsym(sys,"eglGetNativeClientBufferANDROID");
    EGLImageKHR (*create_img)(EGLDisplay,EGLContext,EGLenum,EGLClientBuffer,const EGLint *)=
        dlsym(sys,"eglCreateImageKHR");
    EGLBoolean (*destroy_img)(EGLDisplay,EGLImageKHR)=dlsym(sys,"eglDestroyImageKHR");
    void *(*proc)(const char *)=dlsym(sys,"eglGetProcAddress");
    void (*gen_tex)(GLsizei,GLuint *)=dlsym(gles,"glGenTextures");
    void (*bind_tex)(GLenum,GLuint)=dlsym(gles,"glBindTexture");
    void (*tex_param)(GLenum,GLenum,GLint)=dlsym(gles,"glTexParameteri");
    void (*delete_tex)(GLsizei,const GLuint *)=dlsym(gles,"glDeleteTextures");
    void (*gen_fbo)(GLsizei,GLuint *)=dlsym(gles,"glGenFramebuffers");
    void (*bind_fbo)(GLenum,GLuint)=dlsym(gles,"glBindFramebuffer");
    void (*attach)(GLenum,GLenum,GLenum,GLuint,GLint)=
        dlsym(gles,"glFramebufferTexture2D");
    GLenum (*check)(GLenum)=dlsym(gles,"glCheckFramebufferStatus");
    void (*read)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *)=
        dlsym(gles,"glReadPixels");
    GLenum (*error)(void)=dlsym(gles,"glGetError");
    void (*delete_fbo)(GLsizei,const GLuint *)=dlsym(gles,"glDeleteFramebuffers");
    if (!bind||!choose||!create_ctx||!create_pb||!make||!destroy_ctx||
        !destroy_pb||!get_buf||!create_img||!destroy_img||!proc||
        !gen_tex||!bind_tex||!tex_param||!delete_tex||!gen_fbo||
        !bind_fbo||!attach||!check||!read||!error||!delete_fbo) {
        dlclose(gles);return -2;
    }
    void (*image_tex)(GLenum,GLeglImageOES)=(void *)proc("glEGLImageTargetTexture2DOES");
    if (!image_tex) {dlclose(gles);return -2;}
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
                    EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLint pba[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLConfig cfg=NULL;EGLint count=0;
    EGLContext ctx=EGL_NO_CONTEXT;
    EGLSurface surf=EGL_NO_SURFACE;
    EGLImageKHR img=EGL_NO_IMAGE_KHR;
    GLuint tex=0,fb=0;
    int result=-3;
    if (!bind(EGL_OPENGL_ES_API)||!choose(display,attrs,&cfg,1,&count)||!count)
        goto done;
    ctx=create_ctx(display,cfg,EGL_NO_CONTEXT,ca);
    surf=create_pb(display,cfg,pba);
    if(ctx==EGL_NO_CONTEXT||surf==EGL_NO_SURFACE||!make(display,surf,surf,ctx))
        goto done;
    img=create_img(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,get_buf(ahb),NULL);
    if(img==EGL_NO_IMAGE_KHR)goto done;
    gen_tex(1,&tex);bind_tex(GL_TEXTURE_2D,tex);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    image_tex(GL_TEXTURE_2D,(GLeglImageOES)img);
    gen_fbo(1,&fb);bind_fbo(GL_FRAMEBUFFER,fb);
    attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    if(check(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||error()!=GL_NO_ERROR)
        goto done;
    const unsigned points[5][2]={{8,8},{width/2,height/2},
        {width-8,height-8},{8,height-8},{width-8,8}};
    int matched=0;
    for(size_t i=0;i<5;i++) {
        GLubyte sample[4]={0};
        read((GLint)points[i][0],(GLint)points[i][1],1,1,
             GL_RGBA,GL_UNSIGNED_BYTE,sample);
        if(error()!=GL_NO_ERROR) break;
        if(i==0)memcpy(pixel,sample,4);
        printf("S13_KGSL_AHB_VENDOR_PIXEL_%zu=%u,%u,%u,%u at=%u,%u\n",
               i,sample[0],sample[1],sample[2],sample[3],
               points[i][0],points[i][1]);
        if(sample[0]>=22&&sample[0]<=30 &&
           sample[1]>=174&&sample[1]<=184 &&
           sample[2]>=225&&sample[2]<=234 && sample[3]==255)matched++;
    }
    printf("S13_KGSL_AHB_VENDOR_SAMPLE_POINTS=%d/5\n",matched);
    result=matched==5?0:-5;
done:
    if(fb)delete_fbo(1,&fb);
    if(tex)delete_tex(1,&tex);
    if(img!=EGL_NO_IMAGE_KHR)destroy_img(display,img);
    if(ctx!=EGL_NO_CONTEXT)make(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(surf!=EGL_NO_SURFACE)destroy_pb(display,surf);
    if(ctx!=EGL_NO_CONTEXT)destroy_ctx(display,ctx);
    dlclose(gles);
    return result;
}

/* The AOSP VNDK native_handle ABI is inspected at runtime. This is not a
 * public NDK contract: absence or ambiguous image FD selection must fail
 * closed. The optional symbol is dynamically resolved. */
struct alroot_native_handle {
    int version;
    int numFds;
    int numInts;
    int data[];
};

static int has(const char *ext,const char *name) {
    if (!ext || !name) return 0;
    size_t n=strlen(name);
    for (const char *p=ext;(p=strstr(p,name));p+=n)
        if ((p==ext || p[-1]==' ') && (p[n]==' ' || p[n]==0)) return 1;
    return 0;
}
static void caps(const char *p,const char *extensions) {
    const char *names[]={"EGL_ANDROID_image_native_buffer","EGL_EXT_image_dma_buf_import",
       "EGL_EXT_image_dma_buf_import_modifiers","EGL_MESA_image_dma_buf_export",
       "EGL_KHR_image_base","EGL_KHR_gl_texture_2D_image"};
    for (size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)
        printf("S13_%s_%s=%d\n",p,names[i],has(extensions,names[i]));
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    unsigned width=64,height=64;
    const char *sizes=getenv("ALROOT_S13_RESOLUTION");
    if (sizes && sscanf(sizes,"%ux%u",&width,&height)!=2) return 2;
    if(width<64||height<64||width>1920||height>1080)return 2;
    void *sys=dlopen("/system/lib64/libEGL.so",RTLD_NOW|RTLD_LOCAL);
    void *native=dlopen("/system/lib64/libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
    if (!sys||!native) {puts("S13_PLATFORM_LIBRARY=UNAVAILABLE");return 2;}
    EGLDisplay (*sys_get)(EGLNativeDisplayType)=dlsym(sys,"eglGetDisplay");
    EGLBoolean (*sys_init)(EGLDisplay,EGLint *,EGLint *)=dlsym(sys,"eglInitialize");
    const char *(*sys_query)(EGLDisplay,EGLint)=dlsym(sys,"eglQueryString");
    EGLBoolean (*sys_term)(EGLDisplay)=dlsym(sys,"eglTerminate");
    EGLClientBuffer (*sys_native)(const AHardwareBuffer *)=
        dlsym(sys,"eglGetNativeClientBufferANDROID");
    int (*alloc)(const AHardwareBuffer_Desc *,AHardwareBuffer **)=
        dlsym(native,"AHardwareBuffer_allocate");
    void (*release)(AHardwareBuffer *)=dlsym(native,"AHardwareBuffer_release");
    const struct alroot_native_handle *(*get_handle)(const AHardwareBuffer *)=
        dlsym(native,"AHardwareBuffer_getNativeHandle");
    if (!sys_get||!sys_init||!sys_query||!sys_term||!sys_native||!alloc||!release) {
        puts("S13_REQUIRED_PUBLIC_APIS=UNAVAILABLE");return 2;
    }
    EGLint maj=0,min=0;
    EGLDisplay sd=sys_get(EGL_DEFAULT_DISPLAY);
    if (sd!=EGL_NO_DISPLAY&&sys_init(sd,&maj,&min)) {
        puts("S13_ANDROID_EGL=READY");
        caps("ANDROID_EGL",sys_query(sd,EGL_EXTENSIONS));
    } else puts("S13_ANDROID_EGL=UNAVAILABLE");
    PFNEGLGETPLATFORMDISPLAYEXTPROC getplatform=
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay md=getplatform?
        getplatform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL):EGL_NO_DISPLAY;
    if (md==EGL_NO_DISPLAY||!eglInitialize(md,&maj,&min)) {
        puts("S13_KGSL_EGL=UNAVAILABLE");return 2;
    }
    puts("S13_KGSL_EGL=READY");
    caps("KGSL_EGL",eglQueryString(md,EGL_EXTENSIONS));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLint pba[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLint ctxa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLConfig cfg=NULL;EGLint count=0;
    if (!eglBindAPI(EGL_OPENGL_ES_API)||
        !eglChooseConfig(md,attrs,&cfg,1,&count)||!count) return 2;
    EGLSurface pb=eglCreatePbufferSurface(md,cfg,pba);
    EGLContext ctx=eglCreateContext(md,cfg,EGL_NO_CONTEXT,ctxa);
    if (pb==EGL_NO_SURFACE||ctx==EGL_NO_CONTEXT||
        !eglMakeCurrent(md,pb,pb,ctx)) return 2;
    printf("S13_KGSL_GL_RENDERER=%s\n",glGetString(GL_RENDERER));
    const GLubyte *gl_ext=glGetString(GL_EXTENSIONS);
    printf("S13_KGSL_GL_OES_EGL_image=%d\n",has((const char *)gl_ext,"GL_OES_EGL_image"));
    printf("S13_KGSL_GL_EXT_memory_object_fd=%d\n",
           has((const char *)gl_ext,"GL_EXT_memory_object_fd"));
    AHardwareBuffer_Desc desc={.width=width,.height=height,.layers=1,
      .format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
      .usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    AHardwareBuffer *ahb=NULL;
    if (alloc(&desc,&ahb)||!ahb) {
        puts("S13_AHB_ALLOC=FAIL");return 3;
    }
    puts("S13_AHB_ALLOC=PASS");
    void (*describe)(const AHardwareBuffer *,AHardwareBuffer_Desc *)=
        dlsym(native,"AHardwareBuffer_describe");
    if (describe) {
        describe(ahb,&desc);
        printf("S13_AHB_DIMENSIONS=%ux%u stride=%u\n",desc.width,desc.height,desc.stride);
    }
    unsigned texture_width=width,texture_height=height;
    if(getenv("ALROOT_S13_TEXTURE_STRIDE_WIDTH"))texture_width=desc.stride;
    const char *texh=getenv("ALROOT_S13_TEXTURE_HEIGHT");
    if(texh)texture_height=(unsigned)atoi(texh);
    if(texture_width<64||texture_height<64||texture_width>8192||texture_height>4096)
        return 2;
    printf("S13_KGSL_TEXTURE_DIMENSIONS=%ux%u\n",texture_width,texture_height);
    unsigned sample_height=height;
    const char *sh=getenv("ALROOT_S13_SAMPLE_HEIGHT");
    if(sh)sample_height=(unsigned)atoi(sh);
    if(sample_height<64||sample_height>height)return 2;
    EGLClientBuffer buffer=sys_native(ahb);
    printf("S13_ANDROID_GET_NATIVE_CLIENT_BUFFER=%s\n",buffer?"PASS":"FAIL");
    if (!get_handle) {
        puts("S13_AHB_NATIVE_HANDLE_ACCESS=UNAVAILABLE");
    } else {
        const struct alroot_native_handle *nh=get_handle(ahb);
        if (!nh || nh->numFds<1 || nh->numFds>8 ||
            nh->numInts<0 || nh->numInts>1024 || nh->version<12) {
            puts("S13_AHB_NATIVE_HANDLE_ACCESS=INVALID");
        } else {
            printf("S13_AHB_HANDLE_FDS=%d\n",nh->numFds);
            struct stat st;
            for (int i=0;i<nh->numFds;i++) {
                int fd=nh->data[i];
                if (fd<0 || fstat(fd,&st)) continue;
                printf("S13_AHB_FD%d_SIZE=%lld\n",i,(long long)st.st_size);
            }
            uint64_t selected_size=0;
            uint32_t image_stride=0;
            int selected_fd=alroot_kgsl_ahb_dup_image_fd(ahb,&selected_size,&image_stride);
            printf("S13_AHB_IMAGE_FD_SELECTION=%s size=%llu stride=%u\n",
                   selected_fd>=0?"PASS":"UNAVAILABLE",
                   (unsigned long long)selected_size,image_stride);
            if (selected_fd>=0 &&
                has((const char *)gl_ext,"GL_EXT_memory_object_fd")) {
                PFNGLCREATEMEMORYOBJECTSEXTPROC mk=(void *)
                    eglGetProcAddress("glCreateMemoryObjectsEXT");
                PFNGLIMPORTMEMORYFDEXTPROC imp=(void *)
                    eglGetProcAddress("glImportMemoryFdEXT");
                PFNGLTEXSTORAGEMEM2DEXTPROC storage=(void *)
                    eglGetProcAddress("glTexStorageMem2DEXT");
                PFNGLDELETEMEMORYOBJECTSEXTPROC del=(void *)
                    eglGetProcAddress("glDeleteMemoryObjectsEXT");
                if (mk&&imp&&storage&&del) {
                  for (int index=0;index<1;index++) {
                    GLuint mem=0,tex=0,fb=0;
                    struct stat fdstat={0};
                    if (fstat(selected_fd,&fdstat))
                        continue;
                    mk(1,&mem);
                    int copy=dup(selected_fd);
                    if (copy>=0) {
                        GLuint64 size=fdstat.st_size>0?(GLuint64)fdstat.st_size:262144u;
                        imp(mem,size,GL_HANDLE_TYPE_OPAQUE_FD_EXT,copy);
                        GLenum err=glGetError();
                        printf("S13_KGSL_AHB_GL_FD%d_IMPORT=%s gl_error=0x%x\n",
                               index,err==GL_NO_ERROR?"PASS":"FAIL",err);
                        if (err) close(copy);
                        if (!err) {
                            glGenTextures(1,&tex);
                            glBindTexture(GL_TEXTURE_2D,tex);
                            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_TILING_EXT,
                                            GL_LINEAR_TILING_EXT);
                            GLenum tiling_error=glGetError();
                            printf("S13_KGSL_AHB_GL_FD%d_LINEAR_TILING=%s gl_error=0x%x\n",
                                   index,tiling_error==GL_NO_ERROR?"PASS":"FAIL",tiling_error);
                            storage(GL_TEXTURE_2D,1,GL_RGBA8,texture_width,texture_height,mem,0);
                            GLenum storage_error=glGetError();
                            printf("S13_KGSL_AHB_GL_FD%d_STORAGE=%s gl_error=0x%x\n",
                                   index,storage_error==GL_NO_ERROR?"PASS":"FAIL",storage_error);
                            if (!storage_error) {
                                glGenFramebuffers(1,&fb);
                                glBindFramebuffer(GL_FRAMEBUFFER,fb);
                                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                                                       GL_TEXTURE_2D,tex,0);
                                GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
                                printf("S13_KGSL_AHB_GL_FD%d_FBO=%s fbo=0x%x\n",
                                       index,status==GL_FRAMEBUFFER_COMPLETE?"PASS":"FAIL",status);
                                if (status==GL_FRAMEBUFFER_COMPLETE) {
                                    glClearColor(0.1f,0.7f,0.9f,1.f);
                                    glClear(GL_COLOR_BUFFER_BIT);
                                    glFinish();
                                    GLenum clear_error=glGetError();
                                    printf("S13_KGSL_AHB_GL_FD%d_GPU_CLEAR=%s gl_error=0x%x\n",
                                           index,clear_error==GL_NO_ERROR?"SUBMITTED":"FAIL",clear_error);
                                    GLubyte pixel[4]={0};
                                    int sampled=vendor_sample_ahb(sys,sd,ahb,width,sample_height,pixel);
                                    if(sampled==0) {
                                        printf("S13_KGSL_AHB_GL_FD%d_VENDOR_PIXEL=%u,%u,%u,%u\n",
                                               index,pixel[0],pixel[1],pixel[2],pixel[3]);
                                        const int match=(pixel[0]>=22&&pixel[0]<=30&&
                                            pixel[1]>=174&&pixel[1]<=184&&
                                            pixel[2]>=225&&pixel[2]<=234&&pixel[3]==255);
                                        printf("S13_KGSL_AHB_GL_FD%d_CROSS_DRIVER_PIXELS=%s\n",
                                               index,match?"PASS":"MISMATCH");
                                    } else printf("S13_KGSL_AHB_GL_FD%d_VENDOR_READ=FAILED_%d\n",
                                                  index,sampled);
                                }
                            }
                        }
                    }
                    if(fb)glDeleteFramebuffers(1,&fb);
                    if(tex)glDeleteTextures(1,&tex);
                    if(mem)del(1,&mem);
                  }
                } else puts("S13_KGSL_AHB_GL_FD_IMPORT=FUNCTION_UNAVAILABLE");
            }
            if (selected_fd>=0)close(selected_fd);
        }
    }
    PFNEGLCREATEIMAGEKHRPROC create=(PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy=(PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    if (buffer&&create) image=create(md,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,buffer,NULL);
    if (image==EGL_NO_IMAGE_KHR) {
        printf("S13_KGSL_AHB_EGL_IMAGE=REJECTED_0x%04x\n",eglGetError());
    } else {
        puts("S13_KGSL_AHB_EGL_IMAGE=PASS");
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC use=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
        GLuint tex=0,fb=0;
        glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
        if (use) use(GL_TEXTURE_2D,image);
        GLenum err=glGetError();
        glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
        GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("S13_KGSL_AHB_FBO=%s gl_error=0x%x fbo=0x%x\n",
               err==GL_NO_ERROR&&status==GL_FRAMEBUFFER_COMPLETE?"PASS":"FAIL",err,status);
        glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&tex);
        if (destroy)destroy(md,image);
    }
    release(ahb);
    eglMakeCurrent(md,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(md,pb);eglDestroyContext(md,ctx);eglTerminate(md);
    if(sd!=EGL_NO_DISPLAY)sys_term(sd);
    dlclose(native);dlclose(sys);
    return 0;
}
