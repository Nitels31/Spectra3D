#pragma once
#include <string>
#include <vector>

namespace s3d {

// THE PICTURE OF THE SCREEN, IN A FILE.
//
// A picture of the editor is worth a lot more than a description of it: half
// of the bugs this engine has ever had were found by looking at one. So the
// engine can take one - F12, in the editor and in the launcher - and write it
// next to the program.
//
// The file is a PNG, because a PNG is what a person can look at and what a
// document can carry, and the writer is a header that is already in the tree.
namespace screenshot {

// Writes what is on the screen into `path`.
//
// The picture is read out of the framebuffer, so it has to be called before
// the buffer is swapped: after the swap the picture on the screen is the one
// that was there before. Returns false when the file cannot be written, and
// the reason is in the log.
bool save(const std::string& path, int width, int height);

// Writes a picture that is already in memory: three bytes a pixel, the top
// row first.
//
// The same writer, for the pictures that are not of the screen - the map of
// the shadows, for one, which is the one thing worth looking at when the
// shadows are not what they should be.
bool savePixels(const std::string& path, const std::vector<unsigned char>& pixels, int width,
                int height);

// The next free name for a picture: "screenshot-001.png", "screenshot-002.png"
// and so on, next to the program.
//
// The number is counted from the files that are already there, so a picture
// taken yesterday is not overwritten by one taken today.
std::string nextPath(const std::string& folder);

// WHERE THE PICTURES GO.
//
// **Not next to the program.** The program sits in a build folder, and a
// build folder full of pictures is a build folder nobody can read - and the
// pictures of a program that is still being worked on are not what anybody
// looks for there.
//
// They go into a folder of their own, "test_screenshot", beside the folder
// the program is in - so that they are easy to find and easy to throw away,
// and so that nothing lands in the build itself.
std::string pictureFolder();

} // namespace screenshot
} // namespace s3d
