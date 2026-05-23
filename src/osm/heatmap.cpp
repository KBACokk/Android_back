#include "heatmap.hpp"
#include "osm_projection.hpp"
#include "tile_cache.hpp"
#include "tile_texture.hpp"
#include "db/db.hpp"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <future>

void heatmap_palette_rgb(float t, unsigned char& r, unsigned char& g, unsigned char& b);

namespace {
constexpr int kTilePx = 256;
constexpr int kSplatRadiusPx = 20;
constexpr int kMinGenZoom = 10;
constexpr int kMaxGenZoom = 17;

std::filesystem::path g_build_root;
std::mutex g_state_mtx;
HeatmapJobState g_job;
HeatmapCriterion g_criterion = HeatmapCriterion::RSRP;
int g_earfcn = -1;
bool g_layer_enabled = true;
float g_vmin = -110.0f;
float g_vmax = -80.0f;

std::vector<HeatmapSample> g_samples;
std::vector<int> g_earfcns;
HeatmapDbFingerprint g_fp;

std::future<void> g_worker_future;

struct GpuHeatTile { TileTexture tex; };
std::mutex g_gpu_mtx;
std::unordered_map<std::string, GpuHeatTile> g_gpu_tiles;

void heatmap_palette(float t, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a) {
    heatmap_palette_rgb(t, r, g, b);
    a = static_cast<unsigned char>((0.1f + t * 0.5f) * 255.f);
}

float sample_value(const HeatmapSample& s, HeatmapCriterion c) {
    if (c == HeatmapCriterion::RSRQ) return s.rsrq;
    if (c == HeatmapCriterion::RSSI) return s.rssi;
    if (c == HeatmapCriterion::Altitude) return s.altitude;
    return s.rsrp;
}

std::string criterion_tag(HeatmapCriterion c) {
    if (c == HeatmapCriterion::RSRQ) return "rsrq";
    if (c == HeatmapCriterion::RSSI) return "rssi";
    if (c == HeatmapCriterion::Altitude) return "altitude";
    return "rsrp";
}

std::filesystem::path layer_root(HeatmapCriterion c, int earfcn) {
    return g_build_root / criterion_tag(c) / ("e" + std::to_string(earfcn));
}

void compute_value_range(HeatmapCriterion c, int earfcn_filter, float& vmin, float& vmax) {
    if (c == HeatmapCriterion::RSRP || c == HeatmapCriterion::RSSI) {
        vmin = -110.0f; vmax = -80.0f; return; 
    }
    vmin = 1e9f; vmax = -1e9f;
    for (const auto& s : g_samples) {
        if (earfcn_filter >= 0 && s.earfcn != earfcn_filter) continue;
        float v = sample_value(s, c);
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    if (vmin >= vmax) { vmin = 0.0f; vmax = 1.0f; }
}

bool generate_tile_png(HeatmapCriterion c, int earfcn, int z, int tx, int ty, float vmin, float vmax) {
    std::vector<float> density(kTilePx * kTilePx, 0.0f);
    std::vector<float> value_w(kTilePx * kTilePx, 0.0f);
    bool has_points = false;

    for (const auto& s : g_samples) {
        if (earfcn >= 0 && s.earfcn != earfcn) continue;

        double t_x = osm::mercator_x_to_tile_x(s.lon, z) - tx;
        double t_y = osm::mercator_y_to_tile_y(s.merc_y, z) - ty;
        
        int p_x = static_cast<int>(t_x * kTilePx);
        int p_y = static_cast<int>(t_y * kTilePx);

        if (p_x < -kSplatRadiusPx || p_x >= kTilePx + kSplatRadiusPx ||
            p_y < -kSplatRadiusPx || p_y >= kTilePx + kSplatRadiusPx) continue;

        float val = sample_value(s, c);
        has_points = true;

        int x0 = std::max(0, p_x - kSplatRadiusPx);
        int x1 = std::min(kTilePx - 1, p_x + kSplatRadiusPx);
        int y0 = std::max(0, p_y - kSplatRadiusPx);
        int y1 = std::min(kTilePx - 1, p_y + kSplatRadiusPx);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float dx = static_cast<float>(x - p_x);
                float dy = static_cast<float>(y - p_y);
                float dist_sq = dx * dx + dy * dy;
                float r_sq = static_cast<float>(kSplatRadiusPx * kSplatRadiusPx);

                if (dist_sq < r_sq) {
                    float weight = std::exp(-dist_sq / (2.0f * r_sq));
                    int idx = y * kTilePx + x;
                    density[idx] += weight;
                    value_w[idx] += weight * val;
                }
            }
        }
    }

    if (!has_points) return true;

    std::vector<unsigned char> rgba(kTilePx * kTilePx * 4, 0);
    bool any_visible = false;

