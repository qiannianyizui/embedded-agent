// TomlConversion.h — toml11 from/into specializations for config structs
// Enables automatic TOML ↔ C++ struct mapping via toml::get<T> and toml::into<T>
#pragma once
#include "Config.h"
#include <toml.hpp>
#include <chrono>

// ============================================================================
// Simple structs — TOML11_DEFINE_CONVERSION_NON_INTRUSIVE macro
// Order matters: leaf types must be defined before types that nest them.
// ============================================================================

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::MemoryFilesConfig,
    enable, dir, memory_char_limit, user_char_limit)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::OnboardingConfig,
    profile_build, profile_build_offered)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::LogConfig,
    level, persist, verbose, max_file_bytes, max_total_bytes)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::TraceConfig,
    persist, max_entries, tool_io, tool_io_truncate_bytes, llm_payload)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::ConversationConfig,
    path, auto_resume, auto_persist, max_conversations)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::agent::SubagentConfig,
    name, description, model, system_prompt, toolsets,
    shared_memory, max_iterations, dangerous)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::budget::ModelPricing,
    model_id, input_per_mtok, output_per_mtok,
    cache_read_per_mtok, cache_write_per_mtok)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::net::TlsConfig,
    ca_cert_path, client_cert_path, client_key_path, verify_server)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::SecurityConfig::Approval,
    mode, auto_approve_dangerous)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::SkillConfig,
    enable, dirs, disabled, template_vars, inline_shell, inline_shell_timeout,
    plugin_mirrors)

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(ea::config::WebConfig,
    search_backend, searxng_url, exa_api_key, parallel_api_key)

// ============================================================================
// Manual specializations — structs with non-trivial mapping
// ============================================================================

namespace toml {

// --- MemoryStrategyConfig (manual: all keys optional, defaults from struct) ---
template<>
struct from<ea::config::MemoryStrategyConfig> {
    template<typename TC>
    static ea::config::MemoryStrategyConfig from_toml(const basic_value<TC>& v) {
        ea::config::MemoryStrategyConfig cfg;
        cfg.type                   = toml::find_or<std::string>(v, "type", cfg.type);
        cfg.working_turns          = toml::find_or<int>(v, "working_turns", cfg.working_turns);
        cfg.short_term_max         = toml::find_or<int>(v, "short_term_max", cfg.short_term_max);
        cfg.long_term_importance   = toml::find_or<int>(v, "long_term_importance", cfg.long_term_importance);
        cfg.enable_fact_extraction = toml::find_or<bool>(v, "enable_fact_extraction", cfg.enable_fact_extraction);
        cfg.enable_auto_summarize  = toml::find_or<bool>(v, "enable_auto_summarize", cfg.enable_auto_summarize);
        cfg.fact_extraction_prompt = toml::find_or<std::string>(
            v, "fact_extraction_prompt", cfg.fact_extraction_prompt);
        return cfg;
    }
};

template<>
struct into<ea::config::MemoryStrategyConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::MemoryStrategyConfig& cfg) {
        basic_value<TC> v;
        v["type"]                   = cfg.type;
        v["working_turns"]          = cfg.working_turns;
        v["short_term_max"]         = cfg.short_term_max;
        v["long_term_importance"]   = cfg.long_term_importance;
        v["enable_fact_extraction"] = cfg.enable_fact_extraction;
        v["enable_auto_summarize"]  = cfg.enable_auto_summarize;
        v["fact_extraction_prompt"] = cfg.fact_extraction_prompt;
        return v;
    }
};

// --- AgentConfig::Compression (manual: legacy max_tokens/keep_recent_turns tolerated) ---
template<>
struct from<ea::config::AgentConfig::Compression> {
    template<typename TC>
    static ea::config::AgentConfig::Compression from_toml(const basic_value<TC>& v) {
        ea::config::AgentConfig::Compression cfg;
        cfg.enable                 = toml::find_or<bool>(v, "enable", cfg.enable);
        cfg.context_length         = toml::find_or<int>(v, "context_length", cfg.context_length);
        cfg.threshold_percent      = toml::find_or<double>(v, "threshold_percent", cfg.threshold_percent);
        cfg.target_ratio           = toml::find_or<double>(v, "target_ratio", cfg.target_ratio);
        cfg.protect_first_n        = toml::find_or<int>(v, "protect_first_n", cfg.protect_first_n);
        cfg.protect_last_n         = toml::find_or<int>(v, "protect_last_n", cfg.protect_last_n);
        cfg.max_summary_tokens     = toml::find_or<int>(v, "max_summary_tokens", cfg.max_summary_tokens);
        cfg.abort_on_summary_failure = toml::find_or<bool>(
            v, "abort_on_summary_failure", cfg.abort_on_summary_failure);
        cfg.in_place               = toml::find_or<bool>(v, "in_place", cfg.in_place);
        return cfg;
    }
};

