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
extern PFNGLUNIFORM2FPROC Uniform2f;
extern PFNGLUNIFORM3FPROC Uniform3f;
extern PFNGLUNIFORM4FPROC Uniform4f;
extern PFNGLUNIFORM1FVPROC Uniform1fv;
extern PFNGLUNIFORM4FVPROC Uniform4fv;
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

// The buffers, and the attributes that read them.
//
// The particles are drawn from a buffer and not from an array in the
// program's own memory: there are four thousand of them and they change
// every frame, and a picture that is written into the program's memory is a
// picture that is copied there first.
//
// The divisor is what makes a particle one particle: the four corners of the
// quad are one vertex each, and the centre, the size and the colour of the
// particle are the same for all four of them - so they are read once per
// particle and not once per corner.
extern PFNGLGENBUFFERSPROC GenBuffers;
extern PFNGLDELETEBUFFERSPROC DeleteBuffers;
extern PFNGLBINDBUFFERPROC BindBuffer;
extern PFNGLBUFFERDATAPROC BufferData;
extern PFNGLBUFFERSUBDATAPROC BufferSubData;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
extern PFNGLVERTEXATTRIBDIVISORPROC VertexAttribDivisor;
extern PFNGLDRAWARRAYSINSTANCEDPROC DrawArraysInstanced;

// The sampler objects: the filter of a texture, kept apart from the texture.
//
// The filter belongs to the texture and not to the place it is read from, so
// one picture read two ways - the texels where they are and the texels spread
// out - needs two samplers and not one texture bound to two units. The shadow
// map is read both ways in the same frame.
extern PFNGLGENSAMPLERSPROC GenSamplers;
extern PFNGLDELETESAMPLERSPROC DeleteSamplers;
extern PFNGLBINDSAMPLERPROC BindSampler;
extern PFNGLSAMPLERPARAMETERIPROC SamplerParameteri;

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
