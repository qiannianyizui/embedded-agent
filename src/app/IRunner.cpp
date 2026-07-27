#include "app/IRunner.h"
#include "app/CliRunner.h"
#include "app/ServerRunner.h"
#include "app/TuiRunner.h"

namespace ea::app {

std::unique_ptr<IRunner> IRunner::create(RunMode mode) {
    switch (mode) {
        case RunMode::Server:
            return std::make_unique<ServerRunner>();
        case RunMode::Tui:
            return std::make_unique<TuiRunner>();
        case RunMode::Cli:
        default:
            return std::make_unique<CliRunner>();
    }
}

}  // namespace ea::app