template<>
struct into<ea::config::AgentConfig::Compression> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::AgentConfig::Compression& cfg) {
        basic_value<TC> v;
        v["enable"]                    = cfg.enable;
        v["context_length"]            = cfg.context_length;
        v["threshold_percent"]         = cfg.threshold_percent;
        v["target_ratio"]              = cfg.target_ratio;
        v["protect_first_n"]           = cfg.protect_first_n;
        v["protect_last_n"]            = cfg.protect_last_n;
        v["max_summary_tokens"]        = cfg.max_summary_tokens;
        v["abort_on_summary_failure"]  = cfg.abort_on_summary_failure;
        v["in_place"]                  = cfg.in_place;
        return v;
    }
};

// --- RetryPolicy (chrono::milliseconds fields stored as _ms integers in TOML) ---
// Defined before ProviderConfig (which nests it).
template<>
struct from<ea::net::RetryPolicy> {
    template<typename TC>
    static ea::net::RetryPolicy from_toml(const basic_value<TC>& v) {
        ea::net::RetryPolicy cfg;
        cfg.max_retries        = toml::find_or<int>(v, "max_retries", cfg.max_retries);
        cfg.base_delay         = std::chrono::milliseconds(
            toml::find_or<int>(v, "base_delay_ms", 1000));
        cfg.backoff_multiplier = toml::find_or<double>(v, "backoff_multiplier", cfg.backoff_multiplier);
        cfg.max_delay          = std::chrono::milliseconds(
            toml::find_or<int>(v, "max_delay_ms", 30000));
        return cfg;
    }
};

template<>
struct into<ea::net::RetryPolicy> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::net::RetryPolicy& cfg) {
        basic_value<TC> v;
        v["max_retries"]        = cfg.max_retries;
        v["base_delay_ms"]      = static_cast<int>(cfg.base_delay.count());
        v["backoff_multiplier"] = cfg.backoff_multiplier;
        v["max_delay_ms"]       = static_cast<int>(cfg.max_delay.count());
        return v;
    }
};

// --- ProviderConfig (timeout: seconds in TOML ↔ milliseconds in C++; nests tls/retry) ---
template<>
struct from<ea::config::ProviderConfig> {
    template<typename TC>
    static ea::config::ProviderConfig from_toml(const basic_value<TC>& v) {
        ea::config::ProviderConfig cfg;
        cfg.type          = toml::find_or<std::string>(v, "type", cfg.type);
        cfg.base_url      = toml::find_or<std::string>(v, "base_url", cfg.base_url);
        cfg.api_key       = toml::find_or<std::string>(v, "api_key", cfg.api_key);
        cfg.default_model = toml::find_or<std::string>(v, "default_model", cfg.default_model);
        cfg.timeout       = std::chrono::milliseconds(
            toml::find_or<int>(v, "timeout", 60) * 1000);
        if (v.contains("tls")) {
            cfg.tls = toml::find<ea::net::TlsConfig>(v, "tls");
        }
        if (v.contains("retry")) {
            cfg.retry = toml::find<ea::net::RetryPolicy>(v, "retry");
        }
        return cfg;
    }
};

template<>
struct into<ea::config::ProviderConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::ProviderConfig& cfg) {
        basic_value<TC> v;
        v["type"] = cfg.type;
        if (!cfg.base_url.empty())       v["base_url"]      = cfg.base_url;
        if (!cfg.api_key.empty())        v["api_key"]       = cfg.api_key;
        if (!cfg.default_model.empty())  v["default_model"] = cfg.default_model;
        v["timeout"] = static_cast<int>(cfg.timeout.count() / 1000);
        v["tls"]     = into<ea::net::TlsConfig>::into_toml<TC>(cfg.tls);
        v["retry"]   = into<ea::net::RetryPolicy>::into_toml<TC>(cfg.retry);
        return v;
    }
};

