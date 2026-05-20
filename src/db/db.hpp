#pragma once

#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include <cstdint>
#include <vector>

#include "../osm/heatmap.hpp"

#define DB_HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "telemetry"
#define DB_USER "postgres"
#define DB_USER_PASSWORD "kba_login"

extern PGconn *con;

const char* db_connection_info();
PGconn* db_connect();

void saveToDatabase(const nlohmann::json& j);
void loadHistoryFromDatabase();

struct HeatmapDbFingerprint {
    int point_count = 0;
    long long max_timestamp = 0;
};

HeatmapDbFingerprint loadHeatmapFingerprintFromDatabase(PGconn* db);
std::vector<HeatmapSample> loadHeatmapSamplesFromDatabase(PGconn* db);
