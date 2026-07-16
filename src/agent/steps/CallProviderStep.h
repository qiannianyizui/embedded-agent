// CallProviderStep — invoke LLM provider with optional context compression
#pragma once
#include "agent/ITurnStep.h"
#include "agent/ContextCompressor.h"

namespace ea::agent {

class CallProviderStep : public ITurnStep {
public:
    explicit CallProviderStep(ContextCompressor* compressor = nullptr);

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "call_provider"; }

private:
    ContextCompressor* compressor_;
};

}  // namespace ea::agent
