#pragma once

#include <filesystem>
#include <string>

std::filesystem::path tile_png_path(int zoom, int tile_x, int tile_y);
