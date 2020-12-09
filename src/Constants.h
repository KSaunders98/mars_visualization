#pragma once

namespace Aftr {
    constexpr GLuint PATCH_RESOLUTION = 256; // square resolution of imagery and elevation tiles from the database
    constexpr double MARS_SCALE = 1e-1; // scale of planet Mars
    constexpr GLuint NUM_PATCHES_PER_BUFFER = 10; // number of patches per OpenGL buffer
    constexpr int32_t PATCH_RENDER_RADIUS = 1; // number of patches surrounding the current patch to render (in a square, not a circle)
};