// --- MemoryConfig (nests MemoryStrategyConfig) ---
template<>
struct from<ea::config::MemoryConfig> {
    template<typename TC>
    static ea::config::MemoryConfig from_toml(const basic_value<TC>& v) {
        ea::config::MemoryConfig cfg;
        cfg.path            = toml::find_or<std::string>(v, "path", cfg.path);
        cfg.enable_wal      = toml::find_or<bool>(v, "enable_wal", cfg.enable_wal);
        cfg.trust_positive  = toml::find_or<double>(v, "trust_positive", cfg.trust_positive);
        cfg.trust_negative  = toml::find_or<double>(v, "trust_negative", cfg.trust_negative);
        cfg.hrr_dim         = toml::find_or<int>(v, "hrr_dim", cfg.hrr_dim);
        if (v.contains("strategy")) {
            cfg.strategy = toml::find<ea::config::MemoryStrategyConfig>(v, "strategy");
        }
        if (v.contains("files")) {
            cfg.files = toml::find<ea::config::MemoryFilesConfig>(v, "files");
        }
        return cfg;
    }
};

template<>
struct into<ea::config::MemoryConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::MemoryConfig& cfg) {
        basic_value<TC> v;
        if (!cfg.path.empty()) v["path"]           = cfg.path;
        v["enable_wal"]      = cfg.enable_wal;
        v["trust_positive"]  = cfg.trust_positive;
        v["trust_negative"]  = cfg.trust_negative;
        v["hrr_dim"]         = cfg.hrr_dim;
        v["files"]           = into<ea::config::MemoryFilesConfig>::into_toml<TC>(cfg.files);
        v["strategy"]        = into<ea::config::MemoryStrategyConfig>::into_toml<TC>(cfg.strategy);
        return v;
    }
};

// --- AgentConfig (nests Compression + vector<SubagentConfig>) ---
template<>
struct from<ea::config::AgentConfig> {
    template<typename TC>
    static ea::config::AgentConfig from_toml(const basic_value<TC>& v) {
        ea::config::AgentConfig cfg;
        cfg.model          = toml::find_or<std::string>(v, "model", cfg.model);
        cfg.max_iterations = toml::find_or<int>(v, "max_iterations", cfg.max_iterations);
        cfg.auto_memory    = toml::find_or<bool>(v, "auto_memory", cfg.auto_memory);
        cfg.soul           = toml::find_or<std::string>(v, "soul", cfg.soul);
        cfg.context_file_max_chars = toml::find_or<int>(v, "context_file_max_chars", cfg.context_file_max_chars);
        cfg.stream         = toml::find_or<bool>(v, "stream", cfg.stream);
        cfg.max_tokens     = toml::find_or<int>(v, "max_tokens", cfg.max_tokens);
        if (v.contains("compression")) {
            cfg.compression = toml::find<ea::config::AgentConfig::Compression>(v, "compression");
        }
        if (v.contains("subagents")) {
            cfg.subagents = toml::find<std::vector<ea::agent::SubagentConfig>>(v, "subagents");
        }
        return cfg;
    }
};

template<>
struct into<ea::config::AgentConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::AgentConfig& cfg) {
        basic_value<TC> v;
        if (!cfg.model.empty()) v["model"] = cfg.model;
        v["max_iterations"] = cfg.max_iterations;
        v["auto_memory"]    = cfg.auto_memory;
        if (!cfg.soul.empty()) v["soul"] = cfg.soul;
        v["context_file_max_chars"] = cfg.context_file_max_chars;
        v["stream"]         = cfg.stream;
        if (cfg.max_tokens > 0) v["max_tokens"] = cfg.max_tokens;
        v["compression"]    = into<ea::config::AgentConfig::Compression>::into_toml<TC>(cfg.compression);
        if (!cfg.subagents.empty()) {
            typename basic_value<TC>::array_type arr;
            for (const auto& s : cfg.subagents) {
                arr.push_back(into<ea::agent::SubagentConfig>::template into_toml<TC>(s));
            }
            v["subagents"] = basic_value<TC>(std::move(arr));
        }
        return v;
    }
};

