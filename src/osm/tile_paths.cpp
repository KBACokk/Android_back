#include "tile_paths.hpp"

#ifndef TILE_CACHE_ROOT
    #define TILE_CACHE_ROOT "@TILE_CACHE_ROOT@"
#endif

#include <sstream>

std::filesystem::path tile_png_path(int zoom, int tile_x, int tile_y) {
    std::filesystem::path root("/mnt/c/Users/alt_ma1n/Desktop/Android_back/cache");
    std::ostringstream yname;
    yname << tile_y << ".png";
    return root / std::to_string(zoom) / std::to_string(tile_x) / yname.str();
}
