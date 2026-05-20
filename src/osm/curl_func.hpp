#pragma once

#include <cstddef>
#include <string>
#include <vector>

bool curl_http_get_binary(const std::string& url, std::vector<unsigned char>& out_body, std::string& out_error);