// --- SecurityConfig (nests Approval) ---
template<>
struct from<ea::config::SecurityConfig> {
    template<typename TC>
    static ea::config::SecurityConfig from_toml(const basic_value<TC>& v) {
        ea::config::SecurityConfig cfg;
        cfg.autonomy         = toml::find_or<std::string>(v, "autonomy", cfg.autonomy);
        cfg.approval_timeout = toml::find_or<int>(v, "approval_timeout", cfg.approval_timeout);
        if (v.contains("allowed_commands")) {
            cfg.allowed_commands = toml::find<std::vector<std::string>>(v, "allowed_commands");
        }
        if (v.contains("approval")) {
            cfg.approval = toml::find<ea::config::SecurityConfig::Approval>(v, "approval");
        }
        return cfg;
    }
};

template<>
struct into<ea::config::SecurityConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::SecurityConfig& cfg) {
        basic_value<TC> v;
        v["autonomy"] = cfg.autonomy;
        if (!cfg.allowed_commands.empty()) {
            v["allowed_commands"] = cfg.allowed_commands;
        }
        v["approval_timeout"] = cfg.approval_timeout;
        v["approval"] = into<ea::config::SecurityConfig::Approval>::into_toml<TC>(cfg.approval);
        return v;
    }
};

// --- McpServerConfig (env is std::map<string,string>) ---
template<>
struct from<ea::config::McpServerConfig> {
    template<typename TC>
    static ea::config::McpServerConfig from_toml(const basic_value<TC>& v) {
        ea::config::McpServerConfig cfg;
        cfg.name      = toml::find<std::string>(v, "name");
        cfg.command   = toml::find<std::string>(v, "command");
        cfg.args      = toml::find_or<std::vector<std::string>>(v, "args", cfg.args);
        cfg.dangerous = toml::find_or<bool>(v, "dangerous", cfg.dangerous);
        if (v.contains("env")) {
            auto env_table = toml::find<toml::table>(v, "env");
            for (const auto& [k, val] : env_table) {
                cfg.env[k] = val.as_string();
            }
        }
        return cfg;
    }
};

template<>
struct into<ea::config::McpServerConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::McpServerConfig& cfg) {
        basic_value<TC> v;
        v["name"]    = cfg.name;
        v["command"] = cfg.command;
        if (!cfg.args.empty()) v["args"] = cfg.args;
        if (!cfg.env.empty()) {
            basic_value<TC> env_tbl;
            for (const auto& [k, val] : cfg.env) {
                env_tbl[k] = val;
            }
            v["env"] = env_tbl;
        }
        v["dangerous"] = cfg.dangerous;
        return v;
    }
};

// --- BudgetConfig (has vector<ModelPricing>) ---
template<>
struct from<ea::budget::BudgetConfig> {
    template<typename TC>
    static ea::budget::BudgetConfig from_toml(const basic_value<TC>& v) {
        ea::budget::BudgetConfig cfg;
        cfg.path               = toml::find_or<std::string>(v, "path", cfg.path);
        cfg.warn_input_tokens  = toml::find_or<int>(v, "warn_input_tokens", cfg.warn_input_tokens);
        cfg.warn_output_tokens = toml::find_or<int>(v, "warn_output_tokens", cfg.warn_output_tokens);
        cfg.warn_cost_usd      = toml::find_or<double>(v, "warn_cost_usd", cfg.warn_cost_usd);
        cfg.max_input_tokens   = toml::find_or<int>(v, "max_input_tokens", cfg.max_input_tokens);
        cfg.max_output_tokens  = toml::find_or<int>(v, "max_output_tokens", cfg.max_output_tokens);
        cfg.max_cost_usd       = toml::find_or<double>(v, "max_cost_usd", cfg.max_cost_usd);
        if (v.contains("pricing")) {
            cfg.pricing = toml::find<std::vector<ea::budget::ModelPricing>>(v, "pricing");
        }
        return cfg;
    }
};

