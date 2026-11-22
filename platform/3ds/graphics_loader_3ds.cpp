// CPU-side resource bookkeeping for the original world loader.
// Rendering stays in Citro3D. These bindings do not implement OpenGL:
// desktop draw submissions fail explicitly rather than disappearing silently.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <unordered_map>
#include <stdexcept>
#include <type_traits>
#include <cstdio>

namespace {
struct Texture { int width=0,height=0,format=0; };
std::unordered_map<GLuint,Texture> textures;
std::unordered_map<GLuint,GLsizeiptr> buffers;
std::unordered_map<GLenum,GLuint> bindings;
GLuint nextHandle=1,boundTexture=0;
void Generate(GLsizei count, GLuint* out) {
    if (count<0 || !out || nextHandle>0x70000000u || GLuint(count)>0x70000000u-nextHandle) throw std::runtime_error("Invalid graphics resource allocation");
    while(count--) *out++=nextHandle++;
}
void BindBuffer(GLenum target,GLuint object) { bindings[target]=object; }
void BufferData(GLenum target,GLsizeiptr bytes,const void*,GLenum) {
    if(bytes<0 || !bindings[target]) throw std::runtime_error("Invalid loader buffer");
    buffers[bindings[target]]=bytes;
}
void BufferSubData(GLenum target,GLintptr offset,GLsizeiptr bytes,const void*) {
    const auto it=buffers.find(bindings[target]);
    if(it==buffers.end() || offset<0 || bytes<0 || offset>it->second || bytes>it->second-offset)
        throw std::runtime_error("Loader buffer update out of bounds");
}
void DeleteBuffers(GLsizei n,const GLuint* objects) { while(n-->0) buffers.erase(*objects++); }
void BindTexture(GLenum,GLuint object) { boundTexture=object; }
void TextureStorage(GLenum,GLsizei,GLenum format,GLsizei width,GLsizei height) {
    if(!boundTexture || width<=0 || height<=0) throw std::runtime_error("Invalid loader texture");
    textures[boundTexture]={width,height,int(format)};
}
void DeleteTextures(GLsizei n,const GLuint* objects) { while(n-->0) textures.erase(*objects++); }
void GetTexture(GLenum,GLint level,GLenum key,GLint* value) {
    const auto t=textures.find(boundTexture);if(t==textures.end()) {*value=0;return;}
    if(key==GL_TEXTURE_WIDTH) *value=std::max(1,t->second.width>>level);
    else if(key==GL_TEXTURE_HEIGHT) *value=std::max(1,t->second.height>>level);
    else if(key==GL_TEXTURE_INTERNAL_FORMAT) *value=t->second.format;
    else *value=0;
}
void GetInteger(GLenum key,GLint* value) {
    switch(key) {
    case GL_TEXTURE_BINDING_2D:*value=boundTexture;break;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:*value=16;break;
    // No desktop extension support or projected texture-array shadows.
    default:*value=0;break;
    }
}
void GetFloat(GLenum,GLfloat* value) {*value=1.0f;}
void GetBoolean(GLenum,GLboolean* value) {*value=GL_FALSE;}
GLenum GetError() {return GL_NO_ERROR;}
GLuint UniformBlock(GLuint,const GLchar*) {return 0;}
GLboolean IsEnabled(GLenum) {return GL_FALSE;}
// Only loader-side uploads/state updates may be ignored: original model and
// texture bytes remain owned by SUBGLOB/IMGHDR and are read by the native renderer.
template<class T> struct Ignore;
template<class R,class... A> struct Ignore<R(*)(A...)> {
    static R call(A...) {if constexpr(!std::is_void_v<R>) return R{};}
};
template<size_t N> struct ApiName {
    char value[N];
    constexpr ApiName(const char (&text)[N]) {std::copy_n(text,N,value);}
};
template<class T,ApiName name> struct Unsupported;
template<class R,class... A,ApiName name> struct Unsupported<R(*)(A...),name> {
    static R call(A...) {throw std::runtime_error(std::string("Native loader cannot execute ")+name.value);}
};
}
PFNGLACTIVETEXTUREPROC glad_glActiveTexture = Ignore<PFNGLACTIVETEXTUREPROC>::call;
PFNGLBINDBUFFERPROC glad_glBindBuffer = BindBuffer;
PFNGLBINDBUFFERBASEPROC glad_glBindBufferBase = Ignore<PFNGLBINDBUFFERBASEPROC>::call;
PFNGLBINDBUFFERRANGEPROC glad_glBindBufferRange = Ignore<PFNGLBINDBUFFERRANGEPROC>::call;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer = Unsupported<PFNGLBINDFRAMEBUFFERPROC,"glBindFramebuffer">::call;
PFNGLBINDTEXTUREPROC glad_glBindTexture = BindTexture;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = Ignore<PFNGLBINDVERTEXARRAYPROC>::call;
PFNGLBLENDFUNCSEPARATEPROC glad_glBlendFuncSeparate = Ignore<PFNGLBLENDFUNCSEPARATEPROC>::call;
PFNGLBLITFRAMEBUFFERPROC glad_glBlitFramebuffer = Unsupported<PFNGLBLITFRAMEBUFFERPROC,"glBlitFramebuffer">::call;
PFNGLBUFFERDATAPROC glad_glBufferData = BufferData;
PFNGLBUFFERSUBDATAPROC glad_glBufferSubData = BufferSubData;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus = Unsupported<PFNGLCHECKFRAMEBUFFERSTATUSPROC,"glCheckFramebufferStatus">::call;
PFNGLCLEARPROC glad_glClear = Unsupported<PFNGLCLEARPROC,"glClear">::call;
PFNGLCLEARBUFFERFVPROC glad_glClearBufferfv = Unsupported<PFNGLCLEARBUFFERFVPROC,"glClearBufferfv">::call;
PFNGLCLEARCOLORPROC glad_glClearColor = Unsupported<PFNGLCLEARCOLORPROC,"glClearColor">::call;
PFNGLCOLORMASKPROC glad_glColorMask = Ignore<PFNGLCOLORMASKPROC>::call;
PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC glad_glCompressedTexSubImage2D = Ignore<PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC>::call;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = DeleteBuffers;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers = Unsupported<PFNGLDELETEFRAMEBUFFERSPROC,"glDeleteFramebuffers">::call;
PFNGLDELETESYNCPROC glad_glDeleteSync = Ignore<PFNGLDELETESYNCPROC>::call;
PFNGLDELETETEXTURESPROC glad_glDeleteTextures = DeleteTextures;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = Ignore<PFNGLDELETEVERTEXARRAYSPROC>::call;
PFNGLDEPTHFUNCPROC glad_glDepthFunc = Ignore<PFNGLDEPTHFUNCPROC>::call;
PFNGLDEPTHMASKPROC glad_glDepthMask = Ignore<PFNGLDEPTHMASKPROC>::call;
PFNGLDISABLEPROC glad_glDisable = Ignore<PFNGLDISABLEPROC>::call;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray = Ignore<PFNGLDISABLEVERTEXATTRIBARRAYPROC>::call;
PFNGLDRAWARRAYSPROC glad_glDrawArrays = Unsupported<PFNGLDRAWARRAYSPROC,"glDrawArrays">::call;
PFNGLDRAWBUFFERPROC glad_glDrawBuffer = Unsupported<PFNGLDRAWBUFFERPROC,"glDrawBuffer">::call;
PFNGLDRAWELEMENTSBASEVERTEXPROC glad_glDrawElementsBaseVertex = Unsupported<PFNGLDRAWELEMENTSBASEVERTEXPROC,"glDrawElementsBaseVertex">::call;
PFNGLENABLEPROC glad_glEnable = Ignore<PFNGLENABLEPROC>::call;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = Ignore<PFNGLENABLEVERTEXATTRIBARRAYPROC>::call;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D = Unsupported<PFNGLFRAMEBUFFERTEXTURE2DPROC,"glFramebufferTexture2D">::call;
PFNGLFRAMEBUFFERTEXTURELAYERPROC glad_glFramebufferTextureLayer = Unsupported<PFNGLFRAMEBUFFERTEXTURELAYERPROC,"glFramebufferTextureLayer">::call;
PFNGLFRONTFACEPROC glad_glFrontFace = Ignore<PFNGLFRONTFACEPROC>::call;
PFNGLGENBUFFERSPROC glad_glGenBuffers = Generate;
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers = Generate;
PFNGLGENTEXTURESPROC glad_glGenTextures = Generate;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = Generate;
PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap = Ignore<PFNGLGENERATEMIPMAPPROC>::call;
PFNGLGETBOOLEANVPROC glad_glGetBooleanv = GetBoolean;
PFNGLGETERRORPROC glad_glGetError = GetError;
PFNGLGETFLOATVPROC glad_glGetFloatv = GetFloat;
PFNGLGETINTEGERVPROC glad_glGetIntegerv = GetInteger;
PFNGLGETSTRINGIPROC glad_glGetStringi = Unsupported<PFNGLGETSTRINGIPROC,"glGetStringi">::call;
PFNGLGETTEXLEVELPARAMETERIVPROC glad_glGetTexLevelParameteriv = GetTexture;
PFNGLGETUNIFORMBLOCKINDEXPROC glad_glGetUniformBlockIndex = UniformBlock;
PFNGLISENABLEDPROC glad_glIsEnabled = IsEnabled;
PFNGLLINEWIDTHPROC glad_glLineWidth = Ignore<PFNGLLINEWIDTHPROC>::call;
PFNGLMEMORYBARRIERPROC glad_glMemoryBarrier = Ignore<PFNGLMEMORYBARRIERPROC>::call;
PFNGLPIXELSTOREIPROC glad_glPixelStorei = Ignore<PFNGLPIXELSTOREIPROC>::call;
PFNGLPOLYGONMODEPROC glad_glPolygonMode = Ignore<PFNGLPOLYGONMODEPROC>::call;
PFNGLREADBUFFERPROC glad_glReadBuffer = Unsupported<PFNGLREADBUFFERPROC,"glReadBuffer">::call;
PFNGLSTENCILFUNCPROC glad_glStencilFunc = Ignore<PFNGLSTENCILFUNCPROC>::call;
PFNGLSTENCILOPPROC glad_glStencilOp = Ignore<PFNGLSTENCILOPPROC>::call;
PFNGLTEXPARAMETERFPROC glad_glTexParameterf = Ignore<PFNGLTEXPARAMETERFPROC>::call;
PFNGLTEXPARAMETERIPROC glad_glTexParameteri = Ignore<PFNGLTEXPARAMETERIPROC>::call;
PFNGLTEXSTORAGE2DPROC glad_glTexStorage2D = TextureStorage;
PFNGLTEXSTORAGE3DPROC glad_glTexStorage3D = Unsupported<PFNGLTEXSTORAGE3DPROC,"glTexStorage3D">::call;
PFNGLTEXSUBIMAGE2DPROC glad_glTexSubImage2D = Ignore<PFNGLTEXSUBIMAGE2DPROC>::call;
PFNGLUNIFORM1FPROC glad_glUniform1f = Ignore<PFNGLUNIFORM1FPROC>::call;
PFNGLUNIFORM1IPROC glad_glUniform1i = Ignore<PFNGLUNIFORM1IPROC>::call;
PFNGLUNIFORM2FPROC glad_glUniform2f = Ignore<PFNGLUNIFORM2FPROC>::call;
PFNGLUNIFORM3FPROC glad_glUniform3f = Ignore<PFNGLUNIFORM3FPROC>::call;
PFNGLUNIFORM3FVPROC glad_glUniform3fv = Ignore<PFNGLUNIFORM3FVPROC>::call;
PFNGLUNIFORM4FPROC glad_glUniform4f = Ignore<PFNGLUNIFORM4FPROC>::call;
PFNGLUNIFORM4FVPROC glad_glUniform4fv = Ignore<PFNGLUNIFORM4FVPROC>::call;
PFNGLUNIFORMBLOCKBINDINGPROC glad_glUniformBlockBinding = Ignore<PFNGLUNIFORMBLOCKBINDINGPROC>::call;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = Ignore<PFNGLUNIFORMMATRIX4FVPROC>::call;
PFNGLUNMAPBUFFERPROC glad_glUnmapBuffer = Ignore<PFNGLUNMAPBUFFERPROC>::call;
PFNGLUSEPROGRAMPROC glad_glUseProgram = Ignore<PFNGLUSEPROGRAMPROC>::call;
PFNGLVERTEXATTRIB2FPROC glad_glVertexAttrib2f = Ignore<PFNGLVERTEXATTRIB2FPROC>::call;
PFNGLVERTEXATTRIB3FPROC glad_glVertexAttrib3f = Ignore<PFNGLVERTEXATTRIB3FPROC>::call;
PFNGLVERTEXATTRIBIPOINTERPROC glad_glVertexAttribIPointer = Ignore<PFNGLVERTEXATTRIBIPOINTERPROC>::call;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = Ignore<PFNGLVERTEXATTRIBPOINTERPROC>::call;

