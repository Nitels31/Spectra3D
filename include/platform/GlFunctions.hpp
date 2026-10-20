#pragma once
#include <GL/gl.h>
#include <GL/glext.h>

namespace s3d {

// THE OPENGL FUNCTIONS THE 1.1 HEADERS DO NOT HAVE
//
// Windows' opengl32.dll exports the 1.1 API and nothing more: the newer
// entry points live in the driver's own library, and they are reached through
// wglGetProcAddress. The engine needs them - the shaders, the framebuffer the
// shadow map is drawn into, the three-dimensional textures - so they are
// loaded here, once, and called through the table.
//
// The table is a list of names and not a list of addresses to guess: a
// function the driver does not have is a function that is left empty, and the
// engine says which one instead of jumping into nothing.
namespace gl {

extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLDELETESHADERPROC DeleteShader;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLDETACHSHADERPROC DetachShader;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLDELETEPROGRAMPROC DeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLGETATTRIBLOCATIONPROC GetAttribLocation;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM1FPROC Uniform1f;
extern PFNGLUNIFORM3FPROC Uniform3f;
extern PFNGLUNIFORM1FVPROC Uniform1fv;
extern PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;

extern PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
extern PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
extern PFNGLFRAMEBUFFERTEXTURELAYERPROC FramebufferTextureLayer;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
extern PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
extern PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
extern PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;

extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLTEXIMAGE3DPROC TexImage3D;

} // namespace gl

// Loads them.
//
// It has to be called with a current context - the addresses come from the
// driver, and the driver is the one that has them - and it is worth calling
// once: the functions are the same for the whole run.
//
// False when a function the engine needs is not there. The engine goes on
// without it and says so in the log: a driver without shaders can still draw
// a scene, just not one with shadows in it.
bool loadGlFunctions();

} // namespace s3d
