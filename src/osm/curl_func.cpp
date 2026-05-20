#include "curl_func.hpp"

#include <curl/curl.h>

#include <cstring>

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::vector<unsigned char>*>(userdata);
    const size_t n = size * nmemb;
    out->insert(out->end(), ptr, ptr + n);
    return n;
}

}

bool curl_http_get_binary(const std::string& url, std::vector<unsigned char>& out_body, std::string& out_error) {
    out_body.clear();
    out_error.clear();

    

    CURL* curl = curl_easy_init();
    if (!curl) {
        out_error = "curl_easy_init failed";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TelemetryKBAViewer/1.0 (statsenko228s@gmail.com)");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
  
    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        out_error = curl_easy_strerror(rc);
        return false;
    }
    if (http_code != 200) {
        out_error = "HTTP " + std::to_string(http_code);
        return false;
    }
    
    return true;
}
