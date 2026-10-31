/* Experimental KGSL rootful Xwayland AHB output, not the default backend.
 * Android AHB is imported as a GLES FBO via GL_EXT_memory_object_fd, and
 * shared with the existing Alroot host through the public AHB handle API.
 * This preserves Xorg root/content ownership and Xfwm4 XComposite.
 * It must only be used with a pinned private Xwayland and an opted-in
 * full-HD Adreno 840 GLES driver. Unsupported states fail closed to the
 * original Xwayland DMA-BUF path without changing any global libraries. */

#define _GNU_SOURCE
#include "s36_xwl_ahb_output.h"
#include "alroot_kgsl_ahb_fd.h"
#include "alroot-gpu-buffer-v1-client-protocol.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif
#ifndef GL_TEXTURE_TILING_EXT
#define GL_TEXTURE_TILING_EXT 0x9580
#endif
#ifndef GL_LINEAR_TILING_EXT
#define GL_LINEAR_TILING_EXT 0x9585
#endif

struct s36_ahb_output {
    AHardwareBuffer *ahb;
    EGLDisplay egl;
    GLuint memory, texture, read_fbo, write_fbo;
    int width, height;
    struct wl_buffer *buffer;
    int direct_output;
    int glamor_owns_texture;
};
struct s36_api {
    void *libnative;
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
    void (*release)(AHardwareBuffer *);
    int (*send_handle)(const AHardwareBuffer *, int);
    PFNGLCREATEMEMORYOBJECTSEXTPROC create_memory;
    PFNGLIMPORTMEMORYFDEXTPROC import_fd;
    PFNGLTEXSTORAGEMEM2DEXTPROC storage;
    PFNGLDELETEMEMORYOBJECTSEXTPROC delete_memory;
    struct alroot_gpu_buffer_v1 *manager;
    struct wl_registry *registry;
    struct wl_display *display;
    int symbols_checked, supported, manager_logged;
};
static struct s36_api s36;

static void s36_global(void *user, struct wl_registry *registry, uint32_t id,
                       const char *interface, uint32_t version)
{
    struct s36_api *api = user;
    if (!api->manager &&
        !strcmp(interface, alroot_gpu_buffer_v1_interface.name)) {
        api->manager = wl_registry_bind(registry, id,
                                        &alroot_gpu_buffer_v1_interface,
                                        version >= 1 ? 1 : version);
        fprintf(stderr, "S36_MANAGER_BOUND display=%p registry=%p manager=%p id=%u\n",
                (void *)api->display, (void *)registry,
                (void *)api->manager,
                api->manager ? wl_proxy_get_id((struct wl_proxy *)api->manager) : 0);
    }
}
static void s36_remove(void *user, struct wl_registry *registry, uint32_t id)
{
    (void)user; (void)registry; (void)id;
}
static const struct wl_registry_listener s36_registry_listener = {
    .global = s36_global, .global_remove = s36_remove,
};

void s36_ahb_output_probe(struct wl_display *display)
{
    if (!display) return;
    if (s36.display != display) {
        fprintf(stderr, "S36_MANAGER_DISPLAY_CHANGE previous=%p current=%p\n",
                (void *)s36.display, (void *)display);
        /* A wl_proxy is scoped to exactly one wl_display connection. Old
         * globals are destroyed with that connection; do not marshal a
         * new-buffer request via a manager associated with the old one. */
        s36.manager = NULL;
        s36.registry = NULL;
        s36.display = NULL;
    }
    if (s36.registry) {
        static int s36_logs;
        if (s36_logs++ < 4)
            fprintf(stderr, "S36_MANAGER_REUSED display=%p registry=%p manager=%p\n",
                    (void *)display,(void *)s36.registry,(void *)s36.manager);
        return;
    }
    s36.display = display;
    /* Separate registry, so Xwayland's primary registry listener is never
     * overwritten. Globals are dispatched asynchronously on its normal
     * Wayland event loop, with zero synchronous roundtrip in a frame. */
    s36.registry = wl_display_get_registry(display);
    if (s36.registry)
        wl_registry_add_listener(s36.registry, &s36_registry_listener, &s36);
    wl_display_flush(display);
}

