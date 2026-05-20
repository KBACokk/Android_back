#include "db/db.hpp"
#include "../osm/osm_projection.hpp"
// #include "osm/osm_projection.hpp
#include "../osm/types.hpp"
#include <iostream>
#include <optional>
#include <chrono>
#include <cmath>

using json = nlohmann::json;

static constexpr int OverLot = 2147483647;

const char* db_connection_info() {
    static const char info[] =
        "host=" DB_HOST " port=" DB_PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD;
    return info;
}

PGconn* db_connect() { return PQconnectdb(db_connection_info()); }

static bool pq_command_ok(PGresult* r) {
    const auto s = PQresultStatus(r);
    return s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK;
}

static void db_rollback(PGconn* c) {
    PGresult* r = PQexec(c, "ROLLBACK");
    PQclear(r);
}

struct PqParamBuf {
    std::vector<std::optional<std::string>> slots;

    void add_null() { 
        slots.push_back(std::nullopt);
    }
    void add_str(std::string s) {
        slots.push_back(std::move(s));
    }
    void add_bool(bool b) {
        add_str(b ? "true" : "false");
    }

    void add_json_int(const json& j, const char* key, bool null_if_sentinel = true) {
        if (!j.contains(key) || j[key].is_null()) {
            add_null();
            return;
        }
        int v = 0;
        if (j[key].is_number_integer()) {
            v = static_cast<int>(j[key].get<long long>());
        }
        else if (j[key].is_number_float())  {
            v = static_cast<int>(j[key].get<double>());
        }
        else { 
            add_null();
            return;
        }
        
        if (null_if_sentinel && v == OverLot) {
            add_null();
            return;
        }
        add_str(std::to_string(v));
    }

    void add_json_int_allow_neg(const json& j, const char* key, bool null_if_sentinel = true) {
        if (!j.contains(key) || j[key].is_null()) { 
            add_null(); return;
        }
        int v = 0;
        if (j[key].is_number_integer()) {
            v = static_cast<int>(j[key].get<long long>());
        }
        else if (j[key].is_number_float()) {
            v = static_cast<int>(j[key].get<double>());
        }
        else {
            add_null();
            return;
        }

        if (null_if_sentinel && v == OverLot) {
            add_null();
            return;
        }

        if (v < 0) {
            add_null();
            return;
        }
        add_str(std::to_string(v));
    }

    void add_json_bigint(const json& j, const char* key) {
        if (!j.contains(key) || j[key].is_null() || !j[key].is_number()) {
            add_null();
            return;
        }

        long long v;

        if (j[key].is_number_integer()) {
            v = j[key].get<long long>();
        } 
        else {
            v = static_cast<long long>(j[key].get<double>());
        }

        if (v == static_cast<long long>(OverLot)) {
            add_null();
        }
        else { 
            add_str(std::to_string(v));
        }
    }

    void add_json_text(const json& j, const char* key) {
        if (!j.contains(key) || j[key].is_null()) {
            add_str("");
        }
        else if (j[key].is_string()) {
            add_str(j[key].get<std::string>());
        }
        else { 
            add_str(j[key].dump());
        }
    }

    std::vector<const char*> ptr_array() const {
        std::vector<const char*> out;
        out.reserve(slots.size());
        for (const auto& s : slots) {
            if (s.has_value()) out.push_back(s->c_str());
            else out.push_back(nullptr);
        }
        return out;
    }

    int count() const { 
        return static_cast<int>(slots.size());
    }
};

