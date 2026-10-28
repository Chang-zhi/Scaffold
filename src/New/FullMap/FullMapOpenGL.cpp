#include "FullMapOpenGL.h"
#include "FullMapPerf.h"
#include <Windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <climits>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
 using FullMapGeometry::Rect;
 constexpr GLenum R16UI=0x8234,RedInteger=0x8D94,Framebuffer=0x8D40;
 constexpr GLenum ColorAttachment0=0x8CE0,FramebufferComplete=0x8CD5;
 constexpr GLenum VertexShader=0x8B31,FragmentShader=0x8B30;
 constexpr GLenum CompileStatus=0x8B81,LinkStatus=0x8B82,ClampToEdge=0x812F;

 // Load only on demand: old machines can still load Scaffold without a working
 // OpenGL ICD. No import-table dependency on opengl32 or external loader DLLs.
 struct Functions
 {
  HGLRC (WINAPI* CreateContext)(HDC)=nullptr;
  BOOL (WINAPI* DeleteContext)(HGLRC)=nullptr;
  BOOL (WINAPI* MakeCurrent)(HDC,HGLRC)=nullptr;
  HGLRC (WINAPI* GetCurrentContext)()=nullptr;
  HDC (WINAPI* GetCurrentDC)()=nullptr;
  PROC (WINAPI* GetProc)(LPCSTR)=nullptr;
  const GLubyte* (APIENTRY* GetString)(GLenum)=nullptr;
  void (APIENTRY* GetIntegerv)(GLenum,GLint*)=nullptr;
  GLenum (APIENTRY* GetError)()=nullptr;
  void (APIENTRY* GenTextures)(GLsizei,GLuint*)=nullptr;
  void (APIENTRY* BindTexture)(GLenum,GLuint)=nullptr;
  void (APIENTRY* DeleteTextures)(GLsizei,const GLuint*)=nullptr;
  void (APIENTRY* TexParameteri)(GLenum,GLenum,GLint)=nullptr;
  void (APIENTRY* PixelStorei)(GLenum,GLint)=nullptr;
  void (APIENTRY* TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)=nullptr;
  void (APIENTRY* TexSubImage2D)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*)=nullptr;
  void (APIENTRY* Viewport)(GLint,GLint,GLsizei,GLsizei)=nullptr;
  void (APIENTRY* Disable)(GLenum)=nullptr;
  void (APIENTRY* DrawArrays)(GLenum,GLint,GLsizei)=nullptr;
  void (APIENTRY* DrawBuffer)(GLenum)=nullptr;
  void (APIENTRY* ReadBuffer)(GLenum)=nullptr;
  void (APIENTRY* ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)=nullptr;
  GLuint (APIENTRY* CreateShader)(GLenum)=nullptr;
  void (APIENTRY* ShaderSource)(GLuint,GLsizei,const char* const*,const GLint*)=nullptr;
  void (APIENTRY* CompileShader)(GLuint)=nullptr;
  void (APIENTRY* GetShaderiv)(GLuint,GLenum,GLint*)=nullptr;
  void (APIENTRY* GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,char*)=nullptr;
  void (APIENTRY* DeleteShader)(GLuint)=nullptr;
  GLuint (APIENTRY* CreateProgram)()=nullptr;
  void (APIENTRY* AttachShader)(GLuint,GLuint)=nullptr;
  void (APIENTRY* BindFragDataLocation)(GLuint,GLuint,const char*)=nullptr;
  void (APIENTRY* LinkProgram)(GLuint)=nullptr;
  void (APIENTRY* GetProgramiv)(GLuint,GLenum,GLint*)=nullptr;
  void (APIENTRY* GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,char*)=nullptr;
  void (APIENTRY* UseProgram)(GLuint)=nullptr;
  GLint (APIENTRY* GetUniformLocation)(GLuint,const char*)=nullptr;
  void (APIENTRY* Uniform1i)(GLint,GLint)=nullptr;
  void (APIENTRY* Uniform4i)(GLint,GLint,GLint,GLint,GLint)=nullptr;
  void (APIENTRY* GenFramebuffers)(GLsizei,GLuint*)=nullptr;
  void (APIENTRY* BindFramebuffer)(GLenum,GLuint)=nullptr;
  void (APIENTRY* FramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint)=nullptr;
  GLenum (APIENTRY* CheckFramebufferStatus)(GLenum)=nullptr;
 };
 struct State
 {
  Functions GL;
  HMODULE Library=nullptr;HWND Window=nullptr;HDC DC=nullptr;HGLRC Context=nullptr;
  bool Initialized=false,Failed=false,RegisteredClass=false;
  GLuint Program=0,Input=0,Target=0,FBO=0;
  GLint SourceUniform=-1,DestinationUniform=-1,ClipUniform=-1;
  int InputW=0,InputH=0,TargetW=0,TargetH=0,MaximumTexture=0;
  const uint16_t* UploadedSource=nullptr;uint64_t UploadedRevision=0;
  FullMapOpenGL::Statistics Stats;
  std::vector<uint16_t> PackedSource,Readback;
  char Error[512]{};
 } g_GL;
 constexpr wchar_t ClassName[]=L"Scaffold.FullMap.OffscreenOpenGL";

 bool Fail(const char* what,unsigned code=0)
 {
  if(!g_GL.Failed)
  {
   sprintf_s(g_GL.Error,"%s (0x%08X)",what,code);
   Debug::Log("[FullMap] OpenGL disabled for this session: %s; using CPU scaling\n",g_GL.Error);
  }
  g_GL.Failed=true;return false;
 }
 template<typename T> bool Entry(T& target,const char* name,bool extension=false)
 {
  PROC proc=nullptr;
  if(extension&&g_GL.GL.GetProc)proc=g_GL.GL.GetProc(name);
  const auto address=reinterpret_cast<uintptr_t>(proc);
  if(!proc||address<=3||address==std::numeric_limits<uintptr_t>::max())
   proc=reinterpret_cast<PROC>(GetProcAddress(g_GL.Library,name));
  target=reinterpret_cast<T>(proc);
  return target!=nullptr||Fail(name);
 }
 struct CurrentContext
 {
  HDC PreviousDC;HGLRC Previous;bool OK=false,Changed=false;
  CurrentContext():PreviousDC(g_GL.GL.GetCurrentDC()),Previous(g_GL.GL.GetCurrentContext())
  {
   Changed=Previous!=g_GL.Context||PreviousDC!=g_GL.DC;
   OK=!Changed||g_GL.GL.MakeCurrent(g_GL.DC,g_GL.Context)!=FALSE;
   if(!OK)Fail("wglMakeCurrent",GetLastError());
  }
  ~CurrentContext()
  {
   if(Changed&&!g_GL.GL.MakeCurrent(PreviousDC,Previous))
    Fail("restore previous WGL context",GetLastError());
  }
 };
 bool Check(const char* where)
 {
  const auto error=g_GL.GL.GetError();
  return error==GL_NO_ERROR||Fail(where,error);
 }
 GLuint Shader(GLenum kind,const char* source)
 {
  auto& gl=g_GL.GL;
  const auto shader=gl.CreateShader(kind);
  gl.ShaderSource(shader,1,&source,nullptr);gl.CompileShader(shader);
  GLint compiled=0;gl.GetShaderiv(shader,CompileStatus,&compiled);
  if(!compiled)
  {
   char error[384]{};gl.GetShaderInfoLog(shader,sizeof(error),nullptr,error);
   Debug::Log("[FullMap] OpenGL shader error: %s\n",error);
   gl.DeleteShader(shader);Fail("GLSL 1.30 shader compile");return 0;
  }
  return shader;
 }
 bool Initialize()
 {
  FullMapPerf::Scope timing(FullMapPerf::Stage::GLInit);
  wchar_t path[MAX_PATH]{};
  const auto n=GetSystemDirectoryW(path,MAX_PATH);
  if(!n||n>=MAX_PATH-14)return Fail("GetSystemDirectoryW",GetLastError());
  wcscat_s(path,L"\\opengl32.dll");
  g_GL.Library=LoadLibraryW(path);
  if(!g_GL.Library)return Fail("load system opengl32.dll",GetLastError());
  auto& gl=g_GL.GL;
  if(!Entry(gl.CreateContext,"wglCreateContext")||!Entry(gl.DeleteContext,"wglDeleteContext")
   ||!Entry(gl.MakeCurrent,"wglMakeCurrent")||!Entry(gl.GetCurrentContext,"wglGetCurrentContext")
   ||!Entry(gl.GetCurrentDC,"wglGetCurrentDC")||!Entry(gl.GetProc,"wglGetProcAddress"))return false;
  WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcW;
  wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=ClassName;
  g_GL.RegisteredClass=RegisterClassW(&wc)!=0;
  if(!g_GL.RegisteredClass&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
   return Fail("register offscreen class",GetLastError());
  // This surface is never shown and never becomes the game's presentation HWND.
  g_GL.Window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,ClassName,L"",
   WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);
  if(!g_GL.Window)return Fail("create hidden offscreen window",GetLastError());
  g_GL.DC=GetDC(g_GL.Window);
  PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
  pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=24;
  const auto format=ChoosePixelFormat(g_GL.DC,&pfd);
  if(!format||!SetPixelFormat(g_GL.DC,format,&pfd))return Fail("set offscreen pixel format",GetLastError());
  DescribePixelFormat(g_GL.DC,format,sizeof(pfd),&pfd);
  if((pfd.dwFlags&PFD_GENERIC_FORMAT)&&!(pfd.dwFlags&PFD_GENERIC_ACCELERATED))
   return Fail("software OpenGL is not an acceleration backend");
  g_GL.Context=gl.CreateContext(g_GL.DC);
  if(!g_GL.Context)return Fail("create hardware WGL context",GetLastError());
  CurrentContext current;if(!current.OK)return false;
#define LOAD_GL(name) if(!Entry(gl.name,"gl" #name,true))return false
  LOAD_GL(GetString);LOAD_GL(GetIntegerv);LOAD_GL(GetError);LOAD_GL(GenTextures);LOAD_GL(BindTexture);
  LOAD_GL(DeleteTextures);LOAD_GL(TexParameteri);LOAD_GL(PixelStorei);LOAD_GL(TexImage2D);LOAD_GL(TexSubImage2D);
  LOAD_GL(Viewport);LOAD_GL(Disable);LOAD_GL(DrawArrays);LOAD_GL(DrawBuffer);LOAD_GL(ReadBuffer);LOAD_GL(ReadPixels);
  const char* version=reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
  int major=0,minor=0;
  if(!version||sscanf_s(version,"%d.%d",&major,&minor)!=2||major<3)
   return Fail("hardware OpenGL 3.0 / GLSL 1.30 required");
  LOAD_GL(CreateShader);LOAD_GL(ShaderSource);LOAD_GL(CompileShader);LOAD_GL(GetShaderiv);
  LOAD_GL(GetShaderInfoLog);LOAD_GL(DeleteShader);LOAD_GL(CreateProgram);LOAD_GL(AttachShader);
  LOAD_GL(BindFragDataLocation);LOAD_GL(LinkProgram);LOAD_GL(GetProgramiv);LOAD_GL(GetProgramInfoLog);
  LOAD_GL(UseProgram);LOAD_GL(GetUniformLocation);LOAD_GL(Uniform1i);LOAD_GL(Uniform4i);
  LOAD_GL(GenFramebuffers);LOAD_GL(BindFramebuffer);LOAD_GL(FramebufferTexture2D);LOAD_GL(CheckFramebufferStatus);
#undef LOAD_GL
  gl.GetIntegerv(GL_MAX_TEXTURE_SIZE,&g_GL.MaximumTexture);
  // Integer textures preserve RGB565 exactly; no palette conversion or filtering.
  constexpr const char* vs=R"(#version 130
   void main() {
    vec2 corners[3]=vec2[3](vec2(-1,-1),vec2(-1,3),vec2(3,-1));
    gl_Position=vec4(corners[gl_VertexID],0,1);
   })";
  constexpr const char* ps=R"(#version 130
   uniform usampler2D Terrain;
   uniform ivec4 Source;
   uniform ivec4 Destination;
   uniform ivec4 Clip;
   out uint Pixel;
   void main() {
    // Readback row zero is framebuffer row zero. Both are treated as the
    // logical top row; no CPU flip or lower/upper map mirroring is involved.
    ivec2 relative=ivec2(gl_FragCoord.xy)+Clip.xy-Destination.xy;
    ivec2 cell=Source.xy+(relative*Source.zw)/Destination.zw;
    Pixel=texelFetch(Terrain,cell,0).r;
   })";
  const auto vertex=Shader(VertexShader,vs);if(!vertex)return false;
  const auto pixel=Shader(FragmentShader,ps);if(!pixel){gl.DeleteShader(vertex);return false;}
  g_GL.Program=gl.CreateProgram();gl.AttachShader(g_GL.Program,vertex);gl.AttachShader(g_GL.Program,pixel);
  gl.BindFragDataLocation(g_GL.Program,0,"Pixel");gl.LinkProgram(g_GL.Program);
  gl.DeleteShader(vertex);gl.DeleteShader(pixel);
  GLint linked=0;gl.GetProgramiv(g_GL.Program,LinkStatus,&linked);
  if(!linked)
  {
   char error[384]{};gl.GetProgramInfoLog(g_GL.Program,sizeof(error),nullptr,error);
   Debug::Log("[FullMap] OpenGL link error: %s\n",error);return Fail("link scaling shader");
  }
  gl.UseProgram(g_GL.Program);
  gl.Uniform1i(gl.GetUniformLocation(g_GL.Program,"Terrain"),0);
  g_GL.SourceUniform=gl.GetUniformLocation(g_GL.Program,"Source");
  g_GL.DestinationUniform=gl.GetUniformLocation(g_GL.Program,"Destination");
  g_GL.ClipUniform=gl.GetUniformLocation(g_GL.Program,"Clip");
  if(g_GL.SourceUniform<0||g_GL.DestinationUniform<0||g_GL.ClipUniform<0)return Fail("scaling shader uniforms");
  gl.GenFramebuffers(1,&g_GL.FBO);
  gl.Disable(GL_BLEND);gl.Disable(GL_DITHER);gl.Disable(GL_DEPTH_TEST);gl.Disable(GL_CULL_FACE);gl.Disable(GL_SCISSOR_TEST);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT,2);gl.PixelStorei(GL_PACK_ALIGNMENT,2);
  if(!Check("initialize scaling resources"))return false;
  Debug::Log("[FullMap] OpenGL offscreen scaler: %s; %s; maxTexture=%d (readback prototype)\n",
   reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)),version,g_GL.MaximumTexture);
  g_GL.Initialized=true;return true;
 }
 bool Texture(GLuint& texture,int width,int height)
 {
  auto& gl=g_GL.GL;
  if(texture)gl.DeleteTextures(1,&texture);
  gl.GenTextures(1,&texture);gl.BindTexture(GL_TEXTURE_2D,texture);
  gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,ClampToEdge);
  gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,ClampToEdge);
  gl.TexImage2D(GL_TEXTURE_2D,0,R16UI,width,height,0,RedInteger,GL_UNSIGNED_SHORT,nullptr);
  return Check("create integer texture");
 }
}

