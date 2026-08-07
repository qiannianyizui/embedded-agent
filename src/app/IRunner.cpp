#include "app/IRunner.h"
#include "app/TuiRunner.h"

namespace ea::app {

std::unique_ptr<IRunner> IRunner::create() {
    return std::make_unique<TuiRunner>();
}

}  // namespace ea::app
