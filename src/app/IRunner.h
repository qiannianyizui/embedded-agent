// IRunner — strategy interface for application run modes
#pragma once
#include <memory>

namespace ea::app {

struct AppContext;

class IRunner {
public:
    virtual ~IRunner() = default;
    virtual int run(AppContext& ctx) = 0;

    // Factory: currently creates the TUI runner.
    static std::unique_ptr<IRunner> create();
};

}  // namespace ea::app
