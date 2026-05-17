#pragma once

#include "cookie_jar.hpp"
#include "options.hpp"

#include <string>

namespace hisol {

struct SolSession {
    CookieJar cookies;
    std::string csrf_token;
};

SolSession open_sol_session(const SolOptions& options);

} // namespace hisol
