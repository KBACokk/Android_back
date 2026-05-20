#include "osm_map.hpp"
#include "heatmap.hpp"
#include "osm_projection.hpp"
#include "tile_loader.hpp"
#include "tile_paths.hpp"
#include "tile_texture.hpp"
#include "types.hpp"

#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace {

struct GpuTile {
    TileTexture tex;
    bool failed = false;
};

std::unordered_map<std::string, GpuTile> g_gpu_tiles;
std::unordered_set<std::string> g_pending;

std::uint64_t g_view_generation = 1;
int g_last_zoom = -1;
bool g_inited = false;

double MercatorXToTileX(double mercatorX, int zoom) {
    return (0.5 + mercatorX / 360.0) * (1 << zoom);
}

double MercatorYToTileY(double mercatorY, int zoom) {
    return (0.5 - mercatorY / 360.0) * (1 << zoom);
}

double TileXToMercatorX(int tileX, int zoom) {
    return (tileX / static_cast<double>(1 << zoom) - 0.5) * 360.0;
}

double TileYToMercatorY(int tileY, int zoom) {
    return (0.5 - tileY / static_cast<double>(1 << zoom)) * 360.0;
}

int CalculateZoom(const ImPlotRect& limits) {
    double dx = limits.X.Max - limits.X.Min;
    if (dx > 90.0) return 3;
    if (dx > 45.0) return 4;
    if (dx > 22.5) return 5;
    if (dx > 11.2) return 6;
    if (dx > 5.6)  return 7;
    if (dx > 2.8)  return 8;
    if (dx > 1.4)  return 9;
    if (dx > 0.7)  return 10;
    if (dx > 0.35) return 11;
    if (dx > 0.17) return 12;
    if (dx > 0.08) return 13;
    if (dx > 0.04) return 14;
    if (dx > 0.02) return 15;
    if (dx > 0.01) return 16;
    return 17;
}

void drain_loader(std::uint64_t current_generation) {
    for (;;) {
        std::string key;
        int z, x, y, iw, ih;
        std::vector<unsigned char> rgba;
        std::uint64_t gen;
        bool ok;
        
        if (!tile_loader_try_pop_completed_rgba(key, z, x, y, rgba, iw, ih, gen, ok)) break;
        if (gen != current_generation) continue;
        
        g_pending.erase(key);
        GpuTile& slot = g_gpu_tiles[key];
        
        if (!ok || rgba.empty()) {
            slot.failed = true;
            continue;
        }
        
        slot.failed = false;
        if (!slot.tex.upload_rgba8(rgba, iw, ih)) {
            slot.failed = true;
        }
    }
}

void request_tile(int z, int tx, int ty, std::uint64_t gen) {
    const std::string key = tile_key(z, tx, ty);
    if (g_gpu_tiles.count(key) || g_pending.count(key)) return;
    
    g_pending.insert(key);
    tile_loader_enqueue(z, tx, ty, gen);
}

void purge_stale_tiles(const std::unordered_set<std::string>& keep) {
    for (auto it = g_gpu_tiles.begin(); it != g_gpu_tiles.end();) {
        if (keep.find(it->first) == keep.end()) {
            it ->second.tex.destroy();
            it = g_gpu_tiles.erase(it);
        } else {
            ++it;
        }
    }
}

void draw_tile_placeholder(const ImPlotPoint& pmin, const ImPlotPoint& pmax) {
    ImPlot::PushPlotClipRect();
    ImDrawList* dl = ImPlot::GetPlotDrawList();
    ImVec2 c0 = ImPlot::PlotToPixels(pmin.x, pmin.y);
    ImVec2 c1 = ImPlot::PlotToPixels(pmax.x, pmax.y);
    dl->AddRectFilled(c0, c1, IM_COL32(40, 44, 52, 200));
    ImPlot::PopPlotClipRect();
}

}

void osm_map_init() {
    if (g_inited) return;
    tile_loader_global_init();
    tile_loader_start();
    g_inited = true;
}

void osm_map_shutdown() {
    if (!g_inited) return;
    heatmap_shutdown();
    tile_loader_stop();
    tile_loader_global_cleanup();
    for (auto& e : g_gpu_tiles) e.second.tex.destroy();
    g_gpu_tiles.clear();
    g_pending.clear();
    g_inited = false;
}

