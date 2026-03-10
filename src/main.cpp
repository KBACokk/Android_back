#include <zmq.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <fstream>
#include <mutex>
#include <vector>
#include <sstream>

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

struct TelemetryData {
    std::string lat = "-";
    std::string lon = "-";
    std::string alt = "-";
    std::string acc = "-";
    std::string mobile_data = "-";
} current_telemetry;

std::mutex mtx;
std::vector<std::string> log_messages;
bool running = true;

void parseArrayData(const std::string& msg) {

    std::string content = msg;
    if (content.front() == '[') content = content.substr(1);
    if (content.back() == ']') content.pop_back();
    
    std::vector<std::string> values;
    std::stringstream ss(content);
    std::string item;
    bool inQuotes = false;
    std::string currentValue;
    
    for (char c : content) {
        if (c == '"') {
            inQuotes = !inQuotes;
            currentValue += c;
        }
        else if (c == ',' && !inQuotes) {
            values.push_back(currentValue);
            currentValue.clear();
        }
        else {
            currentValue += c;
        }
    }
    if (!currentValue.empty()) {
        values.push_back(currentValue);
    }
    
    for (auto& val : values) {
        size_t start = val.find_first_not_of(" \t");
        if (start != std::string::npos) {
            val = val.substr(start);
        }
        size_t end = val.find_last_not_of(" \t");
        if (end != std::string::npos) {
            val = val.substr(0, end + 1);
        }
    }
    
    if (values.size() >= 6) {
        try {
            std::string netType = values[5];
            if (netType.front() == '"') netType = netType.substr(1);
            if (netType.back() == '"') netType.pop_back();
            
            current_telemetry.lat = values[0];
            current_telemetry.lon = values[1];
            current_telemetry.alt = values[2];
            current_telemetry.acc = values[4];  
            current_telemetry.mobile_data = netType;
            
            std::cout << "Parsed - lat: " << values[0] 
                      << ", lon: " << values[1] 
                      << ", alt: " << values[2]
                      << ", acc: " << values[4]
                      << ", net: " << netType << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing values: " << e.what() << std::endl;
        }
    }
}

void run_server() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.set(zmq::sockopt::rcvtimeo, 500); 

    try {
        socket.bind("tcp://*:7777");
        std::cout << "[Server] Started on port 7777" << std::endl;
    } catch (const zmq::error_t& e) {
        std::cerr << "[Server] Bind error: " << e.what() << std::endl;
        return;
    }

    std::ofstream file("data.json", std::ios::app);

    while (running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);

        if (res) {
            std::string message(static_cast<char *>(request.data()), request.size());
            file << message << "\n";
            file.flush();
            
            {
                std::lock_guard<std::mutex> lock(mtx);
                
                parseArrayData(message);
                
                log_messages.push_back(message);
                if(log_messages.size() > 500) log_messages.erase(log_messages.begin());
            }

            std::string reply_str = "OK";
            socket.send(zmq::buffer(reply_str), zmq::send_flags::none);
        }
    }
}

void run_gui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return;
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    
    SDL_Window* window = SDL_CreateWindow(
        "ZMQ Log Viewer", 
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED,
        1100, 700, 
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) 
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        
        ImGui::SetNextWindowPos(ImVec2(10, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        ImGui::Begin("Current Telemetry");
        
        TelemetryData local_copy;
        {
            std::lock_guard<std::mutex> lock(mtx);
            local_copy = current_telemetry;
        }
        
        ImGui::Columns(2, "telemetry_cols", true);
        
        ImGui::Text("Latitude:"); 
        ImGui::NextColumn(); 
        ImGui::TextColored(ImVec4(1,1,0,1), "%s", local_copy.lat.c_str()); 
        ImGui::NextColumn();
        
        ImGui::Text("Longitude:"); 
        ImGui::NextColumn(); 
        ImGui::TextColored(ImVec4(1,1,0,1), "%s", local_copy.lon.c_str()); 
        ImGui::NextColumn();
        
        ImGui::Text("Altitude:"); 
        ImGui::NextColumn(); 
        ImGui::Text("%s m", local_copy.alt.c_str()); 
        ImGui::NextColumn();
        
        ImGui::Text("Accuracy:"); 
        ImGui::NextColumn(); 
        ImGui::Text("%s m", local_copy.acc.c_str()); 
        ImGui::NextColumn();
        
        ImGui::Separator();
        
        ImGui::Text("Network Type:"); 
        ImGui::NextColumn(); 
        ImGui::TextColored(ImVec4(0,1,1,1), "%s", local_copy.mobile_data.c_str());
        
        ImGui::Columns(1);
        
        if (local_copy.lat != "-") {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0,1,0,1), "✓ Data received");
        }
        
        ImGui::End();
        
        ImGui::SetNextWindowPos(ImVec2(370, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(710, 660), ImGuiCond_FirstUseEver);
        ImGui::Begin("ZMQ Server Log");
        
        if (ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            std::lock_guard<std::mutex> lock(mtx);
            
            ImGui::PushTextWrapPos(0.0f); 
            for (const auto& msg : log_messages) {
                ImGui::TextUnformatted(msg.c_str());
            }
            
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
                
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        
        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    std::cout << "Starting ZMQ Log Viewer..." << std::endl;
    std::cout << "Waiting for data on port 7777..." << std::endl;
    
    std::thread server_thread(run_server);
    run_gui();
    
    if (server_thread.joinable()) {
        running = false;
        server_thread.join();
    }
    
    return 0;
}