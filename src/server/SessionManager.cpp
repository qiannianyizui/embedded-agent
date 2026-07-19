// src/server/SessionManager.cpp
#include "SessionManager.h"
#include "common/io/Logger.h"
#include <sstream>
#include <iomanip>

namespace ea::server {

SessionManager::SessionManager(const ServerConfig& config)
    : config_(config) {}

std::string SessionManager::generate_id() {
    // Generate "sess_" + 8 hex chars from random bytes
    // Note: caller must hold mutex_ — this method does NOT lock
    std::stringstream ss;
    ss << "sess_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << (rng_() & 0xFF);
    }
    return ss.str();
}

Session* SessionManager::create(IProvider* provider,
                                 tool::ToolRegistry* registry,
                                 IMemory* /*shared_backend*/,
                                 security::SecurityPolicy* policy,
                                 const AgentLoop::Config& loop_cfg,
                                 const std::string& model,
                                 const std::string& /*system_prompt*/) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (static_cast<int>(sessions_.size()) >= config_.max_sessions) {
        EA_WARN("Max sessions ({}) reached, cannot create new session", config_.max_sessions);
        return nullptr;
    }

    auto session = std::make_unique<Session>();
    session->id = generate_id();
    session->model = model;
    // TODO: pass model to AgentLoop when supported — currently AgentLoop::Config
    // and TurnContext do not accept a model override, so the model specified at
    // session creation is stored but unused. This is a known limitation.
    session->last_active = std::chrono::steady_clock::now();

    // Create scoped memory for this session
    memory::MemoryScope scope;
    scope.agent_id = session->id;
    // Create a new InMemoryBackend per session for simplicity.
    // In production, shared_backend would be a shared SqliteMemory
    // wrapped with ScopedMemory for isolation.
    auto per_session_backend = std::make_unique<memory::InMemoryBackend>();
    session->memory = std::make_unique<memory::ScopedMemory>(
        std::move(per_session_backend), scope);

    // Create per-session approval handler
    session->approval = std::make_unique<security::PendingApprovalHandler>(
        300);  // 5 minute default timeout

    // Create AgentLoop for this session
    session->loop = std::make_shared<AgentLoop>(
        provider,
        registry,
        session->memory.get(),
        loop_cfg,
        [](const std::string& text) { (void)text; },  // Output captured via history
        nullptr,  // StreamFn set per-request
        policy,
        session->approval.get()
    );

    auto* ptr = session.get();
    sessions_[session->id] = std::move(session);

    EA_INFO("Session created: {}", ptr->id);
    return ptr;
}

Session* SessionManager::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    it->second->last_active = std::chrono::steady_clock::now();
    return it->second.get();
}

bool SessionManager::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;

    // Refuse to delete a session whose AgentLoop is still running
    if (it->second->running.load()) {
        EA_WARN("Cannot remove running session: {}", id);
        return false;
    }

    EA_INFO("Session removed: {}", id);
    sessions_.erase(it);
    return true;
}

std::vector<Session*> SessionManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Session*> result;
    for (const auto& [id, session] : sessions_) {
        result.push_back(session.get());
    }
    return result;
}

void SessionManager::cleanup_idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = config_.session_idle_timeout;

    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second->last_active);
        if (elapsed > timeout && !it->second->running.load()) {
            EA_INFO("Session expired (idle): {}", it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace ea::server
