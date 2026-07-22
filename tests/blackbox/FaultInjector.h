// tests/blackbox/FaultInjector.h
// Fault injection utilities for testing system resilience.
// FaultyProvider wraps an IProvider and injects various failure modes.
// FaultyMemory wraps an IMemory and injects lifecycle/data failures.
#pragma once
#include "core/IProvider.h"
#include "core/ITool.h"
#include "core/IMemory.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include "provider/ProviderCapabilities.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>
#include <thread>

namespace ea::test::fault {

// FaultyProvider — wraps an IProvider and injects controlled failures
class FaultyProvider : public IProvider {
public:
    enum class FaultMode {
        None,             // Normal passthrough
        Timeout,          // Returns Error::timeout()
        ConnectionReset,  // Returns Error::net("connection reset")
        PartialJson,      // Returns truncated JSON in LLMResponse
        EmptyBody,        // Returns LLMResponse with empty content
        NonJsonBody,      // Returns Error::parse() (simulates HTML error page)
        DelayedResponse,  // Delays before forwarding to backend
        Intermittent,     // Alternates between success and failure
    };

    explicit FaultyProvider(std::shared_ptr<IProvider> backend,
                            std::string name = "faulty")
        : backend_(std::move(backend)), name_(std::move(name)) {}

    void set_fault(FaultMode mode, int fail_after_n = 0) {
        fault_mode_ = mode;
        fail_after_n_ = fail_after_n;
        call_count_ = 0;
    }

    void set_delay(std::chrono::milliseconds delay) {
        delay_ = delay;
    }

    void clear_fault() {
        fault_mode_ = FaultMode::None;
        fail_after_n_ = 0;
        call_count_ = 0;
        delay_ = std::chrono::milliseconds(0);
    }

    // IProvider interface
    std::string name() const override { return name_; }

    std::vector<std::string> list_models() const override {
        return backend_->list_models();
    }

    provider::ProviderCapabilities capabilities() const override {
        return backend_->capabilities();
    }

    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}
    ) override {
        call_count_++;

        // Apply delay if configured
        if (delay_ > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(delay_);
        }

        // Check if we should inject a fault
        if (should_inject_fault()) {
            fault_count_++;
            return inject_chat_fault();
        }

        auto result = backend_->chat(messages, tools, model, opts);
        if (result.ok()) success_count_++;
        return result;
    }

    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}
    ) override {
        call_count_++;

        if (delay_ > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(delay_);
        }

        if (should_inject_fault()) {
            fault_count_++;
            switch (fault_mode_) {
                case FaultMode::Timeout:
                    return Error::timeout("stream connection timed out");
                case FaultMode::ConnectionReset:
                    return Error::net("stream connection reset by peer");
                default:
                    return Error::net("stream fault injected");
            }
        }

        auto result = backend_->stream_chat(messages, tools, model, std::move(on_chunk), opts);
        if (result.ok()) success_count_++;
        return result;
    }

    // Observation
    int fault_count() const { return fault_count_; }
    int success_count() const { return success_count_; }
    int total_calls() const { return call_count_; }

private:
    bool should_inject_fault() {
        if (fault_mode_ == FaultMode::None) return false;
        if (fault_mode_ == FaultMode::Intermittent) {
            return (call_count_ % 2 == 1);  // Odd calls fail
        }
        if (fail_after_n_ > 0) {
            return call_count_ > fail_after_n_;
        }
        return true;  // Always inject for other modes
    }

    Result<LLMResponse> inject_chat_fault() {
        switch (fault_mode_) {
            case FaultMode::Timeout:
                return Error::timeout("connection timed out");
            case FaultMode::ConnectionReset:
                return Error::net("connection reset by peer");
            case FaultMode::PartialJson: {
                LLMResponse resp;
                resp.content = R"({"choices":[{"message":{"content":"Hel)";
                resp.stop_reason = "error";
                return resp;
            }
            case FaultMode::EmptyBody: {
                LLMResponse resp;
                resp.content = "";
                resp.stop_reason = "stop";
                return resp;
            }
            case FaultMode::NonJsonBody:
                return Error::parse("Failed to parse response: expected JSON, got HTML");
            case FaultMode::DelayedResponse:
                // Already handled delay above, fall through to normal
                break;
            case FaultMode::Intermittent:
                return Error::net("intermittent fault");
            default:
                break;
        }
        return Error::net("unknown fault");
    }

    std::shared_ptr<IProvider> backend_;
    std::string name_;
    FaultMode fault_mode_ = FaultMode::None;
    int fail_after_n_ = 0;
    std::atomic<int> call_count_{0};
    std::atomic<int> fault_count_{0};
    std::atomic<int> success_count_{0};
    std::chrono::milliseconds delay_{0};
};

// FaultyMemory — wraps an IMemory and injects controlled failures
class FaultyMemory : public IMemory {
public:
    explicit FaultyMemory(std::unique_ptr<IMemory> backend)
        : backend_(std::move(backend)) {}

    void inject_open_failure() { open_fails_ = true; }
    void inject_store_failure() { store_fails_ = true; }
    void inject_recall_failure() { recall_fails_ = true; }
    void clear_all_failures() {
        open_fails_ = false;
        store_fails_ = false;
        recall_fails_ = false;
    }

    // IMemory interface
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override {
        if (store_fails_) return Error::db("store fault injected");
        return backend_->store(content, category, importance);
    }

    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                              int limit = 10) override {
        if (recall_fails_) return Error::db("recall fault injected");
        return backend_->recall(query, limit);
    }

    Result<bool> forget(const std::string& id) override {
        return backend_->forget(id);
    }

    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override {
        return backend_->list(limit, offset);
    }

    Result<int> count() override {
        return backend_->count();
    }

    Result<void> open() override {
        if (open_fails_) return Error::db("open fault injected");
        return backend_->open();
    }

    Result<void> close() override {
        return backend_->close();
    }

    std::string system_prompt_block() const override {
        return backend_->system_prompt_block();
    }

    void on_turn_start(const std::string& user_input) override {
        backend_->on_turn_start(user_input);
    }

    void on_turn_end(const std::string& assistant_output) override {
        backend_->on_turn_end(assistant_output);
    }

    void on_pre_compress() override {
        backend_->on_pre_compress();
    }

private:
    std::unique_ptr<IMemory> backend_;
    bool open_fails_ = false;
    bool store_fails_ = false;
    bool recall_fails_ = false;
};

}  // namespace ea::test::fault
