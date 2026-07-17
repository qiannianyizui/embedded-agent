// SubagentOrchestrator — manages sub-agent templates and serial/parallel delegation
#pragma once
#include "SubagentConfig.h"
#include "Subagent.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ea::agent {

class SubagentOrchestrator {
public:
    explicit SubagentOrchestrator(IProvider* provider,
                                   tool::ToolRegistry* registry,
                                   IMemory* memory);

    void register_template(SubagentConfig config);

    Result<std::shared_ptr<Subagent>> create(const std::string& template_name);

    Result<std::string> delegate(const std::string& template_name,
                                 const std::string& task);

    Result<std::vector<std::pair<std::string, std::string>>>
    delegate_parallel(const std::vector<std::pair<std::string, std::string>>& tasks);

    std::vector<std::string> available_templates() const;
    bool has_template(const std::string& name) const;

private:
    IProvider* provider_;
    tool::ToolRegistry* registry_;
    IMemory* memory_;
    std::map<std::string, SubagentConfig> templates_;
    int next_id_ = 1;
};

}  // namespace ea::agent
