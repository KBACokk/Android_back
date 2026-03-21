#include <zmq.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <fstream>
#include <mutex>
#include <vector>
#include <cmath>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

struct SignalHistory {
    std::vector<float> x;
    std::vector<float> y;

    double start_time = -1.0;

    static constexpr int max_points = 200;

    void add_point(float value, double timestamp_ms) {

        if (start_time < 0)
            start_time = timestamp_ms;

        double time_sec = (timestamp_ms - start_time) / 1000.0;
        time_sec = floor(time_sec);

        if (x.size() >= max_points) {
            x.erase(x.begin());
            y.erase(y.begin());
        }

        x.push_back((float)time_sec);
        y.push_back(value);
    }
};

struct CellInfo {
    std::string type;
    int dbm = -140;
    int pci = -1;
    int tac = -1;
};

struct TelemetryData {
    std::string lat = "-";
    std::string lon = "-";
    std::string acc = "-";
    std::string mobile_data = "-";
    float signal = -140;

    std::vector<CellInfo> cells;
    SignalHistory history;
} current_telemetry;

std::mutex mtx;
std::vector<std::string> log_messages;

bool running = true;
int packet_counter = 0;

void parseJsonData(const std::string& msg) {
    try {
        auto j = json::parse(msg);

        current_telemetry.lat = std::to_string(j.value("lat", 0.0));
        current_telemetry.lon = std::to_string(j.value("lon", 0.0));
        current_telemetry.acc = std::to_string(j.value("accuracy", 0.0));
        current_telemetry.mobile_data = j.value("networkType", "-");

        float signal = j.value("signal", -140.0f);
        
        current_telemetry.cells.clear();
        if (j.contains("cells") && j["cells"].is_array()) {
            for (auto& c : j["cells"]) {
                CellInfo cell;
                cell.type = c.value("type", "unknown");
                cell.dbm = c.value("dbm", -140);
                cell.pci = c.value("pci", -1);
                cell.tac = c.value("tac", -1);
                current_telemetry.cells.push_back(cell);
            }
            
            if (!current_telemetry.cells.empty()) {
                signal = (float)current_telemetry.cells[0].dbm;
            }
        }
        
        current_telemetry.signal = signal;
        double timestamp = j.value("timestamp", 0.0);
        current_telemetry.history.add_point(signal, timestamp);

        if (timestamp == 0.0) {
            timestamp = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
        ).count();
}

    } catch (std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
}

void run_server() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.set(zmq::sockopt::rcvtimeo, 500);
    try {
        socket.bind("tcp://*:7777");
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Bind error: " << e.what() << std::endl;
        return;
    }

    std::cout << "Server started on 7777\n";
    std::ofstream file("data.json", std::ios::app);

    while (running) {
        zmq::message_t request;
        if (socket.recv(request, zmq::recv_flags::none)) {
            std::string msg(static_cast<char*>(request.data()), request.size());
            file << msg << std::endl;

            {
                std::lock_guard<std::mutex> lock(mtx);
                parseJsonData(msg);
                log_messages.push_back(msg);
                if (log_messages.size() > 500) log_messages.erase(log_messages.begin());
                packet_counter++;
            }
            socket.send(zmq::buffer("Ok"), zmq::send_flags::none);
        }
    }
}

static int TimeFormatter(double value, char* buff, int size, void* user_data) {
    int total_seconds = (int)value;

    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    if (hours > 0)
        snprintf(buff, size, "%02d:%02d:%02d", hours, minutes, seconds);
    else
        snprintf(buff, size, "%02d:%02d", minutes, seconds);

    return 0;
}

void run_gui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return;

    SDL_Window* window = SDL_CreateWindow(
        "Telemetry backend server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1200, 750, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    glewInit();

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
        // for (const auto& c : cells_copy) {
        //     ImGui::BulletText("%s | %d dBm | PCI:%d | TAC:%d", c.type.c_str(), c.dbm, c.pci, c.tac);
        // }
        ImGui::Separator();
        ImGui::Text("Total Packets: %d", p_count);
        ImGui::End();

        ImGui::Begin("Signal Graph");
{
    std::lock_guard<std::mutex> lock(mtx); 
    if (ImPlot::BeginPlot("Signal History", ImVec2(-1, -1))) {
        
        ImPlot::SetupAxes("Time", "Signal (dBm)", ImPlotAxisFlags_AutoFit, 0); 
        ImPlot::SetupAxisFormat(ImAxis_X1, TimeFormatter, nullptr);

        if (!current_telemetry.history.y.empty()) {

            float min_y = current_telemetry.history.y[0];
            float max_y = current_telemetry.history.y[0];
            
            for (float val : current_telemetry.history.y) {
                if (val < min_y) min_y = val;
                if (val > max_y) max_y = val;
            }


            float range = max_y - min_y;
            if (range < 1.0f) range = 10.0f; 

            float padding = range * 0.15f; 
            ImPlot::SetupAxisLimits(ImAxis_Y1, min_y - padding, max_y + padding, ImGuiCond_Always);
            
            ImPlot::PlotLine("Signal", 
                current_telemetry.history.x.data(), 
                current_telemetry.history.y.data(), 
                (int)current_telemetry.history.x.size());
        } else {
            ImPlot::SetupAxisLimits(ImAxis_Y1, -130, -40, ImGuiCond_Once);
        }

        ImPlot::EndPlot();
    }
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

        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main() {
    std::thread server_thread(run_server);
    run_gui();
    
    running = false;
    if (server_thread.joinable()) server_thread.join();
    
    return 0;
}