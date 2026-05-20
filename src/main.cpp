#include "osm/types.hpp"
#include "osm/heatmap.hpp"
#include "db/db.hpp"
#include "server/server.hpp"
#include "gui/gui.hpp"
#include "paths_config.h"

#include <iostream>
#include <thread>

TelemetryData current_telemetry;
std::mutex mtx;
std::vector<std::string> log_messages;
bool running = true;
int packet_counter = 0;
PGconn *con = nullptr;

int main() {
    con = db_connect();

    if (PQstatus(con) != CONNECTION_OK) {
        std::cerr << "\033[31mОШИБКА\033[0m подключения к БД.\n" << PQerrorMessage(con) << "\n";
        PQfinish(con);
        return 1;
    }

    std::cout << "Подключение к PostgreSQL \033[32mуспешно\033[0m\n";
    if (PQsetClientEncoding(con, "UTF8") != 0)
        std::cerr << "Предупреждение: PQsetClientEncoding(UTF8): " << PQerrorMessage(con) << "\n";

    loadHistoryFromDatabase();

    heatmap_set_build_root(HEATMAP_CACHE_ROOT);

    std::thread server_thread(run_server);
    heatmap_startup_async();

    run_gui();

    running = false;
    if (server_thread.joinable())
        server_thread.join();

    heatmap_shutdown();

    if (con) {
        PQfinish(con);
        con = nullptr;
    }
    return 0;
}