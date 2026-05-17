#pragma once

#include <memory>

namespace hisol {

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

} // namespace hisol
