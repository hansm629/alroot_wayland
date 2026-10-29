/* Probe an actual EGL texture -> GPU DMA-BUF export, not an extension name.
 * Uses caller-scoped Termux Mesa EGL (Zink/GL, never the Android system EGL).
 * No CPU glReadPixels, no Xwayland/Wayland session modification.
 */
#define EGL_NO_X11
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "alroot_vulkan_import_fd.h"

static int has(const char *ext, const char *needle) {
    if (!ext || !needle) return 0;
    size_t n=strlen(needle);
    for (const char *p=ext;(p=strstr(p,needle));p+=n)
        if ((p==ext || p[-1]==' ') && (p[n]==0 || p[n]==' ')) return 1;
    return 0;
}

int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    PFNEGLCREATEIMAGEKHRPROC create_image =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy_image =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query =
        (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)
        eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    PFNEGLEXPORTDMABUFIMAGEMESAPROC export_image =
        (PFNEGLEXPORTDMABUFIMAGEMESAPROC)
        eglGetProcAddress("eglExportDMABUFImageMESA");
    if (!get_display || !create_image || !destroy_image || !query || !export_image) {
        puts("EGL_DMA_BUF_GPU_EXPORT=FUNCTION_UNAVAILABLE");
        return 2;
    }
    EGLDisplay dpy=get_display(EGL_PLATFORM_SURFACELESS_MESA,
                               EGL_DEFAULT_DISPLAY,NULL);
    EGLint ma=0,mi=0;
    if (dpy==EGL_NO_DISPLAY || !eglInitialize(dpy,&ma,&mi)) {
        puts("EGL_DMA_BUF_GPU_EXPORT=NO_DISPLAY");
        return 2;
    }
    const char *ext=eglQueryString(dpy,EGL_EXTENSIONS);
    if ((!has(ext,"EGL_MESA_image_dma_buf_export") &&
         !getenv("ALROOT_TEST_FORCE_EGL_EXPORT")) ||
        !has(ext,"EGL_KHR_image_base") ||
        !has(ext,"EGL_KHR_gl_texture_2D_image")) {
        puts("EGL_DMA_BUF_GPU_EXPORT=EXTENSION_MISSING");
        eglTerminate(dpy);
        return 2;
    }
    EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig config=NULL;
    EGLint n=0;
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(dpy,config_attrs,&config,1,&n) || !n) {
        puts("EGL_DMA_BUF_GPU_EXPORT=NO_CONFIG");
        eglTerminate(dpy);
        return 2;
    }
    EGLint pb[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface surf=eglCreatePbufferSurface(dpy,config,pb);
    EGLContext ctx=eglCreateContext(dpy,config,EGL_NO_CONTEXT,ca);
    int ok=0;
    if (ctx==EGL_NO_CONTEXT || surf==EGL_NO_SURFACE ||
        !eglMakeCurrent(dpy,surf,surf,ctx)) {
        puts("EGL_DMA_BUF_GPU_EXPORT=NO_CONTEXT");
        goto done;
    }
    printf("EGL_GPU_RENDERER=%s\n",glGetString(GL_RENDERER));
    GLuint tex=0, fbo=0;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        puts("EGL_DMA_BUF_GPU_EXPORT=FBO_INCOMPLETE");
        goto image_cleanup;
    }
    glClearColor(0.1f,0.7f,0.9f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish(); /* correctness, not the Stage-2 no-CPU-fence objective */
    EGLImageKHR img=create_image(dpy,ctx,EGL_GL_TEXTURE_2D_KHR,
                                (EGLClientBuffer)(uintptr_t)tex,NULL);
    if (img==EGL_NO_IMAGE_KHR) {
        printf("EGL_DMA_BUF_GPU_EXPORT=CREATE_IMAGE_FAILED error=0x%x\n",
               eglGetError());
        goto image_cleanup;
    }
    EGLint fourcc=0, planes=0;
    EGLuint64KHR modifier=0;
    if (!query(dpy,img,&fourcc,&planes,&modifier) || planes<1 || planes>4) {
        printf("EGL_DMA_BUF_GPU_EXPORT=QUERY_FAILED planes=%d\n",planes);
        destroy_image(dpy,img);
        goto image_cleanup;
    }
    int fds[4]={-1,-1,-1,-1}, offsets[4]={0}, strides[4]={0};
    if (!export_image(dpy,img,fds,strides,offsets)) {
        puts("EGL_DMA_BUF_GPU_EXPORT=EXPORT_FAILED");
    } else {
        printf("EGL_DMA_BUF_FD0=%d\n",fds[0]);
        printf("EGL_DMA_BUF_FOURCC=0x%08x\n",fourcc);
        printf("EGL_DMA_BUF_MODIFIER=0x%016llx\n",(unsigned long long)modifier);
        printf("EGL_DMA_BUF_PLANES=%d\n",planes);
        printf("EGL_DMA_BUF_PLANE0_STRIDE=%d\n",strides[0]);
        /* A successful EGL DMA-BUF export is not enough for the Android
         * display Vulkan driver. Check OPAQUE_FD and DMA_BUF import in a
         * second loader before treating this as a GPU-shareable frame. */
        if (fds[0]<0) {
            puts("EGL_DMA_BUF_GPU_EXPORT=INVALID_FD");
        } else if (getenv("ALROOT_TEST_VENDOR_FD_IMPORT") && planes==1) {
            int result=alroot_probe_vulkan_import_fd(fds[0],
                                                     (uint32_t)fourcc,64,64);
            printf("EGL_EXPORT_TO_ANDROID_VULKAN=%s\n",
                   result==0?"PASS":"UNAVAILABLE");
            ok=1;
        } else {
            ok=1;
        }
        if (ok) puts("EGL_DMA_BUF_GPU_EXPORT=PASS");
    }
    for(int i=0;i<4;i++) if(fds[i]>=0) close(fds[i]);
    destroy_image(dpy,img);
image_cleanup:
    if(fbo)glDeleteFramebuffers(1,&fbo);
    if(tex)glDeleteTextures(1,&tex);
done:
    eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(ctx!=EGL_NO_CONTEXT)eglDestroyContext(dpy,ctx);
    if(surf!=EGL_NO_SURFACE)eglDestroySurface(dpy,surf);
    eglTerminate(dpy);
    return ok?0:3;
}
