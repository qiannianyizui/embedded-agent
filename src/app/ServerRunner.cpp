#include "app/ServerRunner.h"
#include "app/AppContext.h"
#include "server/HttpServer.h"
#include "log/Logger.h"
#include <chrono>

namespace ea::app {

int ServerRunner::run(AppContext& ctx) {
    ea::server::ServerConfig srv_cfg;
    srv_cfg.host = ctx.config.server.host;
    srv_cfg.port = ctx.config.server.port;
    srv_cfg.max_sessions = ctx.config.server.max_sessions;
    srv_cfg.cors_origin = ctx.config.server.cors_origin;
    srv_cfg.session_idle_timeout = std::chrono::seconds(ctx.config.server.session_idle_timeout);

    auto http_server = std::make_unique<ea::server::HttpServer>(
        srv_cfg, ctx.effective_provider, ctx.registry.get(), ctx.security.get(),
        ctx.memory.get(), ctx.conversation_store.get(), ctx.budget_tracker.get()
    );

    EA_INFO("Server starting on {}:{}", srv_cfg.host, srv_cfg.port);
    http_server->start();
    return 0;
}

}  // namespace ea::app