static bool insert_cell_info_lte(PGconn* c, int measurement_id, const json& cell) {
    PqParamBuf p;
    p.add_str(std::to_string(measurement_id));
    p.add_bool(cell.value("registered", false));
    p.add_json_text(cell, "mcc");
    p.add_json_text(cell, "mnc");
    p.add_json_int_allow_neg(cell, "pci", true);
    p.add_json_int(cell, "tac", true);
    p.add_json_int(cell, "earfcn", true);
    p.add_json_int(cell, "band", true);
    p.add_json_int(cell, "dbm", true);
    p.add_json_int(cell, "asu", true);
    p.add_json_int(cell, "cqi", true);
    p.add_json_int(cell, "rsrp", true);
    p.add_json_int(cell, "rsrq", true);
    p.add_json_int(cell, "rssi", true);
    p.add_json_int(cell, "rssnr", true);
    p.add_json_int(cell, "ta", true);

    static const char* q =
        "INSERT INTO cell_info_lte ("
        "measurement_id, is_registered, mcc, mnc, pci, tac, earfcn, band, "
        "dbm, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance"
        ") VALUES ("
        "$1::integer, $2::boolean, $3::text, $4::text, $5::integer, $6::integer, $7::integer, $8::integer, "
        "$9::integer, $10::integer, $11::integer, $12::integer, $13::integer, $14::integer, $15::integer, $16::integer"
        ")";

    const auto pv = p.ptr_array();
    PGresult* r = PQexecParams(c, q, p.count(), nullptr, pv.data(), nullptr, nullptr, 0);
    const bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) { 
        std::cerr << "\033[31mБД LTE\033[0m: " << PQresultErrorMessage(r) << "\n";
    }
    PQclear(r);
    return ok;
}

static bool insert_cell_info_gsm(PGconn* c, int measurement_id, const json& cell) {
    PqParamBuf p;
    p.add_str(std::to_string(measurement_id));
    p.add_bool(cell.value("registered", false));
    p.add_json_text(cell, "mcc");
    p.add_json_text(cell, "mnc");
    p.add_json_int(cell, "lac", true);
    p.add_json_int(cell, "bsic", true);
    p.add_json_int(cell, "arfcn", true);
    p.add_json_int(cell, "psc", true);
    p.add_json_int(cell, "dbm", true);
    p.add_json_int(cell, "rssi", true);
    p.add_json_int(cell, "ta", true);

    static const char* q =
        "INSERT INTO cell_info_gsm ("
        "measurement_id, is_registered, mcc, mnc, lac, bsic, arfcn, psc, dbm, rssi, timing_advance"
        ") VALUES ("
        "$1::integer, $2::boolean, $3::text, $4::text, $5::integer, $6::integer, $7::integer, $8::integer, "
        "$9::integer, $10::integer, $11::integer"
        ")";

    const auto pv = p.ptr_array();
    PGresult* r = PQexecParams(c, q, p.count(), nullptr, pv.data(), nullptr, nullptr, 0);
    const bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok)  {
        std::cerr << "\033[31mБД GSM\033[0m: " << PQresultErrorMessage(r) << "\n";
    }
    PQclear(r);
    return ok;
}

static bool insert_cell_info_nr(PGconn* c, int measurement_id, const json& cell) {
    PqParamBuf p;
    p.add_str(std::to_string(measurement_id));
    p.add_bool(cell.value("registered", false));
    p.add_json_text(cell, "mcc");
    p.add_json_text(cell, "mnc");
    p.add_json_int_allow_neg(cell, "pci", true);
    p.add_json_int(cell, "tac", true);
    p.add_json_bigint(cell, "nci");
    p.add_json_int(cell, "nrarfcn", true);
    p.add_json_int(cell, "band", true);
    p.add_json_int(cell, "ss_rsrp", true);
    p.add_json_int(cell, "ss_rsrq", true);
    p.add_json_int(cell, "ss_sinr", true);
    p.add_json_int(cell, "ta", true);

    static const char* q =
        "INSERT INTO cell_info_nr ("
        "measurement_id, is_registered, mcc, mnc, pci, tac, nci, nrarfcn, band, "
        "ss_rsrp, ss_rsrq, ss_sinr, timing_advance"
        ") VALUES ("
        "$1::integer, $2::boolean, $3::text, $4::text, $5::integer, $6::integer, $7::bigint, $8::integer, $9::integer, "
        "$10::integer, $11::integer, $12::integer, $13::integer"
        ")";

    const auto pv = p.ptr_array();
    PGresult* r = PQexecParams(c, q, p.count(), nullptr, pv.data(), nullptr, nullptr, 0);
    const bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) {
        std::cerr << "\033[31mБД NR\033[0m: " << PQresultErrorMessage(r) << "\n";
    }
    PQclear(r);
    return ok;
}

