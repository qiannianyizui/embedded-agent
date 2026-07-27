#include "ReliableProvider.h"
#include "ErrorClassifier.h"
#include "log/Logger.h"
#include <thread>

namespace ea::provider {

ReliableProvider::ReliableProvider(std::shared_ptr<IProvider> primary, Config config)
    : primary_(std::move(primary)), config_(std::move(config)) {}

std::string ReliableProvider::name() const { return primary_->name(); }

std::vector<std::string> ReliableProvider::list_models() const {
    return primary_->list_models();
}

provider::ProviderCapabilities ReliableProvider::capabilities() const {
    return primary_->capabilities();
}

Result<LLMResponse> ReliableProvider::try_with_retries(
    std::shared_ptr<IProvider> provider,
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts)
{
    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        auto result = provider->chat(messages, tools, model, opts);

        if (result.ok()) return result;

        auto error_class = classify_error(result.error());

        if (error_class == ErrorClass::NonRetryable) {
            return result;
        }

        if (attempt < config_.max_retries) {
            auto delay = config_.base_delay;
            for (int i = 0; i < attempt; ++i) {
                delay = std::chrono::milliseconds(
                    static_cast<long>(delay.count() * config_.backoff_multiplier));
            }

            EA_WARN("Provider '{}' error (attempt {}/{}), retrying in {}ms: {}",
                    provider->name(), attempt + 1, config_.max_retries + 1,
                    delay.count(), result.error().message);

            std::this_thread::sleep_for(delay);
        } else {
            return result;
        }
    }
    return Error::net("unexpected retry loop exit");
}

Result<LLMResponse> ReliableProvider::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts)
{
    auto result = try_with_retries(primary_, messages, tools, model, opts);
    if (result.ok()) return result;

    auto error_class = classify_error(result.error());

    if (error_class == ErrorClass::NonRetryable || error_class == ErrorClass::RateLimit) {
        for (auto& fallback : config_.fallbacks) {
            EA_INFO("Primary provider '{}' failed, trying fallback '{}'",
                    primary_->name(), fallback->name());
            auto fb_result = fallback->chat(messages, tools, model, opts);
            if (fb_result.ok()) return fb_result;
        }
    }

    return result;
}

Result<void> ReliableProvider::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts)
{
    return primary_->stream_chat(messages, tools, model, on_chunk, opts);
}

}  // namespace ea::provider
