// CliRunner — CLI interactive loop with command dispatch
#pragma once
#include "app/IRunner.h"
#include "app/CliCommandRegistry.h"
#include <memory>

namespace ea::app {

class CliRunner : public IRunner {
public:
    int run(AppContext& ctx) override;

private:
    std::unique_ptr<CliCommandRegistry> commands_;
    void register_commands(AppContext& ctx);
};

}  // namespace ea::app
