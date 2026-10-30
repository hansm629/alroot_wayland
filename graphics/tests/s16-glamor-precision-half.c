/* S16 KGSL GLAMOR precision diagnostic: generates an alternating 1px RGBA
 * 1920x8 GLES texture and compares mediump/highp TEXCOORD sampling.
 * Test-only CPU upload/readback; not the Alroot display/copy path. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define WIDTH 1920
#define HEIGHT 8
static GLuint shader(GLenum type,const char*src){
 GLuint s=glCreateShader(type);glShaderSource(s,1,&src,NULL);glCompileShader(s);
 GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"GLSL_FAIL=%s\n",log);exit(3);}return s;
}
static GLuint prog(const char*precision){
 const char *vs="#version 300 es\nlayout(location=0) in vec2 pos;out highp vec2 texcoord;void main(){gl_Position=vec4(pos,0.,1.);texcoord=pos*.5+.5;}";
 char fs[8192];snprintf(fs,sizeof(fs),"#version 300 es\nprecision %s float;in %s vec2 texcoord;uniform sampler2D inputTex;out vec4 color;void main(){color=texture(inputTex,texcoord);}",precision,precision);
 GLuint a=shader(GL_VERTEX_SHADER,vs),b=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();
 glAttachShader(p,a);glAttachShader(p,b);glLinkProgram(p);
 GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
 if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"GL_LINK_FAIL=%s\n",log);exit(3);}
 glDeleteShader(a);glDeleteShader(b);return p;
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);
 PFNEGLGETPLATFORMDISPLAYEXTPROC get=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
 if(!get)return 2;
 EGLDisplay d=get(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
 EGLint maj=0,min=0;
 if(d==EGL_NO_DISPLAY||!eglInitialize(d,&maj,&min)){fprintf(stderr,"EGL_INIT_FAIL=%x\n",eglGetError());return 2;}
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLConfig cfg;EGLint n=0;
 if(!eglChooseConfig(d,attrs,&cfg,1,&n)||!n)return 2;
 eglBindAPI(EGL_OPENGL_ES_API);
 EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
 EGLint surattrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
 EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxattrs);EGLSurface sur=eglCreatePbufferSurface(d,cfg,surattrs);
 if(c==EGL_NO_CONTEXT||sur==EGL_NO_SURFACE||!eglMakeCurrent(d,sur,sur,c)){fprintf(stderr,"MAKECURRENT_FAIL=%x\n",eglGetError());return 2;}
 printf("S16_GLSL_GPU_RENDERER=%s version:%s glerr:%#x\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),glGetError());
 GLint mr[2],mp=0,hr[2],hp=0;
 glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_MEDIUM_FLOAT,mr,&mp);
 glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,hr,&hp);
 printf("S16_GLSL_MEDIUMP_PRECISION_BITS=%d highp:%d\n",mp,hp);
 uint8_t*src=malloc(WIDTH*HEIGHT*4),*dst=malloc(WIDTH*HEIGHT*4);if(!src||!dst)return 2;
 for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
   size_t i=((size_t)y*WIDTH+x)*4;src[i+0]=(x&1)?255:0;src[i+1]=((x>>1)&1)?255:0;src[i+2]=(y&1)?255:0;src[i+3]=255;
 }
 GLuint input=0,output=0,fb=0,vao=0,vbo=0;
 glGenTextures(1,&input);glBindTexture(GL_TEXTURE_2D,input);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,src);
 glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 float v[]={-1,-1,1,-1,-1,1,1,1};glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
 glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);
 for(int mode=0;mode<2;mode++){
   GLuint p=prog(mode?"highp":"mediump");glUseProgram(p);glUniform1i(glGetUniformLocation(p,"inputTex"),0);
   glViewport(0,0,WIDTH,HEIGHT);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();
   glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,dst);
   uint64_t bad_left=0,bad_right=0,bad_1024=0;int sample_first=-1,sample_last=-1;
   for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
     size_t i=((size_t)y*WIDTH+x)*4;
     if(memcmp(src+i,dst+i,4)!=0){if(x<WIDTH/2)bad_left++;else bad_right++;if(x>=1024)bad_1024++;
       if(sample_first<0)sample_first=x;sample_last=x;}
   }
   printf("S16_GLSL_%s_TEXTURE_COPY_LEFT_BAD=%llu RIGHT_BAD=%llu X_GE1024_BAD=%llu FIRST_BAD_X=%d LAST_BAD_X=%d\n",
     mode?"HIGHP":"MEDIUMP",(unsigned long long)bad_left,(unsigned long long)bad_right,(unsigned long long)bad_1024,sample_first,sample_last);
   if(mode==0 && (bad_left!=0 || bad_right==0)){
   fprintf(stderr,"S16_GLSL_PRECISION_EXPECTED_MEDIUMP_RIGHT_ONLY_FAILURE=NO_REPRO\n");
   return 4;
 }
 if(mode==1 && (bad_left!=0 || bad_right!=0)){
   fprintf(stderr,"S16_GLSL_HIGHP_TEXTURE_SAMPLING=FAIL\n");
   return 5;
 }
 glDeleteProgram(p);
 }
 GLenum err=glGetError();
 printf("S16_GLSL_GPU_ERROR=0x%x\n",err);
 if(err!=GL_NO_ERROR)return 6;
 puts("S16_GLSL_HIGHP_FIX_SUPPORTED=PASS_RIGHT_HALF_NO_BAD_PIXELS");
 return 0;
}