template<>
struct into<ea::budget::BudgetConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::budget::BudgetConfig& cfg) {
        basic_value<TC> v;
        if (!cfg.path.empty()) v["path"] = cfg.path;
        v["warn_input_tokens"]  = cfg.warn_input_tokens;
        v["warn_output_tokens"] = cfg.warn_output_tokens;
        v["warn_cost_usd"]      = cfg.warn_cost_usd;
        v["max_input_tokens"]   = cfg.max_input_tokens;
        v["max_output_tokens"]  = cfg.max_output_tokens;
        v["max_cost_usd"]       = cfg.max_cost_usd;
        if (!cfg.pricing.empty()) {
            typename basic_value<TC>::array_type arr;
            for (const auto& p : cfg.pricing) {
                arr.push_back(into<ea::budget::ModelPricing>::template into_toml<TC>(p));
            }
            v["pricing"] = basic_value<TC>(std::move(arr));
        }
        return v;
    }
};

// --- AppConfig (top-level assembly; config_path is NOT serialized) ---
template<>
struct from<ea::config::AppConfig> {
    template<typename TC>
    static ea::config::AppConfig from_toml(const basic_value<TC>& v) {
        ea::config::AppConfig cfg;
        if (v.contains("log"))          cfg.log          = toml::find<ea::config::LogConfig>(v, "log");
        if (v.contains("trace"))        cfg.trace        = toml::find<ea::config::TraceConfig>(v, "trace");
        if (v.contains("agent"))        cfg.agent        = toml::find<ea::config::AgentConfig>(v, "agent");
        if (v.contains("provider"))     cfg.provider     = toml::find<ea::config::ProviderConfig>(v, "provider");
        if (v.contains("memory"))       cfg.memory       = toml::find<ea::config::MemoryConfig>(v, "memory");
        if (v.contains("onboarding"))   cfg.onboarding   = toml::find<ea::config::OnboardingConfig>(v, "onboarding");
        if (v.contains("security"))     cfg.security     = toml::find<ea::config::SecurityConfig>(v, "security");
        if (v.contains("skills"))       cfg.skills       = toml::find<ea::config::SkillConfig>(v, "skills");
        if (v.contains("web"))          cfg.web          = toml::find<ea::config::WebConfig>(v, "web");
        if (v.contains("conversation")) cfg.conversation = toml::find<ea::config::ConversationConfig>(v, "conversation");
        if (v.contains("budget"))       cfg.budget       = toml::find<ea::budget::BudgetConfig>(v, "budget");
        if (v.contains("mcp")) {
            const auto& mcp = v.at("mcp");
            if (mcp.contains("servers")) {
                cfg.mcp_servers = toml::find<std::vector<ea::config::McpServerConfig>>(mcp, "servers");
            }
        }
        return cfg;
    }
};

template<>
struct into<ea::config::AppConfig> {
    template<typename TC>
    static basic_value<TC> into_toml(const ea::config::AppConfig& cfg) {
        basic_value<TC> v;
        v["log"]          = into<ea::config::LogConfig>::into_toml<TC>(cfg.log);
        v["trace"]        = into<ea::config::TraceConfig>::into_toml<TC>(cfg.trace);
        v["agent"]        = into<ea::config::AgentConfig>::into_toml<TC>(cfg.agent);
        v["provider"]     = into<ea::config::ProviderConfig>::into_toml<TC>(cfg.provider);
        v["memory"]       = into<ea::config::MemoryConfig>::into_toml<TC>(cfg.memory);
        v["onboarding"]   = into<ea::config::OnboardingConfig>::into_toml<TC>(cfg.onboarding);
        v["security"]     = into<ea::config::SecurityConfig>::into_toml<TC>(cfg.security);
        v["skills"]       = into<ea::config::SkillConfig>::into_toml<TC>(cfg.skills);
        v["web"]          = into<ea::config::WebConfig>::into_toml<TC>(cfg.web);
        v["conversation"] = into<ea::config::ConversationConfig>::into_toml<TC>(cfg.conversation);
        // Budget section — only if non-trivial
        if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
            v["budget"] = into<ea::budget::BudgetConfig>::into_toml<TC>(cfg.budget);
        }
        // MCP section — only if configured
        if (!cfg.mcp_servers.empty()) {
            typename basic_value<TC>::array_type arr;
            for (const auto& s : cfg.mcp_servers) {
                arr.push_back(into<ea::config::McpServerConfig>::template into_toml<TC>(s));
            }
            basic_value<TC> mcp_tbl;
            mcp_tbl["servers"] = basic_value<TC>(std::move(arr));
            v["mcp"] = mcp_tbl;
        }
        // config_path is NOT serialized — it's runtime metadata
        return v;
    }
};

}  // namespace toml
