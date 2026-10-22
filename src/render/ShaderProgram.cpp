#include "render/ShaderProgram.hpp"

#include <set>
#include <vector>

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/GlFunctions.hpp"

namespace s3d {
namespace {

// The folder the build puts the shaders in, next to the program.
constexpr const char* kShaderFolder = "shaders";

// THE INCLUDES.
//
// GLSL has no #include of its own, and the fog is a piece of code that three
// shaders want and that has to be the same code in all of them: a fog that
// is written out three times is a fog that is three different fogs the first
// time one of them is changed.
//
// So the loader reads the line, finds the file and puts the file where the
// line was. The line is `#include "name"` and the file is looked for next to
// the file that asked for it - and then next to the shader that was loaded,
// so that a shader can ask for a file in the folder of the shaders as well.
//
// A file that is already in the picture is not put in it twice: the second
// ask is a comment, and the code is in the picture once.

// The folder of a path: everything before the last separator.
std::string folderOf(const std::string& path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut);
}

// The path with the "." and the ".." taken out of it.
//
// It is the key a file is remembered by: two ways of writing the same file -
// "shaders/fog/fog.glsl" and "shaders/water/../fog/fog.glsl" - are one file,
// and a file that is put in the picture twice is a shader that does not
// compile.
std::string plainPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= path.size()) {
        const size_t end = path.find_first_of("/\\", at);
        const size_t stop = end == std::string::npos ? path.size() : end;
        const std::string piece = path.substr(at, stop - at);
        if (piece == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!piece.empty() && piece != ".") {
            parts.push_back(piece);
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

// The file a line asks for, or nothing when the line does not ask for one.
//
// Only a line whose first word is `#include` and whose file is in quotes is
// an include: a line in a comment is a line in a comment.
std::string includeName(const std::string& line) {
    size_t at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at >= line.size() || line[at] != '#') return std::string();
    ++at;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
    const std::string word = "include";
    if (line.compare(at, word.size(), word) != 0) return std::string();
    at += word.size();
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at >= line.size() || line[at] != '"') return std::string();
    const size_t end = line.find('"', at + 1);
    if (end == std::string::npos) return std::string();
    return line.substr(at + 1, end - at - 1);
}

// The source with the includes in it.
std::string withIncludes(const std::string& source, const std::string& folder,
                         const std::string& root, std::set<std::string>& done, int depth) {
    // The nesting is not deep on purpose: a file that includes itself is a
    // mistake, and a mistake that runs out of stack is a worse way to hear
    // about it than a mistake that says what it is.
    if (depth > 8) {
        Log::error("Shader: the includes are nested too deeply");
        return source;
    }

    std::string out;
    out.reserve(source.size() + 256);

    size_t at = 0;
    while (at <= source.size()) {
        const size_t end = source.find('\n', at);
        const size_t stop = end == std::string::npos ? source.size() : end;
        const std::string line = source.substr(at, stop - at);

        const std::string name = includeName(line);
        if (name.empty()) {
            out += line;
            if (end != std::string::npos) out += '\n';
        } else {
            // The file, and where it was found: next to the file that asked
            // for it, and then next to the shader that was loaded.
            std::string path = str::join(folder, name);
            if (!fs::exists(path)) {
                const std::string beside = str::join(root, name);
                if (fs::exists(beside)) path = beside;
            }
            if (!fs::exists(path)) {
                Log::error("Shader: the include '%s' is not there", name.c_str());
            } else if (done.insert(plainPath(path)).second) {
                std::string text;
                if (fs::readFile(path, text)) {
                    out += withIncludes(text, folderOf(path), root, done, depth + 1);
                } else {
                    Log::error("Shader: the include '%s' cannot be read", name.c_str());
                }
            }
        }

        if (end == std::string::npos) break;
        at = end + 1;
    }
    return out;
}


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
    // The project's own shaders first: a project that brings a shader of its
    // own is drawn with it, and the engine's is there for the rest.
    const std::string& projectAssets = fs::projectAssetRoot();
    if (!projectAssets.empty()) {
        const std::string inProject = str::join(str::join(projectAssets, kShaderFolder), relative);
        if (fs::exists(inProject)) return inProject;
    }

    // The engine's own shaders, which live in the assets of the engine - in a
    // built game that is the `plugins` folder of the data.
    const std::string& assets = fs::assetRoot();
    if (!assets.empty()) {
        const std::string inAssets = str::join(str::join(assets, kShaderFolder), relative);
        if (fs::exists(inAssets)) return inAssets;
    }

    // The folder next to the program: where the build puts them.
    const std::string staged = str::join(fs::executableDir(), kShaderFolder);
    const std::string inBuild = str::join(staged, relative);
    if (fs::exists(inBuild)) return inBuild;

    // The source tree: so that the engine runs out of the tree, before
    // anything has been staged. The build points at the folder the shaders
    // are in and not at the folder above it - the shaders live in
    // "src/shaders" and the engine asks for "shaders/scene.vert".
    const std::string source = str::join(SPECTRA_SHADER_DIR, kShaderFolder);
    const std::string inSource = str::join(source, relative);
    if (!source.empty() && fs::exists(inSource)) return inSource;

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

    // The includes, put in place of the lines that ask for them. The vertex
    // stage and the fragment stage are two files and two pictures, so each of
    // them gets its own memory of what is already in it.
    {
        std::set<std::string> done;
        vertexSource =
            withIncludes(vertexSource, folderOf(vertexFile), folderOf(vertexFile), done, 0);
    }
    {
        std::set<std::string> done;
        fragmentSource =
            withIncludes(fragmentSource, folderOf(fragmentFile), folderOf(fragmentFile), done, 0);
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

void ShaderProgram::setInt(const char* name, int value) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform1i(location, value);
}

void ShaderProgram::setFloat(const char* name, float value) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform1f(location, value);
}

void ShaderProgram::setVec2(const char* name, float x, float y) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform2f(location, x, y);
}

void ShaderProgram::setVec3(const char* name, float x, float y, float z) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform3f(location, x, y, z);
}

void ShaderProgram::setVec4(const char* name, float x, float y, float z, float w) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::Uniform4f(location, x, y, z, w);
}

void ShaderProgram::setMat4(const char* name, const float* matrix) const {
    const int location = uniformLocation(name);
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE, matrix);
}

void ShaderProgram::setFloatArray(const char* name, const float* values, int count) const {
    const int location = uniformLocation(name);
    // The count is the number of numbers: a float array has one number to an
    // element, and the driver is told how many numbers there are.
    if (location >= 0) gl::Uniform1fv(location, count, values);
}

void ShaderProgram::setVec4Array(const char* name, const float* values, int count) const {
    const int location = uniformLocation(name);
    // The count is the number of *vectors*: a vec4 array has four numbers to
    // an element, and the driver counts the elements and not the numbers. A
    // count of numbers here writes four vectors' worth into the first vector
    // and leaves the rest of the array as it was.
    if (location >= 0) gl::Uniform4fv(location, count, values);
}


void ShaderProgram::setMat4Array(const char* name, const float* values, int count) const {
    const int location = uniformLocation(name);
    // The count is the number of matrices, and it has to be the number the
    // caller means: one matrix leaves the rest of the array as it was, and an
    // array of matrices with three of them missing is a picture of nothing.
    if (location >= 0) gl::UniformMatrix4fv(location, count, GL_FALSE, values);
}


} // namespace s3d
