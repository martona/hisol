#pragma once

#include <memory>
#include <optional>

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

} // namespace hisol
