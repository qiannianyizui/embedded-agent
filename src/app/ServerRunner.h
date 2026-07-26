// ServerRunner — HTTP API server mode
#pragma once
#include "app/IRunner.h"

namespace ea::app {

class ServerRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
