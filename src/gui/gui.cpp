#include "gui.hpp"
#include "osm/osm_map.hpp"
#include "osm/types.hpp"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include <string>
#include <cstdio>
#include <iostream>
#include <map>
#include <limits>

static int TimeFormatter(double value, char* buff, int size, void* user_data) {
    int total_seconds = (int)value;
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    if (hours > 0)
        return snprintf(buff, size, "%02d:%02d:%02d", hours, minutes, seconds);
    return snprintf(buff, size, "%02d:%02d", minutes, seconds);
}

void run_gui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        std::cerr << "Для WSL: export DISPLAY=:0 и запустите X-сервер (VcXsrv / GWSL).\n";
        return;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Telemetry backend server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1640, 980, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    glewInit();

    std::cout << "GUI: окно открыто\n";

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        std::string lat, lon, acc, net;
        float sig;
        std::vector<CellInfo> cells_copy;
        int p_count;

        {
            std::lock_guard<std::mutex> lock(mtx);
            lat = current_telemetry.lat;
            lon = current_telemetry.lon;
            acc = current_telemetry.acc;
            net = current_telemetry.mobile_data;
            sig = current_telemetry.signal;
            cells_copy = current_telemetry.cells;
            p_count = packet_counter;
        }

        ImGui::Begin("Current Telemetry");
        ImGui::Text("Lat: %s", lat.c_str());
        ImGui::Text("Lon: %s", lon.c_str());
        ImGui::Text("Accuracy: %s", acc.c_str());
        ImGui::Text("Network: %s", net.c_str());
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Signal: %.1f dBm", sig);
        ImGui::Separator();
        ImGui::Text("Cells:");
        for (const auto& c : cells_copy) {
            ImGui::BulletText("%s | dbm:%d | PCI:%d | TAC:%d | RSRP:%d | RSSI:%d | SINR:%d",
                              c.type.c_str(), c.dbm, c.pci, c.tac, c.rsrp, c.rssi, c.sinr);
        }
        ImGui::Separator();
        ImGui::Text("Total Packets: %d", p_count);
        ImGui::End();

        ImGui::Begin("Signal Graphs");
        {
            std::lock_guard<std::mutex> lock(mtx); 
            auto plot_metric = [](const char* plot_id,
                                  const char* y_label,
                                  const std::map<int, SignalHistory>& series_by_pci,
                                  float default_min,
                                  float default_max,
                                  const char* units,
                                  const char* unique_prefix) {
                if (!ImPlot::BeginPlot(plot_id, ImVec2(-1, 220))) return;

                float min_y = std::numeric_limits<float>::max();
                float max_y = std::numeric_limits<float>::lowest();
                bool has_points = false;

                for (const auto& [pci, history] : series_by_pci) {
                    if (history.y.empty()) continue;
                    has_points = true;

                    for (float val : history.y) {
                        if (val < min_y) min_y = val;
                        if (val > max_y) max_y = val;
                    }
                }

                ImPlot::SetupAxes("Time", y_label, ImPlotAxisFlags_AutoFit, 0);
                ImPlot::SetupAxisFormat(ImAxis_X1, TimeFormatter, nullptr);
                if (has_points) {
                    float range = max_y - min_y;
                    if (range < 1.0f) range = 10.0f;
                    const float padding = range * 0.15f;
                    ImPlot::SetupAxisLimits(ImAxis_Y1, min_y - padding, max_y + padding, ImGuiCond_Always);
                } else {
                    ImPlot::SetupAxisLimits(ImAxis_Y1, default_min, default_max, ImGuiCond_Once);
                }

                for (const auto& [pci, history] : series_by_pci) {
                    if (history.y.empty()) continue;
                    const float last_val = history.y.back();
                    char label[96];
                    std::snprintf(label, sizeof(label), "PCI %d: %.0f %s##%s_%d",
                                  pci, last_val, units, unique_prefix, pci);

                    ImPlot::PlotLine(label, history.x.data(), history.y.data(), (int)history.x.size());
                }
                ImPlot::EndPlot();
            };

            plot_metric("RSRP History by PCI", "RSRP (dBm)", current_telemetry.rsrp_history_by_pci, -140.0f, -40.0f, "dBm", "rsrp");
            plot_metric("RSSI History by PCI", "RSSI (dBm)", current_telemetry.rssi_history_by_pci, -140.0f, -40.0f, "dBm", "rssi");
            plot_metric("SINR History by PCI", "SINR (dB)", current_telemetry.sinr_history_by_pci, -30.0f, 40.0f, "dB", "sinr");
        }
        ImGui::End();    

        ImGui::Begin("Logs");
        if (ImGui::BeginChild("LogScroll")) {
            std::lock_guard<std::mutex> lock(mtx);
            for (const auto& msg : log_messages) {
                ImGui::TextUnformatted(msg.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();

        osm_map_render_window();

        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    osm_map_shutdown();

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}