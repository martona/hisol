#pragma once

#include <map>
#include <string>
#include <string_view>

namespace hisol {

class CookieJar {
public:
    void add_set_cookie(std::string_view header);
    void set(std::string name, std::string value);

    [[nodiscard]] std::string header() const;

private:
    std::map<std::string, std::string> cookies_;
};

} // namespace hisol