    for (int i = 0; i < kTilePx * kTilePx; ++i) {
        if (density[i] < 0.01f) continue;
        float avg = value_w[i] / density[i];
        float t = std::clamp((avg - vmin) / (vmax - vmin), 0.0f, 1.0f);

        unsigned char r, g, b, a;
        heatmap_palette(t, r, g, b, a);

        float edge_factor = std::min(1.0f, density[i]);

        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = static_cast<unsigned char>(a * edge_factor);
        any_visible = true;
    }

    if (!any_visible) return true;
    auto out_path = heatmap_tile_png_path(c, earfcn, z, tx, ty);
    std::filesystem::create_directories(out_path.parent_path());
    return stbi_write_png(out_path.string().c_str(), kTilePx, kTilePx, 4, rgba.data(), kTilePx * 4) != 0;
}

void generate_layer(HeatmapCriterion c, int earfcn) {
    {
        std::lock_guard<std::mutex> lk(g_state_mtx);
        g_job.phase = HeatmapJobState::Phase::Generating;
        g_job.progress = 0.0f;
    }

    float vmin, vmax;
    compute_value_range(c, earfcn, vmin, vmax);
    {
        std::lock_guard<std::mutex> lk(g_state_mtx);
        g_vmin = vmin;
        g_vmax = vmax;
    }

    std::vector<std::tuple<int, int, int>> active_tiles;
    for (const auto& s : g_samples) {
        for (int z = kMinGenZoom; z <= kMaxGenZoom; ++z) {
            int tx = static_cast<int>(osm::mercator_x_to_tile_x(s.lon, z));
            int ty = static_cast<int>(osm::mercator_y_to_tile_y(s.merc_y, z));
            auto tile = std::make_tuple(z, tx, ty);
            if (std::find(active_tiles.begin(), active_tiles.end(), tile) == active_tiles.end()) {
                active_tiles.push_back(tile);
            }
        }
    }

    for (size_t i = 0; i < active_tiles.size(); ++i) {
        auto [z, tx, ty] = active_tiles[i];
        generate_tile_png(c, earfcn, z, tx, ty, vmin, vmax);

        std::lock_guard<std::mutex> lk(g_state_mtx);
        g_job.progress = static_cast<float>(i + 1) / active_tiles.size();
    }

    std::lock_guard<std::mutex> lk(g_state_mtx);
    g_job.phase = HeatmapJobState::Phase::Ready;
    g_job.message = "Heatmap ready";
}

void clear_gpu_cache() {
    std::lock_guard<std::mutex> lk(g_gpu_mtx);
    for (auto& e : g_gpu_tiles) e.second.tex.destroy();
    g_gpu_tiles.clear();
}
}

void heatmap_palette_rgb(float t, unsigned char& r, unsigned char& g, unsigned char& b) {
    t = std::clamp(t, 0.0f, 1.0f);
    float stops[5][3] = {
        {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}
    };
    float x = t * 4.0f;
    int seg = std::min(3, static_cast<int>(x));
    float u = x - static_cast<float>(seg);

    r = static_cast<unsigned char>((stops[seg][0] + (stops[seg+1][0] - stops[seg][0]) * u) * 255.f);
    g = static_cast<unsigned char>((stops[seg][1] + (stops[seg+1][1] - stops[seg][1]) * u) * 255.f);
    b = static_cast<unsigned char>((stops[seg][2] + (stops[seg+1][2] - stops[seg][2]) * u) * 255.f);
}

void heatmap_set_build_root(const std::filesystem::path& root) { g_build_root = std::filesystem::absolute(root); }
const std::filesystem::path& heatmap_cache_root() { return g_build_root; }
void heatmap_init() {}
void heatmap_shutdown() { clear_gpu_cache(); if (g_worker_future.valid()) g_worker_future.wait(); }

void heatmap_startup_async() {
    g_worker_future = std::async(std::launch::async, []() {
        PGconn* db = db_connect();
        if (!db) return;
        g_fp = loadHeatmapFingerprintFromDatabase(db);
        g_samples = loadHeatmapSamplesFromDatabase(db);
        PQfinish(db);

        std::vector<int> u; u.push_back(-1);
        for (const auto& s : g_samples) if (s.earfcn >= 0) u.push_back(s.earfcn);
        std::sort(u.begin(), u.end());
        u.erase(std::unique(u.begin(), u.end()), u.end());
        g_earfcns = u;

        generate_layer(g_criterion, g_earfcn);
    });
}

void heatmap_poll() {}

