#include "app/IRunner.h"
#include "app/CliRunner.h"
#include "ea/build_config.h"

#if defined(EA_MODE_SERVER)
#include "app/ServerRunner.h"
#elif defined(EA_ENABLE_TUI)
#include "app/TuiRunner.h"
#endif

namespace ea::app {

std::unique_ptr<IRunner> IRunner::create() {
#if defined(EA_MODE_SERVER)
    return std::make_unique<ServerRunner>();
#elif defined(EA_ENABLE_TUI)
    return std::make_unique<TuiRunner>();
#else
    return std::make_unique<CliRunner>();
#endif
}

}  // namespace ea::app
