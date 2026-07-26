// TuiRunner — FTXUI-based interactive interface
#pragma once
#include "app/IRunner.h"

namespace ea::app {

class TuiRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