HeatmapCriterion heatmap_get_criterion() { std::lock_guard<std::mutex> lk(g_state_mtx); return g_criterion; }
void heatmap_set_criterion(HeatmapCriterion c) { 
    std::lock_guard<std::mutex> lk(g_state_mtx); 
    if (g_criterion != c) { g_criterion = c; clear_gpu_cache(); generate_layer(c, g_earfcn); }
}

int heatmap_get_earfcn() { std::lock_guard<std::mutex> lk(g_state_mtx); return g_earfcn; }
void heatmap_set_earfcn(int earfcn) { 
    std::lock_guard<std::mutex> lk(g_state_mtx); 
    if (g_earfcn != earfcn) { g_earfcn = earfcn; clear_gpu_cache(); generate_layer(g_criterion, earfcn); }
}

std::vector<int> heatmap_get_available_earfcns() { std::lock_guard<std::mutex> lk(g_state_mtx); return g_earfcns; }
bool heatmap_layer_enabled() { std::lock_guard<std::mutex> lk(g_state_mtx); return g_layer_enabled; }
void heatmap_set_layer_enabled(bool enabled) { std::lock_guard<std::mutex> lk(g_state_mtx); g_layer_enabled = enabled; }
const HeatmapJobState& heatmap_job_state() { std::lock_guard<std::mutex> lk(g_state_mtx); return g_job; }

std::filesystem::path heatmap_tile_png_path(HeatmapCriterion criterion, int earfcn, int zoom, int tile_x, int tile_y) {
    return layer_root(criterion, earfcn) / std::to_string(zoom) / std::to_string(tile_x) / (std::to_string(tile_y) + ".png");
}

bool heatmap_try_get_texture(int zoom, int tile_x, int tile_y, unsigned int& out_gl_tex_id) {
    out_gl_tex_id = 0;
    if (!g_layer_enabled) return false;
    std::string key = criterion_tag(g_criterion) + "/" + std::to_string(g_earfcn) + "/" + std::to_string(zoom) + "/" + std::to_string(tile_x) + "/" + std::to_string(tile_y);

    std::lock_guard<std::mutex> lk(g_gpu_mtx);
    if (g_gpu_tiles.count(key)) { out_gl_tex_id = g_gpu_tiles[key].tex.id; return out_gl_tex_id != 0; }

    auto path = heatmap_tile_png_path(g_criterion, g_earfcn, zoom, tile_x, tile_y);
    std::vector<unsigned char> bytes;
    if (std::filesystem::exists(path) && tile_read_file_bytes(path, bytes) && !bytes.empty()) {
        int w, h, ch;
        unsigned char* pix = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &ch, 4);
        if (pix) {
            std::vector<unsigned char> rgba(pix, pix + w * h * 4);
            stbi_image_free(pix);
            GpuHeatTile slot;
            if (slot.tex.upload_rgba8(rgba, w, h, true)) {
                out_gl_tex_id = slot.tex.id;
                g_gpu_tiles[key] = std::move(slot);
                return true;
            }
        }
    }
    g_gpu_tiles[key] = {};
    return false;
}

void heatmap_get_value_range(float& vmin, float& vmax) {
    std::lock_guard<std::mutex> lk(g_state_mtx);
    vmin = g_vmin;
    vmax = g_vmax;
}

const char* heatmap_criterion_short_name(HeatmapCriterion c) {
    if (c == HeatmapCriterion::RSRQ) return "RSRQ";
    if (c == HeatmapCriterion::RSSI) return "RSSI";
    if (c == HeatmapCriterion::Altitude) return "Altitude";
    return "RSRP";
}

const char* heatmap_criterion_unit(HeatmapCriterion c) {
    if (c == HeatmapCriterion::Altitude) return "m";
    if (c == HeatmapCriterion::RSRQ) return "dB";
    return "dBm";
}

int heatmap_collect_view_quads(int zoom, int tile_x, int tile_y, HeatmapTileQuad* out_quads, int max_quads) {
    if (!out_quads || max_quads <= 0 || !g_layer_enabled) return 0;
    unsigned int tex = 0;
    if (!heatmap_try_get_texture(zoom, tile_x, tile_y, tex) || tex == 0) return 0;

    out_quads[0].tex_id = tex;
    out_quads[0].uv0_x = 0.f; out_quads[0].uv0_y = 0.f;
    out_quads[0].uv1_x = 1.f; out_quads[0].uv1_y = 1.f;
    out_quads[0].bounds_min_x = osm::tile_x_to_mercator_x(tile_x, zoom);
    out_quads[0].bounds_max_x = osm::tile_x_to_mercator_x(tile_x + 1, zoom);
    out_quads[0].bounds_min_y = osm::tile_y_to_mercator_y(tile_y + 1, zoom);
    out_quads[0].bounds_max_y = osm::tile_y_to_mercator_y(tile_y, zoom);
    return 1;
}