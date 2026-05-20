#pragma once

#include <algorithm>
#include <cmath>

namespace osm {

inline constexpr double kPi = 3.14159265358979323846;

inline double lat_deg_to_mercator_y(double lat_deg) {
    const double clamped = std::clamp(lat_deg, -85.05112878, 85.05112878);
    const double lat_rad = clamped * kPi / 180.0;
    return 180.0 / kPi * std::log(std::tan(kPi / 4.0 + lat_rad / 2.0));
}

inline double mercator_y_to_lat_deg(double merc_y) {
    return std::atan(std::exp(merc_y * kPi / 180.0)) * 360.0 / kPi - 90.0;
}

inline double mercator_x_to_tile_x(double mercator_x, int zoom) {
    const int n = 1 << zoom;
    return (0.5 + mercator_x / 360.0) * static_cast<double>(n);
}

inline double mercator_y_to_tile_y(double mercator_y, int zoom) {
    const int n = 1 << zoom;
    return (0.5 - mercator_y / 360.0) * static_cast<double>(n);
}

inline double tile_x_to_mercator_x(int tile_x, int zoom) {
    const double n = static_cast<double>(1 << zoom);
    return (static_cast<double>(tile_x) / n - 0.5) * 360.0;
}

inline double tile_y_to_mercator_y(int tile_y, int zoom) {
    const double n = static_cast<double>(1 << zoom);
    return (0.5 - static_cast<double>(tile_y) / n) * 360.0;
}

inline double MercatorXToTileX(double mercatorX, int zoom) { return mercator_x_to_tile_x(mercatorX, zoom); }
inline double MercatorYToTileY(double mercatorY, int zoom) { return mercator_y_to_tile_y(mercatorY, zoom); }
inline double TileXToMercatorX(int tileX, int zoom) { return tile_x_to_mercator_x(tileX, zoom); }
inline double TileYToMercatorY(int tileY, int zoom) { return tile_y_to_mercator_y(tileY, zoom); }

inline int zoom_from_lon_span(double lon_min, double lon_max) {
    const double span = std::max(1e-6, lon_max - lon_min);
    if (span > 90.0) return 4;
    if (span > 45.0) return 5;
    if (span > 22.5) return 6;
    if (span > 11.25) return 7;
    if (span > 5.625) return 8;
    if (span > 2.8125) return 9;
    if (span > 1.40625) return 10;
    if (span > 0.703125) return 11;
    if (span > 0.3515625) return 12;
    if (span > 0.17578125) return 13;
    if (span > 0.087890625) return 14;
    if (span > 0.0439453125) return 15;
    if (span > 0.02197265625) return 16;
    if (span > 0.010986328125) return 17;
    if (span > 0.0054931640625) return 18;
    return 19;
}

}
