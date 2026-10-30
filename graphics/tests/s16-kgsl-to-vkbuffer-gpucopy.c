/* GPU-S1.6: real single-process KGSL EGL GPU pixmap -> DMA-BUF ->
 * Android vendor Vulkan VkBuffer -> private VkImage GPU-copy -> readback.
 * One-shot GPU producer/consumer validation.  No Bionic memory broker,
 * libhybris, installed driver replacement or live Xwayland modification.
 * This tests Adreno/KGSL specifically; common host fallback is vendor-neutral.
 */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <errno.h>
struct header {uint32_t magic,width,height,stride,format,tiling,backing_height;uint64_t allocation_size;};
#define MAGIC 0x53f80017u
static int choose_memory(const VkPhysicalDeviceMemoryProperties *mem,uint32_t bits,VkMemoryPropertyFlags flags){
 for(uint32_t i=0;i<mem->memoryTypeCount;i++)
  if((bits&(1u<<i)) && (mem->memoryTypes[i].propertyFlags&flags)==flags)return (int)i;
 return -1;
}
#include "s16-vkbuffer-stage1-copy.inc"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <sys/stat.h>
static int has_ext(const char *p,const char *needle){
 if(!p)return 0;size_t n=strlen(needle);
 for(const char *s=p;(s=strstr(s,needle));s+=n)if((s==p||s[-1]==' ')&&(s[n]==0||s[n]==' '))return 1;
 return 0;
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);
 PFNEGLGETPLATFORMDISPLAYEXTPROC get=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
 PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");
 PFNEGLDESTROYIMAGEKHRPROC destroy=(void*)eglGetProcAddress("eglDestroyImageKHR");
 PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query=(void*)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
 PFNEGLEXPORTDMABUFIMAGEMESAPROC export=(void*)eglGetProcAddress("eglExportDMABUFImageMESA");
 if(!get||!create||!destroy||!query||!export){puts("S16_KGSL_EGL_EXPORT_FUNCS=MISSING");return 2;}
 EGLDisplay d=get(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
 EGLint maj=0,min=0;if(d==EGL_NO_DISPLAY||!eglInitialize(d,&maj,&min)){printf("S16_KGSL_EGL_INIT=FAIL 0x%x\n",eglGetError());return 2;}
 const char *ext=eglQueryString(d,EGL_EXTENSIONS);
 printf("S16_KGSL_EGL_EXPORT_EXT=%d IMPORT_EXT=%d\n",has_ext(ext,"EGL_MESA_image_dma_buf_export"),has_ext(ext,"EGL_EXT_image_dma_buf_import"));
 if(!has_ext(ext,"EGL_MESA_image_dma_buf_export")){puts("S16_KGSL_EGL_EXPORT=NO_EXT");return 2;}
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};EGLConfig cfg;EGLint ncfg=0;
 if(!eglChooseConfig(d,attrs,&cfg,1,&ncfg)||!ncfg||!eglBindAPI(EGL_OPENGL_ES_API)){puts("S16_KGSL_GL_CONFIG=FAIL");return 3;}
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
 EGLSurface surf=eglCreatePbufferSurface(d,cfg,pa);EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);
 if(ctx==EGL_NO_CONTEXT||surf==EGL_NO_SURFACE||!eglMakeCurrent(d,surf,surf,ctx)){puts("S16_KGSL_GL_CONTEXT=FAIL");return 3;}
 printf("S16_KGSL_GPU=%s\n",glGetString(GL_RENDERER));
 unsigned w=640,h=360;
 const char *size=getenv("S16_EGL_SIZE");
 if(size && (sscanf(size,"%ux%u",&w,&h)!=2 || w<64 || h<64 || w>1920 || h>1080)){
  puts("S16_EGL_SIZE=INVALID");return 2;
 }
 printf("S16_EGL_TEXTURE_DIMENSIONS=%ux%u\n",w,h);
 GLuint tex=0,fbo=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){puts("S16_KGSL_FBO=FAIL");return 4;}
 if(getenv("S16_GRADIENT_TEST")) {
  /* A solid-color surface cannot detect row/tile swizzle corruption.
   * Draw a high-frequency coordinate-dependent RGB pattern with GPU GL.
   * The Vulkan consumer must recover each logical pixel unchanged. */
  const char *vs="attribute vec2 position;void main(){gl_Position=vec4(position,0.0,1.0);}";
  const char *fs="precision highp float;void main(){vec2 p=floor(gl_FragCoord.xy);gl_FragColor=vec4(mod(p.x,251.0)/255.0,mod(p.y,241.0)/255.0,mod(3.0*p.x+5.0*p.y,251.0)/255.0,1.0);}";
  GLuint shv=glCreateShader(GL_VERTEX_SHADER),shf=glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(shv,1,&vs,NULL);glCompileShader(shv);
  glShaderSource(shf,1,&fs,NULL);glCompileShader(shf);
  GLint v_ok=0,f_ok=0;
  glGetShaderiv(shv,GL_COMPILE_STATUS,&v_ok);glGetShaderiv(shf,GL_COMPILE_STATUS,&f_ok);
  if(!v_ok||!f_ok){puts("S16_SPATIAL_GLSL_COMPILE=FAIL");return 9;}
  GLuint program=glCreateProgram();glAttachShader(program,shv);glAttachShader(program,shf);glLinkProgram(program);
  GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
  if(!linked){puts("S16_SPATIAL_GLSL_LINK=FAIL");return 9;}
  glUseProgram(program);
  glViewport(0,0,w,h);
  static const GLfloat vertices[]={-1.f,-1.f,1.f,-1.f,-1.f,1.f,1.f,1.f};
  GLint attr=glGetAttribLocation(program,"position");
  glVertexAttribPointer((GLuint)attr,2,GL_FLOAT,GL_FALSE,0,vertices);
  glEnableVertexAttribArray((GLuint)attr);
  glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  glFinish();
  printf("S16_SPATIAL_GL_DRAW_ERROR=0x%x\n",glGetError());
 } else {
  glClearColor(.1f,.7f,.9f,1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glFinish();
 }
 EGLint ia[]={EGL_GL_TEXTURE_LEVEL_KHR,0,EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
 EGLImageKHR image=create(d,ctx,EGL_GL_TEXTURE_2D_KHR,(EGLClientBuffer)(uintptr_t)tex,ia);
 if(image==EGL_NO_IMAGE_KHR){printf("S16_KGSL_EGL_IMAGE=FAIL 0x%x\n",eglGetError());return 4;}
 EGLint fourcc=0,planes=0;EGLuint64KHR mod=0;
 if(!query(d,image,&fourcc,&planes,&mod)){printf("S16_KGSL_EXPORT_QUERY=FAIL 0x%x\n",eglGetError());return 5;}
 printf("S16_KGSL_EXPORT_METADATA=fourcc:0x%08x planes:%d modifier:0x%llx\n",fourcc,planes,(unsigned long long)mod);
 if(planes!=1){puts("S16_KGSL_UNSUPPORTED_PLANES");return 5;}
 int fds[4]={-1,-1,-1,-1},stride[4]={0},offset[4]={0};
 if(!export(d,image,fds,stride,offset)){printf("S16_KGSL_EXPORT_FD=FAIL 0x%x\n",eglGetError());return 5;}
 struct stat st={0};if(fds[0]>=0)fstat(fds[0],&st);
 printf("S16_KGSL_GPU_EXPORT=fd:%d stride:%d offset:%d size:%llu\n",fds[0],stride[0],offset[0],(unsigned long long)st.st_size);
 int rc=6;
 if(fds[0]>=0&&(unsigned)stride[0]>=w*4&&offset[0]==0&&mod==0&&st.st_size>0&& (fourcc==0x34324241||fourcc==0x34325241)){
  struct header shared={.magic=MAGIC,.width=w,.height=h,.stride=(uint32_t)stride[0],.format=fourcc==0x34324241?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_B8G8R8A8_UNORM,.tiling=VK_IMAGE_TILING_LINEAR,.backing_height=h,.allocation_size=(uint64_t)st.st_size};
  rc=s16_gpu_copy_probe(fds[0],&shared);
  printf("S16_KGSL_TO_ANDROID_VKIMAGE_GPU_COPY=%s\n",rc==0?"PASS":"FAIL");
 }
 printf("S16_KGSL_TO_VENDOR_VKBUFFER=%s\n",rc==0?"PASS":"FAIL");
 for(int k=0;k<4;k++)if(fds[k]>=0)close(fds[k]);
 destroy(d,image);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 eglDestroyContext(d,ctx);eglDestroySurface(d,surf);eglTerminate(d);
 return rc;
}
