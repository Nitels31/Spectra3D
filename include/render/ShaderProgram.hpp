#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

namespace s3d {

// WHERE THE SHADERS ARE
//
// The shaders are files, and a file has to be found before it can be read.
// They are looked for in three places, in this order:
//
//   1. next to the program, in a "shaders" folder - where the build puts them;
//   2. in the source tree, where the build knows they are - so that the engine
//      runs out of the tree without anything being staged;
//   3. in the working directory, for the times when neither of the two is true.
//
// The first one that has the file wins, and a file that is in none of them is
// a file that is not there.
std::string shaderPath(const std::string& relative);

// A compiled and linked shader program.
//
// The uniforms are looked up once and remembered: a lookup by name in the
// middle of a frame is a string comparison per uniform per draw, and the
// driver has to walk its own table to answer it.
class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // Reads the two files, compiles them and links them. False when any of
    // the three steps failed, and the reason is in the log.
    bool load(const std::string& vertexFile, const std::string& fragmentFile);

    // True when the program is ready to be used.
    bool ready() const { return program_ != 0; }

    // Makes this the program in use.
    void use() const;

    // Sets a uniform. The name is looked up the first time and remembered
    // after that; a uniform the program does not have is a name that is
    // simply not found, and setting it does nothing.
    void setInt(const char* name, int value);
    void setFloat(const char* name, float value);
    void setVec3(const char* name, float x, float y, float z);
    void setMat4(const char* name, const float* matrix);
    // An array of them: the shadow's matrices and the stretches of the scene
    // are arrays, and the name of an array's first element is the name the
    // program knows it by.
    void setFloatArray(const char* name, const float* values, int count);
    void setMat4Array(const char* name, const float* values, int count);

    // Where an attribute lives, or -1 when the program does not have it.
    int attribute(const char* name) const;

private:
    // The place of a uniform in the program, and whether it was found.
    int uniformLocation(const char* name) const;

    unsigned int program_ = 0;
    // The uniforms that were looked up and where they are. A name that was not
    // found is remembered as -1, so that a missing uniform is looked up once
    // and not once per frame.
    mutable std::unordered_map<std::string, int> uniforms_;
};

} // namespace s3d
