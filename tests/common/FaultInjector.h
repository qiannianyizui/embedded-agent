// tests/common/FaultInjector.h
// Fault injection infrastructure — wraps IProvider and IMemory with controlled
// failure modes for grey-box resilience testing.
#pragma once
#include "provider/IProvider.h"
#include "memory/IMemory.h"
#include "common/base/Types.h"
#include "common/base/Result.h"
#include "common/base/Error.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace ea::test::fault {

// ── FaultyProvider ────────────────────────────────────────────────────────────
// Wraps an IProvider and injects failures according to the configured FaultMode.

class FaultyProvider : public IProvider {
public:
    enum class FaultMode {
        None,              // Pass through to inner provider
        Timeout,           // Return Error::timeout
        ConnectionReset,   // Return Error::net (connection reset)
        PartialJson,       // Return LLMResponse with truncated content
        EmptyBody,         // Return LLMResponse with empty content
        NonJsonBody,       // Return Error::parse (non-JSON body)
        Intermittent,      // Fail on odd calls, pass on even calls
    };

    explicit FaultyProvider(std::shared_ptr<IProvider> inner)
        : inner_(std::move(inner)) {}

    void set_fault(FaultMode mode) { fault_mode_ = mode; }
    void clear_fault() { fault_mode_ = FaultMode::None; }
    FaultMode fault_mode() const { return fault_mode_; }
    int fault_count() const { return fault_count_.load(); }

    std::string name() const override {
        return "faulty(" + inner_->name() + ")";
    }

    std::vector<std::string> list_models() const override {
        return inner_->list_models();
    }

    provider::ProviderCapabilities capabilities() const override {
        return inner_->capabilities();
    }

    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) override
    {
        if (should_inject()) {
            return inject_chat_fault();
        }
        return inner_->chat(messages, tools, model, opts);
    }

    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) override
    {
        if (should_inject()) {
            return inject_stream_fault();
        }
        return inner_->stream_chat(messages, tools, model, std::move(on_chunk), opts);
    }

private:
    bool should_inject() {
        if (fault_mode_ == FaultMode::None) return false;

        if (fault_mode_ == FaultMode::Intermittent) {
            int count = call_count_.fetch_add(1);
            if (count % 2 == 0) {
                // Odd call (0-indexed even) — inject fault
                fault_count_.fetch_add(1);
                return true;
            }
            return false;
        }

        fault_count_.fetch_add(1);
        return true;
    }

    Result<LLMResponse> inject_chat_fault() {
        switch (fault_mode_) {
            case FaultMode::Timeout:
                return Error::timeout("simulated provider timeout");

            case FaultMode::ConnectionReset:
                return Error::net("simulated connection reset by peer");

            case FaultMode::PartialJson: {
                LLMResponse resp;
                resp.content = "{\"choices\":[{\"message\":{\"content\":\"partial";  // truncated
                resp.stop_reason = "stop";
                return resp;
            }

            case FaultMode::EmptyBody: {
                LLMResponse resp;
                resp.content = "";
                resp.stop_reason = "stop";
                return resp;
            }

            case FaultMode::NonJsonBody:
                return Error::parse("simulated non-JSON response body: <html>502</html>");

            case FaultMode::Intermittent:
                return Error::net("simulated intermittent failure");

            default:
                return Error::net("unknown fault mode");
        }
    }

    Result<void> inject_stream_fault() {
        switch (fault_mode_) {
            case FaultMode::Timeout:
                return Error::timeout("simulated stream timeout");

            case FaultMode::ConnectionReset:
                return Error::net("simulated stream connection reset");

            default:
                return Error::net("simulated stream failure");
        }
    }

    std::shared_ptr<IProvider> inner_;
    FaultMode fault_mode_ = FaultMode::None;
    std::atomic<int> call_count_{0};
    std::atomic<int> fault_count_{0};
};

// ── FaultyMemory ──────────────────────────────────────────────────────────────
// Wraps an IMemory and injects failures for specific operations.

class FaultyMemory : public IMemory {
public:
    explicit FaultyMemory(std::unique_ptr<IMemory> inner)
        : inner_(std::move(inner)) {}

    // Inject specific failures
    void inject_open_failure() { open_fails_ = true; }
    void inject_store_failure() { store_fails_ = true; }
    void inject_recall_failure() { recall_fails_ = true; }
    void inject_forget_failure() { forget_fails_ = true; }
    void inject_list_failure() { list_fails_ = true; }
    void inject_count_failure() { count_fails_ = true; }

    // Clear all injected failures
    void clear_failures() {
        open_fails_ = false;
        store_fails_ = false;
        recall_fails_ = false;
        forget_fails_ = false;
        list_fails_ = false;
        count_fails_ = false;
    }

    Result<void> open() override {
        if (open_fails_) return Error::db("injected open failure");
        return inner_->open();
    }

    Result<void> close() override {
        return inner_->close();
    }

    std::string system_prompt_block() const override {
        return inner_->system_prompt_block();
    }

    void on_turn_start(const std::string& user_input) override {
        inner_->on_turn_start(user_input);
    }

    void on_turn_end(const std::string& assistant_output) override {
        inner_->on_turn_end(assistant_output);
    }

    void on_pre_compress() override {
        inner_->on_pre_compress();
    }

    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override {
        if (store_fails_) return Error::db("injected store failure");
        return inner_->store(content, category, importance);
    }

    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                              int limit = 10) override {
        if (recall_fails_) return Error::db("injected recall failure");
        return inner_->recall(query, limit);
    }

    Result<bool> forget(const std::string& id) override {
        if (forget_fails_) return Error::db("injected forget failure");
        return inner_->forget(id);
    }

    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override {
        if (list_fails_) return Error::db("injected list failure");
        return inner_->list(limit, offset);
    }

    Result<int> count() override {
        if (count_fails_) return Error::db("injected count failure");
        return inner_->count();
    }

private:
    std::unique_ptr<IMemory> inner_;
    bool open_fails_ = false;
    bool store_fails_ = false;
    bool recall_fails_ = false;
    bool forget_fails_ = false;
    bool list_fails_ = false;
    bool count_fails_ = false;
};

}  // namespace ea::test::fault
