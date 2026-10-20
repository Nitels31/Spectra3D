#include "platform/GlFunctions.hpp"

#include "core/Log.hpp"

#include <string>
#include <vector>

#if !defined(_WIN32)
#include <GLFW/glfw3.h>
#endif

namespace s3d {
namespace gl {

PFNGLCREATESHADERPROC CreateShader = nullptr;
PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
PFNGLCOMPILESHADERPROC CompileShader = nullptr;
PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC DeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
PFNGLATTACHSHADERPROC AttachShader = nullptr;
PFNGLDETACHSHADERPROC DetachShader = nullptr;
PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC UseProgram = nullptr;
PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
PFNGLGETATTRIBLOCATIONPROC GetAttribLocation = nullptr;
PFNGLUNIFORM1IPROC Uniform1i = nullptr;
PFNGLUNIFORM1FPROC Uniform1f = nullptr;
PFNGLUNIFORM3FPROC Uniform3f = nullptr;
PFNGLUNIFORM1FVPROC Uniform1fv = nullptr;
PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;

PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURELAYERPROC FramebufferTextureLayer = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
PFNGLGENRENDERBUFFERSPROC GenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC BindRenderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage = nullptr;

PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
PFNGLTEXIMAGE3DPROC TexImage3D = nullptr;

} // namespace gl

namespace {

// The address of one function, or nothing.
void* load(const char* name) {
#if defined(_WIN32)
    return (void*)wglGetProcAddress(name);
#else
    return (void*)glfwGetProcAddress(name);
#endif
}

} // namespace

bool loadGlFunctions() {
    // The list of the functions and where they go. A macro and not a table of
    // pairs, because the types are all different and a table would need a
    // union for every one of them.
#define SPECTRA_GL_LOAD(type, name)                                     \
    gl::name = reinterpret_cast<type>(load("gl" #name));               \
    if (!gl::name) {                                                    \
        Log::error("OpenGL: the driver has no gl%s", #name);             \
        missing = true;                                                 \
    }

    bool missing = false;

    SPECTRA_GL_LOAD(PFNGLCREATESHADERPROC, CreateShader)
    SPECTRA_GL_LOAD(PFNGLSHADERSOURCEPROC, ShaderSource)
    SPECTRA_GL_LOAD(PFNGLCOMPILESHADERPROC, CompileShader)
    SPECTRA_GL_LOAD(PFNGLGETSHADERIVPROC, GetShaderiv)
    SPECTRA_GL_LOAD(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)
    SPECTRA_GL_LOAD(PFNGLDELETESHADERPROC, DeleteShader)
    SPECTRA_GL_LOAD(PFNGLCREATEPROGRAMPROC, CreateProgram)
    SPECTRA_GL_LOAD(PFNGLATTACHSHADERPROC, AttachShader)
    SPECTRA_GL_LOAD(PFNGLDETACHSHADERPROC, DetachShader)
    SPECTRA_GL_LOAD(PFNGLLINKPROGRAMPROC, LinkProgram)
    SPECTRA_GL_LOAD(PFNGLGETPROGRAMIVPROC, GetProgramiv)
    SPECTRA_GL_LOAD(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)
    SPECTRA_GL_LOAD(PFNGLUSEPROGRAMPROC, UseProgram)
    SPECTRA_GL_LOAD(PFNGLDELETEPROGRAMPROC, DeleteProgram)
    SPECTRA_GL_LOAD(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)
    SPECTRA_GL_LOAD(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation)
    SPECTRA_GL_LOAD(PFNGLUNIFORM1IPROC, Uniform1i)
    SPECTRA_GL_LOAD(PFNGLUNIFORM1FPROC, Uniform1f)
    SPECTRA_GL_LOAD(PFNGLUNIFORM3FPROC, Uniform3f)
    SPECTRA_GL_LOAD(PFNGLUNIFORM1FVPROC, Uniform1fv)
    SPECTRA_GL_LOAD(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv)
    SPECTRA_GL_LOAD(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)
    SPECTRA_GL_LOAD(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)
    SPECTRA_GL_LOAD(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)
    SPECTRA_GL_LOAD(PFNGLFRAMEBUFFERTEXTURELAYERPROC, FramebufferTextureLayer)
    SPECTRA_GL_LOAD(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer)
    SPECTRA_GL_LOAD(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)
    SPECTRA_GL_LOAD(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)
    SPECTRA_GL_LOAD(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)
    SPECTRA_GL_LOAD(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)
    SPECTRA_GL_LOAD(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)
    SPECTRA_GL_LOAD(PFNGLACTIVETEXTUREPROC, ActiveTexture)
    SPECTRA_GL_LOAD(PFNGLTEXIMAGE3DPROC, TexImage3D)

#undef SPECTRA_GL_LOAD

    if (missing) {
        Log::warn("OpenGL: the driver is missing some of the functions the shadows need");
        return false;
    }
    Log::info("OpenGL: the functions the shadows need are loaded");
    return true;
}

} // namespace s3d