bool FullMapOpenGL::Scale(const Image& image,Rect source,Rect destination,Rect clip,Result& output)
{
 output={};
 // Invalid/offscreen rectangles fall back without touching any GL/WGL state.
 if(!image.Pixels||image.Width<=0||image.Height<=0||image.Pitch==INT_MIN
  ||std::abs(image.Pitch)<image.Width||source.X<0||source.Y<0||source.Width<=0||source.Height<=0
  ||static_cast<int64_t>(source.X)+source.Width>image.Width||static_cast<int64_t>(source.Y)+source.Height>image.Height
  ||destination.Width<=0||destination.Height<=0||clip.Width<=0||clip.Height<=0)return false;
 if(clip.X<destination.X||clip.Y<destination.Y
  ||static_cast<int64_t>(clip.X)+clip.Width>static_cast<int64_t>(destination.X)+destination.Width
  ||static_cast<int64_t>(clip.Y)+clip.Height>static_cast<int64_t>(destination.Y)+destination.Height)return false;
 FullMapPerf::Scope timing(FullMapPerf::Stage::ScaleOpenGL);
 if(g_GL.Failed)return false;
 if(!g_GL.Initialized&&!Initialize())return false;
 if(image.Width>g_GL.MaximumTexture||image.Height>g_GL.MaximumTexture
  ||clip.Width>g_GL.MaximumTexture||clip.Height>g_GL.MaximumTexture)return Fail("texture exceeds hardware limit");
 // Avoid integer arithmetic overflow in the GLSL projection even for bad input.
 if(static_cast<int64_t>(destination.Width)*source.Width>INT_MAX
  ||static_cast<int64_t>(destination.Height)*source.Height>INT_MAX)return false;
 CurrentContext current;if(!current.OK)return false;
 auto& gl=g_GL.GL;
 const bool resizeInput=g_GL.InputW!=image.Width||g_GL.InputH!=image.Height;
 if(resizeInput)
 {
  if(!Texture(g_GL.Input,image.Width,image.Height))return false;
  g_GL.InputW=image.Width;g_GL.InputH=image.Height;g_GL.UploadedSource=nullptr;
 }
 if(resizeInput||!image.Revision||g_GL.UploadedSource!=image.Pixels||g_GL.UploadedRevision!=image.Revision)
 {
  FullMapPerf::Scope upload(FullMapPerf::Stage::GLUpload);
  const uint16_t* pixels=image.Pixels;int pitch=image.Pitch;
  if(pitch<0)
  {
   g_GL.PackedSource.resize(static_cast<size_t>(image.Width)*image.Height);
   for(int y=0;y<image.Height;++y)
    memcpy(g_GL.PackedSource.data()+static_cast<size_t>(y)*image.Width,
     pixels+static_cast<ptrdiff_t>(y)*pitch,static_cast<size_t>(image.Width)*2);
   pixels=g_GL.PackedSource.data();pitch=image.Width;
  }
  gl.BindTexture(GL_TEXTURE_2D,g_GL.Input);gl.PixelStorei(GL_UNPACK_ROW_LENGTH,pitch);
  gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,image.Width,image.Height,RedInteger,GL_UNSIGNED_SHORT,pixels);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH,0);
  if(!Check("upload terrain"))return false;
  g_GL.UploadedSource=image.Pixels;g_GL.UploadedRevision=image.Revision;
  ++g_GL.Stats.Uploads;g_GL.Stats.UploadedBytes+=static_cast<uint64_t>(image.Width)*image.Height*2;
 }
 gl.BindFramebuffer(Framebuffer,g_GL.FBO);
 if(g_GL.TargetW!=clip.Width||g_GL.TargetH!=clip.Height)
 {
  if(!Texture(g_GL.Target,clip.Width,clip.Height))return false;
  gl.FramebufferTexture2D(Framebuffer,ColorAttachment0,GL_TEXTURE_2D,g_GL.Target,0);
  if(gl.CheckFramebufferStatus(Framebuffer)!=FramebufferComplete)return Fail("integer framebuffer incomplete");
  g_GL.TargetW=clip.Width;g_GL.TargetH=clip.Height;
 }
 {
  FullMapPerf::Scope draw(FullMapPerf::Stage::GLDraw);
  gl.UseProgram(g_GL.Program);gl.BindTexture(GL_TEXTURE_2D,g_GL.Input);
  gl.DrawBuffer(ColorAttachment0);gl.ReadBuffer(ColorAttachment0);
  gl.Viewport(0,0,clip.Width,clip.Height);
  gl.Uniform4i(g_GL.SourceUniform,source.X,source.Y,source.Width,source.Height);
  gl.Uniform4i(g_GL.DestinationUniform,destination.X,destination.Y,destination.Width,destination.Height);
  gl.Uniform4i(g_GL.ClipUniform,clip.X,clip.Y,clip.Width,clip.Height);
  gl.DrawArrays(GL_TRIANGLES,0,3);
  if(!Check("draw scaled terrain"))return false;
 }
 {
  FullMapPerf::Scope readback(FullMapPerf::Stage::GLReadback);
  g_GL.Readback.resize(static_cast<size_t>(clip.Width)*clip.Height);
  gl.ReadPixels(0,0,clip.Width,clip.Height,RedInteger,GL_UNSIGNED_SHORT,g_GL.Readback.data());
  if(!Check("read back scaled terrain"))return false;
 }
 ++g_GL.Stats.Frames;
 output={g_GL.Readback.data(),clip.Width,clip.Height,clip.Width};
 return true;
}

void FullMapOpenGL::Reset()
{
 // GL objects are exclusively owned by this context. Deleting it releases all
 // textures/programs, including partial initialization, without altering the
 // caller's current context (normally cnc-ddraw's renderer).
 auto& gl=g_GL.GL;
 if(g_GL.Context&&gl.GetCurrentContext&&gl.GetCurrentContext()==g_GL.Context)
  gl.MakeCurrent(nullptr,nullptr);
 if(g_GL.Context&&gl.DeleteContext)gl.DeleteContext(g_GL.Context);
 if(g_GL.DC&&g_GL.Window)ReleaseDC(g_GL.Window,g_GL.DC);
 if(g_GL.Window)DestroyWindow(g_GL.Window);
 if(g_GL.Library)FreeLibrary(g_GL.Library);
 if(g_GL.RegisteredClass)UnregisterClassW(ClassName,GetModuleHandleW(nullptr));
 g_GL={};
}
bool FullMapOpenGL::Active(){return g_GL.Initialized&&!g_GL.Failed;}
const char* FullMapOpenGL::LastError(){return g_GL.Error;}
FullMapOpenGL::Statistics FullMapOpenGL::GetStatistics(){return g_GL.Stats;}
