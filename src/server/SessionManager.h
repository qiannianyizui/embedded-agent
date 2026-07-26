// src/server/SessionManager.h
#pragma once
#include "ServerConfig.h"
#include "agent/AgentLoop.h"
#include "provider/IProvider.h"
#include "tool/ToolRegistry.h"
#include "memory/IMemory.h"
#include "security/SecurityPolicy.h"
#include "memory/ScopedMemory.h"
#include "memory/InMemoryBackend.h"
#include "security/PendingApprovalHandler.h"
#include "conversation/IConversationStore.h"
#include "budget/BudgetTracker.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>

namespace ea::server {

using AgentLoop = agent::AgentLoop;

struct Session {
    std::string id;
    std::shared_ptr<AgentLoop> loop;
    std::unique_ptr<IMemory> memory;       // ScopedMemory wrapping backend
    std::unique_ptr<security::PendingApprovalHandler> approval;
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point last_active;
    std::string model;                     // model override for this session
};

class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config,
                            conversation::IConversationStore* conv_store = nullptr,
                            budget::BudgetTracker* budget_tracker = nullptr);

    Session* create(IProvider* provider,
                    tool::ToolRegistry* registry,
                    IMemory* shared_backend,
                    security::SecurityPolicy* policy,
                    const AgentLoop::Config& loop_cfg,
                    const std::string& model = "",
                    const std::string& system_prompt = "",
                    const std::string& conversation_id = "");

    Session* get(const std::string& id);
    bool remove(const std::string& id);
    std::vector<Session*> list();
    void cleanup_idle();

private:
    std::string generate_id();

    ServerConfig config_;
    conversation::IConversationStore* conv_store_;
    budget::BudgetTracker* budget_tracker_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace ea::server
