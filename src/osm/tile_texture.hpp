#pragma once

#include <cstddef>
#include <vector>

#include <GL/glew.h>

struct TileTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;

    void destroy();
    /** smooth: GL_LINEAR + mipmaps (heatmap); false: GL_NEAREST (OSM tiles). */
    bool upload_rgba8(const std::vector<unsigned char>& rgba, int w, int h, bool smooth = false);
};
