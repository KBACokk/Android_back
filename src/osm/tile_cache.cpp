#include "tile_cache.hpp"

#include <fstream>

bool tile_read_file_bytes(const std::filesystem::path& path, std::vector<unsigned char>& out_bytes) {
    out_bytes.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out_bytes.resize(static_cast<std::size_t>(sz));
    f.read(reinterpret_cast<char*>(out_bytes.data()), sz);
    return static_cast<bool>(f);
}

bool tile_write_file_bytes(const std::filesystem::path& path, const unsigned char* data, std::size_t len) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(f);
}
