// IRunner — strategy interface for application run modes
#pragma once
#include <memory>
#include <string>

namespace ea::app {

struct AppContext;

class IRunner {
public:
    virtual ~IRunner() = default;
    virtual int run(AppContext& ctx) = 0;

    // Factory: select Runner based on compile-time mode
    static std::unique_ptr<IRunner> create();
};

}  // namespace ea::app
