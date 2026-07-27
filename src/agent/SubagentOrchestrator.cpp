#include "SubagentOrchestrator.h"
#include "log/Logger.h"
#include <thread>
#include <mutex>

namespace ea::agent {

SubagentOrchestrator::SubagentOrchestrator(IProvider* provider,
                                           tool::ToolRegistry* registry,
                                           IMemory* memory)
    : provider_(provider), registry_(registry), memory_(memory) {}

void SubagentOrchestrator::register_template(SubagentConfig config) {
    EA_INFO("Registered subagent template: {}", config.name);
    templates_[config.name] = std::move(config);
}

Result<std::shared_ptr<Subagent>> SubagentOrchestrator::create(const std::string& template_name) {
    auto it = templates_.find(template_name);
    if (it == templates_.end()) {
        return Error::not_found("Unknown subagent template: " + template_name);
    }

    std::string id = template_name + "-" + std::to_string(next_id_++);
    auto subagent = std::make_shared<Subagent>(
        std::move(id), it->second, provider_, registry_, memory_);

    return subagent;
}

Result<std::string> SubagentOrchestrator::delegate(const std::string& template_name,
                                                    const std::string& task) {
    auto subagent_result = create(template_name);
    if (!subagent_result.ok()) return subagent_result.error();

    return subagent_result.value()->execute(task);
}

Result<std::vector<std::pair<std::string, std::string>>>
SubagentOrchestrator::delegate_parallel(
    const std::vector<std::pair<std::string, std::string>>& tasks)
{
    std::vector<std::pair<std::string, std::string>> results;
    results.resize(tasks.size());

    std::vector<std::thread> threads;
    std::mutex results_mutex;

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& template_name = tasks[i].first;
        const auto& task = tasks[i].second;

        threads.emplace_back([&, i, template_name, task]() {
            auto result = delegate(template_name, task);
            std::lock_guard<std::mutex> lock(results_mutex);
            results[i] = {template_name, result.ok() ? result.value() : result.error().message};
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return results;
}

std::vector<std::string> SubagentOrchestrator::available_templates() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : templates_) {
        names.push_back(name);
    }
    return names;
}

bool SubagentOrchestrator::has_template(const std::string& name) const {
    return templates_.count(name) > 0;
}

}  // namespace ea::agent
