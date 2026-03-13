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
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

struct SignalHistory {
    std::vector<float> x;
    std::vector<float> y;
    float current_step = 0;

    static constexpr int max_points = 200;

    void add_point(float value) {
        if (x.size() >= max_points) {
            x.erase(x.begin());
            y.erase(y.begin());
        }

        x.push_back(current_step++);
        y.push_back(value);
    }
};

struct TelemetryData {
    std::string lat="-";
    std::string lon="-";
    std::string alt="-";
    std::string acc="-";
    std::string mobile_data="-";

    float signal = -140;

    SignalHistory history;
} current_telemetry;

std::mutex mtx;
std::vector<std::string> log_messages;

bool running=true;

int packet_counter=0;

void parseArrayData(const std::string& msg)
{
    std::string content = msg;

    if(content.front()=='[') content = content.substr(1);
    if(content.back()==']') content.pop_back();

    std::vector<std::string> values;

    bool inQuotes=false;
    std::string current;

    for(char c:content)
    {
        if(c=='"')
        {
            inQuotes=!inQuotes;
        }
        else if(c==',' && !inQuotes)
        {
            values.push_back(current);
            current.clear();
        }
        else
        {
            current+=c;
        }
    }

    if(!current.empty())
        values.push_back(current);

    if(values.size() >= 6)
    {
        std::string netType = values[5];

        if(netType.front()=='"') netType=netType.substr(1);
        if(netType.back()=='"') netType.pop_back();

        current_telemetry.lat = values[0];
        current_telemetry.lon = values[1];
        current_telemetry.alt = values[2];

        current_telemetry.mobile_data = netType;
        current_telemetry.acc = "-";

        float signal = -140;

        try
        {
            signal = std::stof(values[4]);
        }
        catch(...)
        {
        }

        current_telemetry.signal = signal;
        current_telemetry.history.add_point(signal);
    }
}



void run_server()
{
    zmq::context_t context(1);
    zmq::socket_t socket(context,zmq::socket_type::rep);

    socket.set(zmq::sockopt::rcvtimeo,500);

    try{
        socket.bind("tcp://*:7777");
        std::cout<<"Server started on 7777\n";
    }
    catch(...)
    {
        return;
    }

    std::ofstream file("data.json",std::ios::app);

    while(running)
    {
        zmq::message_t request;

        if(socket.recv(request,zmq::recv_flags::none))
        {
            std::string msg(
                static_cast<char*>(request.data()),
                request.size()
            );

            file<<msg<<"\n";

            {
                std::lock_guard<std::mutex> lock(mtx);

                parseArrayData(msg);

                log_messages.push_back(msg);

                if(log_messages.size()>500)
                    log_messages.erase(log_messages.begin());
            }

            packet_counter++;

            socket.send(zmq::buffer("OK"),zmq::send_flags::none);
        }
    }
}



void run_gui()
{
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)
        return;

    SDL_Window* window=SDL_CreateWindow(
        "Telemetry backend server",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1200,
        750,
        SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context=SDL_GL_CreateContext(window);

    glewInit();

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window,gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");



    while(running)
    {
        SDL_Event event;

        while(SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if(event.type==SDL_QUIT)
                running=false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();



        TelemetryData data_copy;

        {
            std::lock_guard<std::mutex> lock(mtx);
            data_copy=current_telemetry;
        }



        ImGui::Begin("Current Telemetry");

        ImGui::Columns(2);

        ImGui::Text("Latitude"); ImGui::NextColumn();
        ImGui::Text("%s",data_copy.lat.c_str()); ImGui::NextColumn();

        ImGui::Text("Longitude"); ImGui::NextColumn();
        ImGui::Text("%s",data_copy.lon.c_str()); ImGui::NextColumn();

        ImGui::Text("Altitude"); ImGui::NextColumn();
        ImGui::Text("%s",data_copy.alt.c_str()); ImGui::NextColumn();

        ImGui::Text("Accuracy"); ImGui::NextColumn();
        ImGui::Text("%s",data_copy.acc.c_str()); ImGui::NextColumn();

        ImGui::Text("Network"); ImGui::NextColumn();
        ImGui::Text("%s",data_copy.mobile_data.c_str());

        ImGui::Columns(1);

        ImGui::Separator();

        ImGui::Text("Packets received: %d",packet_counter);

        ImGui::End();



        ImGui::Begin(" Graph");

        {
            std::lock_guard<std::mutex> lock(mtx);

            if (ImPlot::BeginPlot("Signal History", ImVec2(-1, 300)))
            {
                ImPlot::SetupAxes("Samples", "Signal (dBm)");

                int count = current_telemetry.history.x.size();

                if (count > 0)
                {
                    float* xs = current_telemetry.history.x.data();
                    float* ys = current_telemetry.history.y.data();

                    float x_max = xs[count - 1];
                    float x_min = x_max - 100.0f;
                    if (x_min < 0) x_min = 0;

                    float y_min = ys[0];
                    float y_max = ys[0];

                    for (int i = 1; i < count; i++)
                    {
                        if (ys[i] < y_min) y_min = ys[i];
                        if (ys[i] > y_max) y_max = ys[i];
                    }

                    y_min -= 5;
                    y_max += 5;

                    ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImGuiCond_Always);

                    ImPlot::PlotLine(
                        "Signal",
                        xs,
                        ys,
                        count
                    );
                }

                ImPlot::EndPlot();
            }
        }

        ImGui::End();



        ImGui::Begin(" Logs");

        if(ImGui::BeginChild("log"))
        {
            std::lock_guard<std::mutex> lock(mtx);

            for(const auto& msg:log_messages)
                ImGui::TextUnformatted(msg.c_str());

            if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        ImGui::End();



        ImGui::Render();

        glViewport(0,0,1200,750);

        glClearColor(0.1f,0.12f,0.15f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }



    ImPlot::DestroyContext();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_Quit();
}



int main()
{
    std::thread server(run_server);

    run_gui();

    running=false;

    server.join();

    return 0;
}