void s36_ahb_output_reset(struct wl_display *display)
{
    fprintf(stderr, "S36_MANAGER_CLEANUP previous=%p requested=%p manager=%p\n",
            (void *)s36.display, (void *)display, (void *)s36.manager);
    if (display && s36.display != display) return;
    /* Do not call wl_proxy_destroy on a possibly already disconnected
     * connection during Xorg server generation teardown. Its Wayland
     * proxy pool is released by wl_display_disconnect. */
    s36.manager = NULL;
    s36.registry = NULL;
    s36.display = NULL;
}

static int s36_init_symbols(void)
{
    if (s36.symbols_checked) return s36.supported;
    s36.symbols_checked = 1;
    s36.libnative = dlopen("/system/lib64/libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    if (!s36.libnative) return 0;
    s36.allocate = dlsym(s36.libnative, "AHardwareBuffer_allocate");
    s36.release = dlsym(s36.libnative, "AHardwareBuffer_release");
    s36.send_handle = dlsym(s36.libnative, "AHardwareBuffer_sendHandleToUnixSocket");
    s36.create_memory = (PFNGLCREATEMEMORYOBJECTSEXTPROC)
        eglGetProcAddress("glCreateMemoryObjectsEXT");
    s36.import_fd = (PFNGLIMPORTMEMORYFDEXTPROC)
        eglGetProcAddress("glImportMemoryFdEXT");
    s36.storage = (PFNGLTEXSTORAGEMEM2DEXTPROC)
        eglGetProcAddress("glTexStorageMem2DEXT");
    s36.delete_memory = (PFNGLDELETEMEMORYOBJECTSEXTPROC)
        eglGetProcAddress("glDeleteMemoryObjectsEXT");
    s36.supported = s36.allocate && s36.release && s36.send_handle &&
        s36.create_memory && s36.import_fd && s36.storage &&
        s36.delete_memory;
    return s36.supported;
}

static void s36_gl_restore(GLint old_read, GLint old_draw, GLint old_tex,
                           GLboolean old_scissor)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex);
    if (old_scissor) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
}

int s36_ahb_output_copy(struct s36_ahb_output *out,
                        unsigned int source_texture)
{
    if (!out || !out->texture || !out->write_fbo || !source_texture)
        return -1;
    if (out->direct_output) {
        /* GLAMOR rendered directly to this AHB FBO. The Wayland host
         * imports the SAME storage as Vulkan VkImage; no extra GL blit,
         * VkBuffer staging allocation, or CPU pixel copy is needed. */
        if (source_texture != out->texture) return -1;
        glFinish();
        return glGetError() == GL_NO_ERROR ? 0 : -1;
    }
    GLint previous_read = 0, previous_draw = 0, previous_texture = 0;
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, out->read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, source_texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, out->write_fbo);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        s36_gl_restore(previous_read, previous_draw, previous_texture, scissor);
        return -1;
    }
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, out->width, out->height,
                      0, 0, out->width, out->height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    /* Correctness first: the Android Vulkan importer must not see a
     * partially written AHB while its GL producer is still rendering.
     * Future release-fence support can replace this CPU wait safely. */
    glFinish();
    GLenum err = glGetError();
    s36_gl_restore(previous_read, previous_draw, previous_texture, scissor);
    return err == GL_NO_ERROR ? 0 : -1;
}

