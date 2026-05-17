#pragma once

#include "url.hpp"

#include <string>

namespace hisol {

struct GetOptions {
    Url url;
    bool include_headers = false;
    bool verbose = false;
    bool insecure = false;
};

struct SolOptions {
    Url base_url;
    std::string username;
    std::string password;
    bool debug_frames = false;
    bool raw = false;
    bool verbose = false;
    bool insecure = false;
};

} // namespace hisol
