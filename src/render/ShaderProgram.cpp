#include "render/ShaderProgram.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/GlFunctions.hpp"

namespace s3d {
namespace {

// The folder the build puts the shaders in, next to the program.
constexpr const char* kShaderFolder = "shaders";

// Where the shaders are in the source tree. The build knows it, so the engine
// runs out of the tree without anything being staged.
#ifndef SPECTRA_SHADER_DIR
#define SPECTRA_SHADER_DIR ""
#endif

// Reads a file and hands it to the driver.
bool compileStage(unsigned int shader, const std::string& source, const char* what) {
    const char* text = source.c_str();
    const GLint length = (GLint)source.size();
    gl::ShaderSource(shader, 1, &text, &length);
    gl::CompileShader(shader);

    GLint ok = 0;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return true;

    // The driver's own words: what is wrong with which line. A shader that does
    // not compile is a shader nobody can fix without this.
    GLint logLength = 0;
    gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(logLength > 0 ? logLength : 1), '\0');
    if (logLength > 0) gl::GetShaderInfoLog(shader, logLength, nullptr, log.data());
    Log::error("Shader: the %s stage did not compile: %s", what, log.c_str());
    return false;
}

} // namespace

std::string shaderPath(const std::string& relative) {
    // The folder next to the program: where the build puts them.
    const std::string staged = str::join(fs::executableDir(), kShaderFolder);
    const std::string inBuild = str::join(staged, relative);
    if (fs::exists(inBuild)) return inBuild;

    // The source tree: so that the engine runs out of the tree, before
    // anything has been staged.
    const std::string source = str::join(SPECTRA_SHADER_DIR, relative);
    if (!source.empty() && fs::exists(source)) return source;

    // And the working directory, for the rest.
    const std::string here = str::join(kShaderFolder, relative);
    if (fs::exists(here)) return here;

    // The first place, even when there is nothing there: the error that
    // follows says which file was looked for and where.
    return inBuild;
}

ShaderProgram::~ShaderProgram() {
    if (program_) gl::DeleteProgram(program_);
}

bool ShaderProgram::load(const std::string& vertexFile, const std::string& fragmentFile) {
    std::string vertexSource, fragmentSource;
    if (!fs::readFile(vertexFile, vertexSource)) {
        Log::error("Shader: cannot read '%s'", vertexFile.c_str());
        return false;
    }
    if (!fs::readFile(fragmentFile, fragmentSource)) {
        Log::error("Shader: cannot read '%s'", fragmentFile.c_str());
        return false;
    }

    const unsigned int vertex = gl::CreateShader(GL_VERTEX_SHADER);
    const unsigned int fragment = gl::CreateShader(GL_FRAGMENT_SHADER);
    bool ok = compileStage(vertex, vertexSource, "vertex");
    if (ok) ok = compileStage(fragment, fragmentSource, "fragment");
    if (!ok) {
        gl::DeleteShader(vertex);
        gl::DeleteShader(fragment);
        return false;
    }

    const unsigned int program = gl::CreateProgram();
    gl::AttachShader(program, vertex);
    gl::AttachShader(program, fragment);
    gl::LinkProgram(program);
    // The shaders are attached to the program and are not needed after that:
    // the program has them, and the driver frees them when the program goes.
    gl::DetachShader(program, vertex);
    gl::DetachShader(program, fragment);
    gl::DeleteShader(vertex);
    gl::DeleteShader(fragment);

    GLint linked = 0;
    gl::GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLength = 0;
        gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength > 0 ? logLength : 1), '\0');
        if (logLength > 0) gl::GetProgramInfoLog(program, logLength, nullptr, log.data());
        Log::error("Shader: the program did not link: %s", log.c_str());
        gl::DeleteProgram(program);
        return false;
    }

    if (program_) gl::DeleteProgram(program_);
    program_ = program;
    uniforms_.clear();
    Log::info("Shader: '%s' and '%s' are one program", str::fileNameView(vertexFile).data(),
              str::fileNameView(fragmentFile).data());
    return true;
}

void ShaderProgram::use() const {
    if (program_) gl::UseProgram(program_);
}

int ShaderProgram::uniformLocation(const char* name) const {
    const auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    const int location = program_ ? gl::GetUniformLocation(program_, name) : -1;
    uniforms_.emplace(name, location);
    return location;
}

int ShaderProgram::attribute(const char* name) const {
    return program_ ? gl::GetAttribLocation(program_, name) : -1;
}

void ShaderProgram::setInt(const char* name, int value) {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform1i(location, value);
}

void ShaderProgram::setFloat(const char* name, float value) {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform1f(location, value);
}

void ShaderProgram::setVec3(const char* name, float x, float y, float z) {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform3f(location, x, y, z);
}

void ShaderProgram::setMat4(const char* name, const float* matrix) {
    const int location = uniformLocation(name);
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE, matrix);
}

void ShaderProgram::setFloatArray(const char* name, const float* values, int count) {
    const int location = uniformLocation(name);
    // The count is the number of numbers: a float array has one number to an
    // element, and the driver is told how many numbers there are.
    if (location >= 0) gl::Uniform1fv(location, count, values);
}

void ShaderProgram::setVec4Array(const char* name, const float* values, int count) {
    const int location = uniformLocation(name);
    // The count is the number of *vectors*: a vec4 array has four numbers to
    // an element, and the driver counts the elements and not the numbers. A
    // count of numbers here writes four vectors' worth into the first vector
    // and leaves the rest of the array as it was.
    if (location >= 0) gl::Uniform4fv(location, count, values);
}


void ShaderProgram::setMat4Array(const char* name, const float* values, int count) {
    const int location = uniformLocation(name);
    // The count is the number of matrices, and it has to be the number the
    // caller means: one matrix leaves the rest of the array as it was, and an
    // array of matrices with three of them missing is a picture of nothing.
    if (location >= 0) gl::UniformMatrix4fv(location, count, GL_FALSE, values);
}


} // namespace s3d
