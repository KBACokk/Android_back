#pragma once

#include <cstdint>
#include <string>
#include <vector>

void tile_loader_global_init();
void tile_loader_global_cleanup();

void tile_loader_start();
void tile_loader_stop();
void tile_loader_clear_queue();

void tile_loader_enqueue(int zoom, int tile_x, int tile_y, std::uint64_t generation);

std::string tile_key(int zoom, int tile_x, int tile_y);

bool tile_loader_try_pop_completed_rgba(std::string& key, int& z, int& x, int& y,
                                        std::vector<unsigned char>& rgba, int& out_w, int& out_h,
                                        std::uint64_t& generation, bool& ok);