void osm_map_render_window() {
    if (!g_inited) osm_map_init();

    ImGui::SetNextWindowSize(ImVec2(700, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map OSM (ImPlot)", nullptr)) {
        ImGui::End();
        return;
    }

    static bool follow_telemetry = false;
    static int criterion_idx = 0;
    static int earfcn_idx = 0;

    const char* criterion_names[] = {"RSRP", "RSRQ", "RSSI", "Altitude"};
    if (ImGui::Combo("Heatmap criterion", &criterion_idx, criterion_names, IM_ARRAYSIZE(criterion_names))) {
        heatmap_set_criterion(static_cast<HeatmapCriterion>(criterion_idx));
    }

    static std::vector<int> earfcn_vals;
    static std::vector<std::string> earfcn_labels;
    static std::vector<const char*> earfcn_items;
    const std::vector<int> earfcns = heatmap_get_available_earfcns();
    if (!earfcns.empty()) {
        if (earfcn_vals != earfcns) {
            earfcn_vals = earfcns;
            earfcn_labels.clear();
            earfcn_items.clear();
            for (int e : earfcn_vals) {
                if (e < 0) earfcn_labels.emplace_back("All EARFCN");
                else earfcn_labels.emplace_back("EARFCN " + std::to_string(e));
            }
            for (const auto& s : earfcn_labels) earfcn_items.push_back(s.c_str());
        }
        if (earfcn_idx >= static_cast<int>(earfcn_items.size())) earfcn_idx = 0;
        if (ImGui::Combo("EARFCN filter", &earfcn_idx, earfcn_items.data(),
                         static_cast<int>(earfcn_items.size()))) {
            heatmap_set_earfcn(earfcn_vals[static_cast<std::size_t>(earfcn_idx)]);
        }
    }

    bool show_heat = heatmap_layer_enabled();
    if (ImGui::Checkbox("Show heatmap layer", &show_heat)) heatmap_set_layer_enabled(show_heat);

    const HeatmapJobState& hj = heatmap_job_state();
    if (hj.phase == HeatmapJobState::Phase::Generating) {
        ImGui::Text("Heatmap: %s (%.0f%%)", hj.message.c_str(), hj.progress * 100.0f);
        ImGui::ProgressBar(hj.progress);
    } else {
        ImGui::TextDisabled("Heatmap: %s", hj.message.c_str());
    }

    heatmap_poll();

    double center_lon = 82.94, center_lat = 55.01;
    {
        std::lock_guard<std::mutex> lk(mtx);
        try {
            if (current_telemetry.lon != "-" && current_telemetry.lat != "-") {
                center_lon = std::stod(current_telemetry.lon);
                center_lat = std::stod(current_telemetry.lat);
            }
        } catch (...) {}
    }

    const double merc_y = osm::lat_deg_to_mercator_y(center_lat);

    if (ImPlot::BeginPlot("##osm_plot", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("Longitude", "Mercator's Y", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        
        if (follow_telemetry) {
            double span = 0.02; 
            ImPlot::SetupAxesLimits(center_lon - span, center_lon + span, merc_y - span, merc_y + span, ImPlotCond_Always);
        }
        ImPlot::SetupFinish();

        ImPlotRect limits = ImPlot::GetPlotLimits();
        int zoom = CalculateZoom(limits);

        if (zoom != g_last_zoom) {
            g_last_zoom = zoom;
            g_view_generation++;
            tile_loader_clear_queue();
            g_pending.clear();
        }

        int minX = static_cast<int>(std::floor(MercatorXToTileX(limits.X.Min, zoom)));
        int maxX = static_cast<int>(std::floor(MercatorXToTileX(limits.X.Max, zoom)));

        int tY_top = static_cast<int>(std::floor(MercatorYToTileY(limits.Y.Max, zoom)));
        int tY_bottom = static_cast<int>(std::floor(MercatorYToTileY(limits.Y.Min, zoom)));

        int minY = std::min(tY_top, tY_bottom);
        int maxY = std::max(tY_top, tY_bottom);

        int maxIdx = (1 << zoom) - 1;
        minX = std::max(0, minX); maxX = std::min(maxIdx, maxX);
        minY = std::max(0, minY); maxY = std::min(maxIdx, maxY);

        std::unordered_set<std::string> visible_keys;

        for (int ty = minY; ty <= maxY; ++ty) {
            for (int tx = minX; tx <= maxX; ++tx) {
                const std::string key = tile_key(zoom, tx, ty);
                visible_keys.insert(key);
                request_tile(zoom, tx, ty, g_view_generation);

                ImPlotPoint p_min{ TileXToMercatorX(tx, zoom),     TileYToMercatorY(ty + 1, zoom) };
                
                ImPlotPoint p_max{ TileXToMercatorX(tx + 1, zoom), TileYToMercatorY(ty, zoom) };

                auto it = g_gpu_tiles.find(key);
                if (it != g_gpu_tiles.end() && it->second.tex.id != 0) {
                    ImPlot::PlotImage(("##" + key).c_str(), 
                                    (ImTextureID)(intptr_t)it->second.tex.id, 
                                    p_min, p_max);
                } else {
                    draw_tile_placeholder(p_min, p_max);
                }

                if (heatmap_layer_enabled()) {
                    HeatmapTileQuad heat_quads[32];
                    const int n_heat =
                        heatmap_collect_view_quads(zoom, tx, ty, heat_quads, IM_ARRAYSIZE(heat_quads));
                    for (int hi = 0; hi < n_heat; ++hi) {
                        const HeatmapTileQuad& hq = heat_quads[hi];
                        if (hq.tex_id == 0) continue;
                        ImPlotPoint h_min{hq.bounds_min_x, hq.bounds_min_y};
                        ImPlotPoint h_max{hq.bounds_max_x, hq.bounds_max_y};
                        ImPlot::PlotImage(("##heat_" + key + "_" + std::to_string(hi)).c_str(),
                                          (ImTextureID)(intptr_t)hq.tex_id, h_min, h_max,
                                          ImVec2(hq.uv0_x, hq.uv0_y), ImVec2(hq.uv1_x, hq.uv1_y));
                    }
                }
            }
        }

        drain_loader(g_view_generation);
        purge_stale_tiles(visible_keys);

        std::vector<double> db_lon, db_merc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            db_lon = current_telemetry.map_points_lon;
            db_merc = current_telemetry.map_points_merc_y;
        }

        if (!db_lon.empty()) {
            ImPlot::PlotScatter("All metrics", db_lon.data(), db_merc.data(), (int)db_lon.size());
        }

        ImPlot::EndPlot();
    }
    ImGui::End();
}