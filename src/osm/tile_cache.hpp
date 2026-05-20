#pragma once

#include <filesystem>
#include <vector>

bool tile_read_file_bytes(const std::filesystem::path& path, std::vector<unsigned char>& out_bytes);

bool tile_write_file_bytes(const std::filesystem::path& path, const unsigned char* data, std::size_t len);
