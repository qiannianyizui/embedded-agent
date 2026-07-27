// AppBuilder — factory that constructs an AppContext from an AppConfig
// Encapsulates main.cpp initialization steps 3~7.6
#pragma once
#include "app/AppContext.h"
#include "base/Result.h"

namespace ea::app {

class AppBuilder {
public:
    // Build a fully-constructed AppContext from the given config.
    // debug flag controls debug logging and listener attachment.
    // Returns the AppContext on success, or an Error on failure
    // (e.g., unknown provider type).
    static Result<AppContext> build(const config::AppConfig& cfg, bool debug = false, RunMode mode = RunMode::Cli);
};

}  // namespace ea::app
