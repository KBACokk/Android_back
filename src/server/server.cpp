#include "server.hpp"
#include "../osm/types.hpp"
#include "../db/db.hpp"
#include <zmq.hpp>
#include <iostream>
#include <chrono>

using json = nlohmann::json;

static float primary_signal_from_cell(const json& c) {
    const std::string t = c.value("type", "");
    if (t == "LTE") return static_cast<float>(c.value("rsrp", c.value("dbm", -140.0)));
    if (t == "NR") return static_cast<float>(c.value("ss_rsrp", -140.0));
    if (t == "GSM") return static_cast<float>(c.value("dbm", -140.0));
    return static_cast<float>(c.value("dbm", c.value("rsrp", -140.0)));
}

void parseJsonData(const json& j) {
    current_telemetry.lat = std::to_string(j.value("lat", 0.0));
    current_telemetry.lon = std::to_string(j.value("lon", 0.0));
    current_telemetry.acc = std::to_string(j.value("accuracy", 0.0));
    current_telemetry.mobile_data = j.value("networkType", std::string("-"));

    double timestamp = j.value("timestamp", 0.0);
    if (timestamp <= 0.0) {
        timestamp = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    const float time_sec = current_telemetry.get_or_init_relative_time_sec(timestamp);
    float signal = -140.0f;
    bool found_registered = false;
    current_telemetry.cells.clear();

    if (j.contains("cells") && j["cells"].is_array()) {
        for (const auto& c : j["cells"]) {
            CellInfo cell;
            cell.type = c.value("type", "unknown");
            const std::string& t = cell.type;
            
            if (t == "LTE") {
                cell.pci = c.value("pci", -1);
                cell.tac = c.value("tac", -1);
                cell.dbm = c.value("dbm", -140);
                cell.rsrp = c.value("rsrp", cell.dbm);
                cell.rssi = c.value("rssi", cell.dbm);
                cell.sinr = c.value("rssnr", -140);
            } else if (t == "NR") {
                cell.pci = c.value("pci", -1);
                cell.tac = c.value("tac", -1);
                cell.dbm = static_cast<int>(c.value("ss_rsrp", -140.0));
                cell.rsrp = static_cast<int>(c.value("ss_rsrp", -140.0));
                cell.rssi = static_cast<int>(c.value("dbm", cell.dbm));
                cell.sinr = static_cast<int>(c.value("ss_sinr", -140.0));
            } else if (t == "GSM") {
                cell.pci = -1;
                cell.tac = c.value("lac", -1);
                cell.dbm = c.value("dbm", -140);
                cell.rsrp = cell.dbm;
                cell.rssi = c.value("rssi", cell.dbm);
                cell.sinr = -140;
            } else {
                cell.pci = c.value("pci", -1);
                cell.tac = c.value("tac", -1);
                cell.dbm = c.value("dbm", -140);
                cell.rsrp = c.value("rsrp", cell.dbm);
                cell.rssi = c.value("rssi", cell.dbm);
                cell.sinr = c.value("sinr", c.value("rssnr", -140));
            }
            current_telemetry.cells.push_back(cell);
            if (cell.pci >= 0) {
                current_telemetry.rsrp_history_by_pci[cell.pci].add_point(time_sec, static_cast<float>(cell.rsrp));
                current_telemetry.rssi_history_by_pci[cell.pci].add_point(time_sec, static_cast<float>(cell.rssi));
                current_telemetry.sinr_history_by_pci[cell.pci].add_point(time_sec, static_cast<float>(cell.sinr));
            }

            if (!found_registered && c.value("registered", false)) {
                signal = primary_signal_from_cell(c);
                found_registered = true;
            }
        }
        if (!found_registered && !j["cells"].empty())
            signal = primary_signal_from_cell(j["cells"][0]);
    }

    current_telemetry.signal = signal;
    current_telemetry.history.add_point(time_sec, signal);
}

void run_server() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.set(zmq::sockopt::rcvtimeo, 3000); 
    
    try {
        socket.bind("tcp://*:7777");
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Bind error: " << e.what() << std::endl;
        return;
    }

    std::cout << "Server started on 7777 (Waiting for batch uploads)\n";

    while (running) {
        zmq::message_t request;
        if (!socket.recv(request, zmq::recv_flags::none)) continue;

        socket.send(zmq::buffer("Ok"), zmq::send_flags::none); 

        std::string msg(static_cast<char*>(request.data()), request.size());
        std::string clean = msg;
        size_t brace = msg.find('{');
        if (brace != std::string::npos) clean = msg.substr(brace);

        try {
            json j = json::parse(clean);

            if (j.contains("type") && j["type"] == "batch_upload") {
                int count = 0;
                for (const auto& entry : j["data"]) {
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        parseJsonData(entry); 
                    }
                    saveToDatabase(entry); 
                    count++;
                }
                
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    log_messages.push_back("Received batch: " + std::to_string(count) + " items");
                    packet_counter++; 
                }
            } else {
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    parseJsonData(j);
                }
                saveToDatabase(j);
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    log_messages.push_back("Received single packet");
                    packet_counter++;
                }
            }

            std::lock_guard<std::mutex> lock(mtx);
            if (log_messages.size() > 1000) log_messages.erase(log_messages.begin());

        } catch (const std::exception& e) {
            std::cerr << "JSON Parsing error: " << e.what() << std::endl;
        }
    }
}