// Windowless runtime queries. No desktop event loop or window is created.
extern "C" {
int glfwGetInputMode(GLFWwindow*,int) {return GLFW_CURSOR_NORMAL;}
int glfwGetKey(GLFWwindow*,int) {return GLFW_RELEASE;}
const char* glfwGetKeyName(int,int) {return "3DS button";}
int glfwGetMouseButton(GLFWwindow*,int) {return GLFW_RELEASE;}
int glfwGetWindowAttrib(GLFWwindow*,int key) {return key==GLFW_FOCUSED?GLFW_TRUE:GLFW_FALSE;}
void glfwSetInputMode(GLFWwindow*,int,int) {}
GLFWmonitor* glfwGetPrimaryMonitor() {return nullptr;}
GLFWmonitor** glfwGetMonitors(int* count) {*count=0;return nullptr;}
const char* glfwGetMonitorName(GLFWmonitor*) {return "Nintendo 3DS";}
}
bool g_fDeveloperDebugMode=false;
PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer = Unsupported<PFNGLBINDRENDERBUFFERPROC,"glBindRenderbuffer">::call;
PFNGLBINDSAMPLERPROC glad_glBindSampler = Unsupported<PFNGLBINDSAMPLERPROC,"glBindSampler">::call;
PFNGLBLENDEQUATIONSEPARATEPROC glad_glBlendEquationSeparate = Unsupported<PFNGLBLENDEQUATIONSEPARATEPROC,"glBlendEquationSeparate">::call;
PFNGLBLENDFUNCPROC glad_glBlendFunc = Unsupported<PFNGLBLENDFUNCPROC,"glBlendFunc">::call;
PFNGLCLEARDEPTHPROC glad_glClearDepth = Unsupported<PFNGLCLEARDEPTHPROC,"glClearDepth">::call;
PFNGLCLEARSTENCILPROC glad_glClearStencil = Unsupported<PFNGLCLEARSTENCILPROC,"glClearStencil">::call;
PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers = Unsupported<PFNGLDELETERENDERBUFFERSPROC,"glDeleteRenderbuffers">::call;
PFNGLDEPTHRANGEPROC glad_glDepthRange = Unsupported<PFNGLDEPTHRANGEPROC,"glDepthRange">::call;
PFNGLDRAWELEMENTSPROC glad_glDrawElements = Unsupported<PFNGLDRAWELEMENTSPROC,"glDrawElements">::call;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer = Unsupported<PFNGLFRAMEBUFFERRENDERBUFFERPROC,"glFramebufferRenderbuffer">::call;
PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers = Generate;
PFNGLGENSAMPLERSPROC glad_glGenSamplers = Generate;
PFNGLGETDOUBLEVPROC glad_glGetDoublev = Unsupported<PFNGLGETDOUBLEVPROC,"glGetDoublev">::call;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = Unsupported<PFNGLGETUNIFORMLOCATIONPROC,"glGetUniformLocation">::call;
PFNGLPOINTSIZEPROC glad_glPointSize = Unsupported<PFNGLPOINTSIZEPROC,"glPointSize">::call;
PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage = Unsupported<PFNGLRENDERBUFFERSTORAGEPROC,"glRenderbufferStorage">::call;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glad_glRenderbufferStorageMultisample = Unsupported<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC,"glRenderbufferStorageMultisample">::call;
PFNGLSAMPLERPARAMETERIPROC glad_glSamplerParameteri = Unsupported<PFNGLSAMPLERPARAMETERIPROC,"glSamplerParameteri">::call;
PFNGLSCISSORPROC glad_glScissor = Ignore<PFNGLSCISSORPROC>::call;
PFNGLSTENCILMASKPROC glad_glStencilMask = Unsupported<PFNGLSTENCILMASKPROC,"glStencilMask">::call;
PFNGLTEXIMAGE2DPROC glad_glTexImage2D = Unsupported<PFNGLTEXIMAGE2DPROC,"glTexImage2D">::call;
PFNGLVERTEXATTRIB4FPROC glad_glVertexAttrib4f = Ignore<PFNGLVERTEXATTRIB4FPROC>::call;
PFNGLVIEWPORTPROC glad_glViewport = Unsupported<PFNGLVIEWPORTPROC,"glViewport">::call;

