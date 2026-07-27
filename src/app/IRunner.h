// IRunner — strategy interface for application run modes
#pragma once
#include <memory>

namespace ea::app {

struct AppContext;

// Runtime mode selection (replaces compile-time EA_MODE_* macros)
enum class RunMode {
    Cli,
    Tui,
    Server
};

class IRunner {
public:
    virtual ~IRunner() = default;
    virtual int run(AppContext& ctx) = 0;

    // Factory: select Runner based on runtime mode
    static std::unique_ptr<IRunner> create(RunMode mode);
};

}  // namespace ea::app
