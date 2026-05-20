#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class HeatmapCriterion {
    RSRP = 0,
    RSRQ,
    RSSI,
    Altitude,
};

struct HeatmapSample {
    double lon = 0.0;
    double lat = 0.0;
    double merc_y = 0.0;
    float rsrp = -140.0f;
    float rsrq = -20.0f;
    float rssi = -140.0f;
    float altitude = 0.0f;
    int earfcn = -1;
};

struct HeatmapJobState {
    enum class Phase { Idle, Checking, Generating, Ready, Error };
    Phase phase = Phase::Idle;
    float progress = 0.0f;
    std::string message;
};

void heatmap_set_build_root(const std::filesystem::path& root);
const std::filesystem::path& heatmap_cache_root();
void heatmap_init();
void heatmap_shutdown();

void heatmap_startup_async();

void heatmap_poll();

HeatmapCriterion heatmap_get_criterion();
void heatmap_set_criterion(HeatmapCriterion c);

int heatmap_get_earfcn();
void heatmap_set_earfcn(int earfcn);

std::vector<int> heatmap_get_available_earfcns();

bool heatmap_layer_enabled();
void heatmap_set_layer_enabled(bool enabled);

const HeatmapJobState& heatmap_job_state();

std::filesystem::path heatmap_tile_png_path(HeatmapCriterion criterion, int earfcn, int zoom, int tile_x,
                                            int tile_y);

bool heatmap_try_get_texture(int zoom, int tile_x, int tile_y, unsigned int& out_gl_tex_id);

struct HeatmapTileQuad {
    unsigned int tex_id = 0;
    double bounds_min_x = 0.0;
    double bounds_min_y = 0.0;
    double bounds_max_x = 0.0;
    double bounds_max_y = 0.0;
    float uv0_x = 0.f;
    float uv0_y = 0.f;
    float uv1_x = 1.f;
    float uv1_y = 1.f;
};

int heatmap_collect_view_quads(int zoom, int tile_x, int tile_y, HeatmapTileQuad* out_quads, int max_quads);