void saveToDatabase(const json& j) {
    if (!con || PQstatus(con) != CONNECTION_OK) return;

    PGresult* tx = PQexec(con, "BEGIN");
    if (!pq_command_ok(tx)) {
        std::cerr << "\033[31mБД BEGIN\033[0m: " << PQerrorMessage(con) << "\n";
        PQclear(tx);
        return;
    }
    PQclear(tx);

    long long ts_ms = static_cast<long long>(j.value("timestamp", 0.0));
    if (ts_ms <= 0) {
        ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
    }

    const double json_lat = j.value("lat", 0.0);
    const double json_lon = j.value("lon", 0.0);
    std::string s_lat = std::to_string(static_cast<float>(json_lat));
    std::string s_lon = std::to_string(static_cast<float>(json_lon));
    std::string s_acc = std::to_string(static_cast<float>(j.value("accuracy", 0.0)));
    std::string s_ts = std::to_string(ts_ms);
    std::string s_net = j.value("networkType", std::string(""));

    const char* mparams[] = { s_lat.c_str(), s_lon.c_str(), s_acc.c_str(), s_ts.c_str(), s_net.c_str() };

    PGresult* r = PQexecParams(
        con,
        "INSERT INTO measurements (lat, lon, accuracy, timestamp, network_type) "
        "VALUES ($1::real, $2::real, $3::real, $4::bigint, $5::text) "
        "ON CONFLICT DO NOTHING RETURNING id", 
        5, nullptr, mparams, nullptr, nullptr, 0);

    if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
        PQclear(r);
        PGresult* cm = PQexec(con, "COMMIT"); 
        PQclear(cm);
        return; 
    }

    const int measurement_id = std::atoi(PQgetvalue(r, 0, 0));
    PQclear(r);

    if (j.contains("cells") && j["cells"].is_array()) {
        for (const auto& cell : j["cells"]) {
            const std::string t = cell.value("type", "");
            bool ok = true;
            if (t == "LTE") {
                ok = insert_cell_info_lte(con, measurement_id, cell);
            }
            else if (t == "GSM") {
                ok = insert_cell_info_gsm(con, measurement_id, cell);
            }
            else if (t == "NR") {
                ok = insert_cell_info_nr(con, measurement_id, cell);
            }
            if (!ok) {
                db_rollback(con);
                return;
            }
        }
    }

    PGresult* cm = PQexec(con, "COMMIT");
    const bool commit_ok = pq_command_ok(cm);
    if (!commit_ok)
        std::cerr << "\033[31mБД COMMIT\033[0m: " << PQresultErrorMessage(cm) << "\n";
    PQclear(cm);

    if (commit_ok && std::isfinite(json_lat) && std::isfinite(json_lon) && json_lon >= -180.0 && json_lon <= 180.0 && json_lat >= -90.0 && json_lat <= 90.0) {
        std::lock_guard<std::mutex> lock(mtx);
        current_telemetry.map_points_lon.push_back(json_lon);
        current_telemetry.map_points_merc_y.push_back(osm::lat_deg_to_mercator_y(json_lat));
    }
}

void loadHistoryFromDatabase() {
    if (!con || PQstatus(con) != CONNECTION_OK) return;

    const char* query =
        "SELECT m.timestamp, m.lat, m.lon, "
        "COALESCE(lte.rsrp, nr.ss_rsrp, gsm.dbm, lte.dbm, -140) AS signal_val "
        "FROM measurements m "
        "LEFT JOIN cell_info_lte lte ON m.id = lte.measurement_id AND lte.is_registered = true "
        "LEFT JOIN cell_info_nr  nr  ON m.id = nr.measurement_id  AND nr.is_registered = true "
        "LEFT JOIN cell_info_gsm gsm ON m.id = gsm.measurement_id AND gsm.is_registered = true "
        "ORDER BY m.timestamp DESC LIMIT 40000";

    PGresult* res = PQexec(con, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Ошибка загрузки истории: " << PQresultErrorMessage(res) << "\n";
        PQclear(res);
        return;
    }

    const int rows = PQntuples(res);
    std::vector<double> ts_vec;
    std::vector<float> sig_vec;
    std::vector<double> map_lon;
    std::vector<double> map_merc;
    map_lon.reserve(static_cast<std::size_t>(rows));
    map_merc.reserve(static_cast<std::size_t>(rows));

    for (int i = 0; i < rows; i++) {
        ts_vec.push_back(std::atof(PQgetvalue(res, i, 0)));
        const double lat = std::atof(PQgetvalue(res, i, 1));
        const double lon = std::atof(PQgetvalue(res, i, 2));
        sig_vec.push_back(std::atof(PQgetvalue(res, i, 3)));
        if (std::isfinite(lat) && std::isfinite(lon) && lon >= -180.0 && lon <= 180.0 && lat >= -90.0 &&
            lat <= 90.0) {
            map_lon.push_back(lon);
            map_merc.push_back(osm::lat_deg_to_mercator_y(lat));
        }
    }
    PQclear(res);

    std::lock_guard<std::mutex> lock(mtx);
    current_telemetry.map_points_lon = std::move(map_lon);
    current_telemetry.map_points_merc_y = std::move(map_merc);
    for (int i = (int)ts_vec.size() - 1; i >= 0; i--) {
        const float time_sec = current_telemetry.get_or_init_relative_time_sec(ts_vec[i]);
        current_telemetry.history.add_point(time_sec, sig_vec[i]);
    }

    log_messages.push_back("Loaded " + std::to_string(rows) + " points from database.");
}

