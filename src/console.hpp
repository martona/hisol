#pragma once

#include <memory>
#include <optional>
#include <string>

namespace hisol {

struct ConsoleSize {
    int columns = 0;
    int rows = 0;
};

class ConsoleModeGuard {
public:
    ConsoleModeGuard();
    ConsoleModeGuard(const ConsoleModeGuard&) = delete;
    ConsoleModeGuard& operator=(const ConsoleModeGuard&) = delete;
    ~ConsoleModeGuard();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::optional<ConsoleSize> current_console_size();
std::string read_password_from_console(const std::string& prompt);

} // namespace hisol
