#include "tile_loader.hpp"

#include <curl/curl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "curl_func.hpp"
#include "tile_cache.hpp"
#include "tile_paths.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

std::string tile_key(int zoom, int tile_x, int tile_y) {
    std::ostringstream oss;
    oss << zoom << "/" << tile_x << "/" << tile_y;
    return oss.str();
}

struct CompletedTile {
    std::string key;
    int zoom = 0;
    int x = 0;
    int y = 0;
    std::vector<unsigned char> rgba;
    int img_w = 0;
    int img_h = 0;
    std::uint64_t generation = 0;
    bool ok = false;
};

struct TileJob {
    int zoom = 0;
    int x = 0;
    int y = 0;
    std::uint64_t generation = 0;
};

static std::mutex g_done_mtx;
static std::deque<CompletedTile> g_done;

static std::mutex g_q_mtx;
static std::condition_variable g_q_cv;
static std::deque<TileJob> g_queue;
static std::unordered_set<std::string> g_queued_keys;
static std::thread g_worker;
static bool g_stop = true;

static constexpr std::size_t kMaxQueue = 256;

static bool png_bytes_to_rgba(const unsigned char* png, std::size_t png_len, std::vector<unsigned char>& out_rgba, int& out_w, int& out_h) {
    if (!png || png_len < 8 || png[0] != 0x89 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') return false;
    int w = 0, h = 0, ch = 0;
    unsigned char* pix = stbi_load_from_memory(png, static_cast<int>(png_len), &w, &h, &ch, STBI_rgb_alpha);
    if (!pix || w <= 0 || h <= 0) {
        if (pix) stbi_image_free(pix);
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    out_rgba.assign(pix, pix + n);
    stbi_image_free(pix);
    out_w = w;
    out_h = h;
    return true;
}

static std::string make_osm_tile_url(int z, int x, int y) {
    std::ostringstream oss;
    oss << "https://a.tile.openstreetmap.org/" << z << "/" << x << "/" << y << ".png";
    return oss.str();
}

static void worker_loop() {
    for (;;) {
        TileJob job{};
        {
            std::unique_lock<std::mutex> lk(g_q_mtx);
            g_q_cv.wait(lk, [] { return g_stop || !g_queue.empty(); });
            if (g_stop && g_queue.empty()) break;
            if (g_queue.empty()) continue;
            job = g_queue.front();
            g_queue.pop_front();
            g_queued_keys.erase(tile_key(job.zoom, job.x, job.y));
        }

        CompletedTile done;
        done.key = tile_key(job.zoom, job.x, job.y);
        done.zoom = job.zoom;
        done.x = job.x;
        done.y = job.y;
        done.generation = job.generation;

        const auto path = tile_png_path(job.zoom, job.x, job.y);
        std::vector<unsigned char> bytes;
        if (tile_read_file_bytes(path, bytes) && !bytes.empty()) {
            done.ok = png_bytes_to_rgba(bytes.data(), bytes.size(), done.rgba, done.img_w, done.img_h);
        } else {
            std::string err;
            std::vector<unsigned char> net;
            const std::string url = make_osm_tile_url(job.zoom, job.x, job.y);
            if (curl_http_get_binary(url, net, err) && !net.empty()) {
                tile_write_file_bytes(path, net.data(), net.size());
                done.ok = png_bytes_to_rgba(net.data(), net.size(), done.rgba, done.img_w, done.img_h);
            } else {
                done.ok = false;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_done_mtx);
            while (g_done.size() > 128) g_done.pop_front();
            g_done.push_back(std::move(done));
        }
    }
}

static bool try_pop_completed(CompletedTile& out) {
    std::lock_guard<std::mutex> lk(g_done_mtx);
    if (g_done.empty()) return false;
    out = std::move(g_done.front());
    g_done.pop_front();
    return true;
}

void tile_loader_global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

void tile_loader_global_cleanup() { curl_global_cleanup(); }

void tile_loader_start() {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    if (!g_stop) return;
    g_stop = false; 
    g_worker = std::thread(worker_loop);
}

void tile_loader_stop() {
    {
        std::lock_guard<std::mutex> lk(g_q_mtx);
        g_stop = true;
        g_queue.clear();
        g_queued_keys.clear();
    }
    g_q_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();

    std::lock_guard<std::mutex> lk2(g_done_mtx);
    g_done.clear();
}

void tile_loader_clear_queue() {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    g_queue.clear();
    g_queued_keys.clear();
    g_q_cv.notify_one();
}

void tile_loader_enqueue(int zoom, int tile_x, int tile_y, std::uint64_t generation) {
    const std::string key = tile_key(zoom, tile_x, tile_y);
    std::lock_guard<std::mutex> lk(g_q_mtx);
    if (g_stop) return;
    if (g_queued_keys.count(key)) return;
    if (g_queue.size() >= kMaxQueue) return;
    g_queued_keys.insert(key);
    g_queue.push_back(TileJob{zoom, tile_x, tile_y, generation});
    g_q_cv.notify_one();
}

bool tile_loader_try_pop_completed_rgba(std::string& key, int& z, int& x, int& y,
                                        std::vector<unsigned char>& rgba, int& out_w, int& out_h,
                                        std::uint64_t& generation, bool& ok) {
    CompletedTile ct;
    if (!try_pop_completed(ct)) return false;
    key = std::move(ct.key);
    z = ct.zoom;
    x = ct.x;
    y = ct.y;
    rgba = std::move(ct.rgba);
    out_w = ct.img_w;
    out_h = ct.img_h;
    generation = ct.generation;
    ok = ct.ok;
    return true;
}