HeatmapDbFingerprint loadHeatmapFingerprintFromDatabase(PGconn* db) {
    HeatmapDbFingerprint fp;
    if (!db || PQstatus(db) != CONNECTION_OK) return fp;

    PGresult* res = PQexec(db,
        "SELECT COUNT(*)::int, COALESCE(MAX(timestamp), 0)::bigint FROM measurements");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        fp.point_count = std::atoi(PQgetvalue(res, 0, 0));
        fp.max_timestamp = std::atoll(PQgetvalue(res, 0, 1));
    }
    PQclear(res);
    return fp;
}

std::vector<HeatmapSample> loadHeatmapSamplesFromDatabase(PGconn* db) {
    std::vector<HeatmapSample> out;
    if (!db || PQstatus(db) != CONNECTION_OK) return out;

    const char* query =
        "SELECT lat, lon, altitude, rsrp, rsrq, rssi, earfcn FROM ("
        "  SELECT m.lat, m.lon, COALESCE(m.accuracy, 0)::real AS altitude, "
        "    lte.rsrp::real AS rsrp, lte.rsrq::real AS rsrq, lte.rssi::real AS rssi, lte.earfcn AS earfcn "
        "  FROM measurements m INNER JOIN cell_info_lte lte ON m.id = lte.measurement_id "
        "  UNION ALL "
        "  SELECT m.lat, m.lon, COALESCE(m.accuracy, 0)::real, "
        "    nr.ss_rsrp::real, nr.ss_rsrq::real, NULL::real, nr.nrarfcn "
        "  FROM measurements m INNER JOIN cell_info_nr nr ON m.id = nr.measurement_id "
        "  UNION ALL "
        "  SELECT m.lat, m.lon, COALESCE(m.accuracy, 0)::real, "
        "    gsm.dbm::real, NULL::real, gsm.rssi::real, gsm.arfcn "
        "  FROM measurements m INNER JOIN cell_info_gsm gsm ON m.id = gsm.measurement_id "
        ") cells WHERE lat IS NOT NULL AND lon IS NOT NULL "
        "ORDER BY lat, lon "
        "LIMIT 40000";

    PGresult* res = PQexec(db, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Heatmap load error: " << PQresultErrorMessage(res) << "\n";
        PQclear(res);
        return out;
    }

    const int rows = PQntuples(res);
    out.reserve(static_cast<std::size_t>(rows));
    for (int i = 0; i < rows; ++i) {
        const double lat = std::atof(PQgetvalue(res, i, 0));
        const double lon = std::atof(PQgetvalue(res, i, 1));
        if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 ||
            lon > 180.0)
            continue;

        HeatmapSample s;
        s.lat = lat;
        s.lon = lon;
        s.merc_y = osm::lat_deg_to_mercator_y(lat);
        s.altitude = static_cast<float>(std::atof(PQgetvalue(res, i, 2)));
        s.rsrp = static_cast<float>(std::atof(PQgetvalue(res, i, 3)));
        s.rsrq = static_cast<float>(std::atof(PQgetvalue(res, i, 4)));
        s.rssi = static_cast<float>(std::atof(PQgetvalue(res, i, 5)));
        s.earfcn = std::atoi(PQgetvalue(res, i, 6));
        out.push_back(s);
    }
    PQclear(res);
    return out;
}