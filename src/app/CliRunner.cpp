#include "app/CliRunner.h"
#include "app/AppContext.h"
#include "app/auto_resume.h"
#include "agent/AgentLoop.h"
#include "agent/LoggingEventListener.h"
#include "agent/BudgetEventListener.h"
#include "common/io/Logger.h"
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>

namespace ea::app {

void CliRunner::register_commands(AppContext& ctx) {
    commands_ = std::make_unique<CliCommandRegistry>();

    commands_->register_command({
        "quit", "/quit", "Exit the agent",
        [](const std::string&) { /* handled by return value */ }
    });
    commands_->register_command({
        "exit", "/exit", "Exit the agent",
        [](const std::string&) { /* handled by return value */ }
    });
    commands_->register_command({
        "usage", "/usage [global]", "Show token usage statistics",
        [&ctx](const std::string& args) {
            if (!ctx.budget_tracker) {
                std::cout << "Budget tracking not enabled" << std::endl;
                return;
            }
            if (args == "global") {
                auto gu = ctx.budget_tracker->global_usage();
                std::cout << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            } else {
                auto su = ctx.budget_tracker->session_usage();
                auto gu = ctx.budget_tracker->global_usage();
                std::cout << "Session Usage:\n"
                          << "  Input:  " << su.input_tokens << " tokens\n"
                          << "  Output: " << su.output_tokens << " tokens\n"
                          << "  Cache:  " << su.cache_read_tokens << " read / "
                          << su.cache_write_tokens << " write\n"
                          << "  Total:  " << su.total_tokens() << " tokens\n\n"
                          << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            }
        }
    });
    commands_->register_command({
        "cost", "/cost [global]", "Show cost breakdown",
        [&ctx](const std::string& args) {
            if (!ctx.budget_tracker) {
                std::cout << "Budget tracking not enabled" << std::endl;
                return;
            }
            auto old_flags = std::cout.flags();
            auto old_precision = std::cout.precision();
            if (args == "global") {
                auto gc = ctx.budget_tracker->global_cost();
                std::cout << "Global Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << gc.total() << std::endl;
            } else {
                auto sc = ctx.budget_tracker->session_cost();
                auto gc = ctx.budget_tracker->global_cost();
                std::cout << "Session Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << sc.total() << "\n\n"
                          << "Global Cost:\n"
                          << "  Total: $" << gc.total() << std::endl;
            }
            std::cout.flags(old_flags);
            std::cout.precision(old_precision);
        }
    });
    commands_->register_command({
        "history", "/history", "List recent conversations",
        [&ctx](const std::string&) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            auto list = ctx.conversation_store->list(10, 0);
            if (list.ok()) {
                for (const auto& m : list.value()) {
                    std::cout << "  " << m.id << "  " << m.title
                              << "  (" << m.message_count << " msgs, " << m.updated_at << ")" << std::endl;
                }
            }
        }
    });
    commands_->register_command({
        "resume", "/resume <id>", "Resume a conversation",
        [&ctx](const std::string& /*id*/) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            // Note: resume from command needs the loop — handled in run()
        }
    });
    commands_->register_command({
        "export", "/export", "Export current conversation as JSONL",
        [&ctx](const std::string&) {
            // Note: needs loop — handled in run()
        }
    });
    commands_->register_command({
        "import", "/import <file>", "Import conversation from JSONL file",
        [&ctx](const std::string& filepath) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            std::ifstream file(filepath);
            if (!file.is_open()) {
                std::cout << "Cannot open file: " << filepath << std::endl;
                return;
            }
            std::string jsonl_data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
            auto new_id = ctx.conversation_store->import_jsonl(jsonl_data);
            if (new_id.ok()) {
                std::cout << "Imported conversation: " << new_id.value() << std::endl;
            } else {
                std::cout << "Import failed: " << new_id.error().message << std::endl;
            }
        }
    });
    commands_->register_command({
        "help", "/help", "Show available commands",
        [this](const std::string&) { commands_->print_help(); }
    });
}

int CliRunner::run(AppContext& ctx) {
    register_commands(ctx);

    // Create AgentLoop with CLI callbacks
    ea::agent::AgentLoop::StreamFn stream_fn;
    if (ctx.config.agent.stream) {
        stream_fn = [](const ea::StreamChunk& chunk) {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                std::cout << chunk.data << std::flush;
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                std::cout << std::endl;
            }
        };
    }

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{
            ctx.config.agent.max_iterations, 65536, 100, true,
            ctx.config.agent.stream, ctx.config.conversation.auto_persist
        },
        [](const std::string& text) { std::cout << text << std::endl; },
        stream_fn,
        ctx.security.get(),
        ctx.approval.get(),
        ctx.compressor.get(),
        ctx.memory_strategy.get(),
        ctx.conversation_store.get(),
        ctx.budget_tracker.get()
    );

    // Add event listeners
    if (ctx.debug) {
        loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }
    if (ctx.budget_tracker) {
        loop.add_listener(std::make_shared<ea::agent::BudgetEventListener>(ctx.budget_tracker.get()));
    }

    // Auto-resume
    auto_resume(ctx, loop, false);

    // Interactive loop
    std::cout << "embedded-agent v0.1.0 (type /help for commands, /quit to exit)" << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        // Handle quit/exit directly (need to break the loop)
        if (input == "/quit" || input == "/exit") break;

        // Handle /resume with loop reference
        if (input.size() > 8 && input.substr(0, 8) == "/resume ") {
            if (ctx.conversation_store) {
                std::string cid = input.substr(8);
                auto msgs = ctx.conversation_store->load(cid);
                if (msgs.ok()) {
                    loop.restore_conversation(cid, std::move(msgs.value()));
                    auto meta = ctx.conversation_store->get_meta(cid);
                    std::cout << "Resumed: " << (meta.ok() ? meta.value().title : cid) << std::endl;
                } else {
                    std::cout << "Conversation not found: " << cid << std::endl;
                }
            }
            continue;
        }

        // Handle /export with loop reference
        if (input == "/export") {
            if (ctx.conversation_store && !loop.conversation_id().empty()) {
                auto data = ctx.conversation_store->export_jsonl(loop.conversation_id());
                if (data.ok()) {
                    std::cout << data.value();
                } else {
                    std::cout << "Export failed: " << data.error().message << std::endl;
                }
            } else {
                std::cout << "No active conversation to export" << std::endl;
            }
            continue;
        }

        // Try command registry for other commands
        if (commands_->try_dispatch(input)) continue;

        // Run agent
        auto result = loop.run(input);
        if (ctx.budget_tracker && !loop.conversation_id().empty()) {
            ctx.budget_tracker->set_session_id(loop.conversation_id());
        }
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }

    return 0;
}

}  // namespace ea::app
