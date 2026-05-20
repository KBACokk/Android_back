#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <cmath>
#include <map>

struct SignalHistory {
    std::vector<float> x;
    std::vector<float> y;

    static constexpr int max_points = 500;

    void add_point(float time_sec, float value) {
        if (x.size() >= max_points) {
            x.erase(x.begin());
            y.erase(y.begin());
        }
        x.push_back(time_sec);
        y.push_back(value);
    }
};

struct CellInfo {
    std::string type;
    int dbm = -140;
    int pci = -1;
    int tac = -1;
    int rsrp = -140;
    int rssi = -140;
    int sinr = -140;
};

struct TelemetryData {
    std::string lat = "-";
    std::string lon = "-";
    std::string acc = "-";
    std::string mobile_data = "-";
    float signal = -140;
    double history_start_time = -1.0;

    std::vector<CellInfo> cells;
    std::vector<double> map_points_lon;
    std::vector<double> map_points_merc_y;

    SignalHistory history;
    std::map<int, SignalHistory> rsrp_history_by_pci;
    std::map<int, SignalHistory> rssi_history_by_pci;
    std::map<int, SignalHistory> sinr_history_by_pci;

    float get_or_init_relative_time_sec(double timestamp_ms) {
        if (history_start_time < 0.0)
            history_start_time = timestamp_ms;
        const double time_sec = std::floor((timestamp_ms - history_start_time) / 1000.0);
        return static_cast<float>(time_sec);
    }
};

extern TelemetryData current_telemetry;
extern std::mutex mtx;
extern std::vector<std::string> log_messages;
extern bool running;
extern int packet_counter;