#pragma once
#include <vector>

#include <GL/gl.h>

#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Fog.hpp"
#include "render/ShaderProgram.hpp"
#include "render/ShadowMap.hpp"

namespace s3d {

// THE WATER
//
// The water is a surface of its own, with a program of its own, and it is
// drawn after everything else: it is the one thing in the picture that you
// can see through, and something you can see through has to be drawn over
// the things behind it.
//
// IT IS A GRID
//
// The waves are worked out per point, and a sheet with four corners has four
// waves in it and a sheet with twenty-five thousand has twenty-five
// thousand. The grid is the grid of the whole world - it runs from -1 to 1
// in both directions and it is scaled to the sheet - so that two sheets of
// water in the same place have the same waves in them, and a wave that
// belonged to the sheet would be a different wave in every one of them.
//
// The sheet is a grid and not a cube because the water is a surface: a
// cube has a lid and a floor and four walls, and the walls of a lake are
// not what a lake is.
//
// THE PICTURE UNDER THE WATER
//
// The water bends what is under it, and a thing that is bent has to be
// there to be bent. So the picture of the scene is copied into a texture
// before the water is drawn over it, and the water reads the copy.
//
// The copy is of the scene as it is on the screen - the same pixels the
// water is drawn over - so that what shows through the water is the world
// that is really there, and not a picture of it from somewhere else.
class WaterSystem {
public:
    ~WaterSystem();

    WaterSystem() = default;
    WaterSystem(const WaterSystem&) = delete;
    WaterSystem& operator=(const WaterSystem&) = delete;

    // The program and the grid.
    //
    // A program that does not load is not a reason to draw no water: the
    // log says what is missing and the engine goes on with what it has.
    void initialize();

    // Releases the grid and the picture. Safe to call twice.
    void release();

    // Whether there is any water in the world at all.
    bool wanted(const PhysicsWorld& world) const;

    // Whether any of the water in the world bends what is under it. The
    // picture of the scene is only copied when something is going to read it.
    bool bendsLight(const PhysicsWorld& world) const;

    // A copy of the picture of the scene, taken before the water is drawn
    // over it.
    //
    // `x`, `y`, `width` and `height` are the rectangle of the scene in the
    // window, in the window's own numbers: the bottom left corner of it and
    // how big it is.
    void captureScene(int x, int y, int width, int height);

    // The water of the world.
    //
    // The camera and the two matrices are the ones the scene was drawn with,
    // the fog and the shadows are the ones of the picture, and the viewport
    // is where the picture of the scene is in the window - the water needs
    // it to know where it is on the screen, because that is what it bends.
    void draw(const PhysicsWorld& world, const Camera& camera, const Mat4& view,
              const Mat4& projection, const FogSettings& fog, const ShadowMap& shadows,
              int viewportX, int viewportY, int viewportWidth, int viewportHeight, float time,
              int hiddenBody);

private:
    // The grid of the water: the points of it and the indices of them.
    void buildGrid();

    // The texture the picture of the scene is copied into, made again when
    // the size of the picture changes.
    bool ensureSceneTexture(int width, int height);

    ShaderProgram program_;

    // The grid, on the card. The points of it and the indices of them are
    // two buffers, because the water is drawn out of the card and not out of
    // the memory of the program: the grid is twenty-five thousand points and
    // it is the same every frame.
    unsigned int gridBuffer_ = 0;
    unsigned int indexBuffer_ = 0;
    int indexCount_ = 0;

    // The picture of the scene, for the bend of the light.
    unsigned int sceneTexture_ = 0;
    int sceneWidth_ = 0;
    int sceneHeight_ = 0;
    // Whether the copy of the picture works at all. A driver that will not
    // copy out of the window's own buffer is rare, and the water is still
    // water without the bend.
    bool sceneCopyWorks_ = true;
    // Whether the copy has been tried. It is tried once: a driver that will
    // not copy out of the window's own buffer will not copy out of it the
    // second time either, and asking every frame would cost a flush of the
    // whole command stream every frame.
    bool sceneChecked_ = false;
    // Whether there is a picture to bend in this frame.
    bool sceneValid_ = false;
};

} // namespace s3d