#include <chrono>
extern "C" {
int glfwGetError(const char** description) {if(description)*description=nullptr;return GLFW_NO_ERROR;}
void glfwGetFramebufferSize(GLFWwindow*,int* w,int* h) {if(w)*w=400;if(h)*h=240;}
void glfwGetWindowSize(GLFWwindow* window,int* w,int* h) {glfwGetFramebufferSize(window,w,h);}
void glfwGetWindowContentScale(GLFWwindow*,float* x,float* y) {if(x)*x=1;if(y)*y=1;}
void glfwGetWindowPos(GLFWwindow*,int* x,int* y) {if(x)*x=0;if(y)*y=0;}
void glfwGetMonitorPos(GLFWmonitor*,int* x,int* y) {if(x)*x=0;if(y)*y=0;}
void glfwGetMonitorWorkarea(GLFWmonitor*,int* x,int* y,int* w,int* h) {if(x)*x=0;if(y)*y=0;if(w)*w=400;if(h)*h=240;}
const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*,int* count) {if(count)*count=0;return nullptr;}
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) {return nullptr;}
GLFWmonitor* glfwGetWindowMonitor(GLFWwindow*) {return nullptr;}
void glfwSetWindowAttrib(GLFWwindow*,int,int) {throw std::runtime_error("Desktop window setting unavailable");}
void glfwSetWindowMonitor(GLFWwindow*,GLFWmonitor*,int,int,int,int,int) {throw std::runtime_error("Desktop monitor setting unavailable");}
void glfwSetWindowPos(GLFWwindow*,int,int) {throw std::runtime_error("Desktop window positioning unavailable");}
void glfwSwapInterval(int) {}
double glfwGetTime() {
    using Clock=std::chrono::steady_clock;
    static const auto epoch=Clock::now();
    return std::chrono::duration<double>(Clock::now()-epoch).count();
}
}
namespace ImGui {
ImGuiContext* GetCurrentContext() {return nullptr;}
float GetFrameHeight() {return 0;}
}
bool fQuitGame=false;