struct wl_buffer *s36_ahb_output_recreate_buffer(struct s36_ahb_output *out)
{
    if (!out || !out->ahb || !s36.manager) return NULL;
    int pair[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        return NULL;
    int rc = s36.send_handle(out->ahb, pair[0]);
    close(pair[0]);
    if (rc != 0) { close(pair[1]); return NULL; }
    struct wl_buffer *buffer = alroot_gpu_buffer_v1_create_buffer(
        s36.manager, pair[1], (uint32_t)out->width, (uint32_t)out->height);
    close(pair[1]);
    out->buffer = buffer;
    return buffer;
}

void s36_ahb_output_destroy(struct s36_ahb_output *out)
{
    if (!out) return;
    if (out->read_fbo) glDeleteFramebuffers(1, &out->read_fbo);
    if (out->write_fbo) glDeleteFramebuffers(1, &out->write_fbo);
    if (out->texture && !out->glamor_owns_texture)
        glDeleteTextures(1, &out->texture);
    if (out->memory && s36.delete_memory) s36.delete_memory(1, &out->memory);
    if (out->ahb && s36.release) s36.release(out->ahb);
    free(out);
}

unsigned int s36_ahb_output_texture(struct s36_ahb_output *out)
{
    return out ? out->texture : 0;
}

void s36_ahb_output_glamor_owns_texture(struct s36_ahb_output *out)
{
    if (out) out->glamor_owns_texture = 1;
}

struct wl_buffer *s36_ahb_output_create(struct wl_display *display,
                                        EGLDisplay egl,
                                        unsigned int source_texture,
                                        int width, int height,
                                        struct s36_ahb_output **result)
{
    if (!result) return NULL;
    *result = NULL;
    s36_ahb_output_probe(display);
    /* S1.3 full-HD pixel identity and Android VkImage host import were
     * tested on S26U. Never guess at other resolutions or GPU memory
     * layouts, where row stride/padding could cause GPU memory damage. */
    if (!s36.manager || !s36_init_symbols() ||
        width != 1920 || height != 1080)
        return NULL;
    struct s36_ahb_output *out = calloc(1, sizeof(*out));
    if (!out) return NULL;
    out->egl = egl;
    out->width = width;
    out->height = height;
    out->direct_output = !source_texture;
    AHardwareBuffer_Desc desc = {
        .width = (uint32_t)width, .height = (uint32_t)height,
        .layers = 1, .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
    };
    if (s36.allocate(&desc, &out->ahb) != 0 || !out->ahb) goto failed;
    uint64_t bytes = 0;
    uint32_t stride = 0;
    int fd = alroot_kgsl_ahb_dup_image_fd(out->ahb, &bytes, &stride);
    if (fd < 0) goto failed;
    /* Exact stride is needed for correct GLES framebuffer interpretation.
     * Guest KGSL driver does not expose generic linear modifiers. */
    if (stride != (uint32_t)width || bytes < (uint64_t)width*height*4u) {
        close(fd);
        goto failed;
    }
    s36.create_memory(1, &out->memory);
    if (!out->memory) { close(fd); goto failed; }
    s36.import_fd(out->memory, bytes, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
    if (glGetError() != GL_NO_ERROR) {
        /* GL_EXT_memory_object_fd consumes the descriptor only when the
         * import succeeds. Never leak a gralloc DMA-BUF on failure. */
        close(fd);
        goto failed;
    }
    /* The GL external-memory import owns fd on success. */
    GLint old_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex);
    glGenTextures(1, &out->texture);
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_LINEAR_TILING_EXT);
    s36.storage(GL_TEXTURE_2D, 1, GL_RGBA8, width, height, out->memory, 0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex);
    if (glGetError() != GL_NO_ERROR) goto failed;
    glGenFramebuffers(1, &out->read_fbo);
    glGenFramebuffers(1, &out->write_fbo);
    GLint old_draw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, out->write_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, out->texture, 0);
    int fbo_ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw);
    if (!fbo_ok || glGetError() != GL_NO_ERROR ||
        (source_texture && s36_ahb_output_copy(out, source_texture) != 0))
        goto failed;
    if (!s36_ahb_output_recreate_buffer(out)) goto failed;
    if (!s36.manager_logged) {
        s36.manager_logged = 1;
        fprintf(stderr, "S36_ROOTFUL_AHB_OUTPUT=ACTIVE direct_AHB_backing=%d extent=%dx%d host_wl_buffer=AHB VkImage\n",
                out->direct_output, width, height);
    }
    *result = out;
    return out->buffer;
failed:
    s36_ahb_output_destroy(out);
    return NULL;
}
