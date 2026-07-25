// BudgetEventListener — prints usage/cost after each LLM call
#pragma once
#include "IEventListener.h"
#include "budget/BudgetTracker.h"
#include <iostream>
#include <iomanip>

namespace ea::agent {

class BudgetEventListener : public IEventListener {
public:
    explicit BudgetEventListener(budget::BudgetTracker* tracker) : tracker_(tracker) {}

    void on_event(const AgentEvent& event) override {
        if (event.type == AgentEventType::LLMResponse) {
            if (event.usage.input_tokens > 0 || event.usage.output_tokens > 0) {
                auto su = tracker_->session_usage();
                auto sc = tracker_->session_cost();
                auto old_flags = std::cout.flags();
                auto old_precision = std::cout.precision();
                std::cout << "\n[Usage: " << su.input_tokens << " in / "
                          << su.output_tokens << " out | $"
                          << std::fixed << std::setprecision(4) << sc.total()
                          << " session]" << std::flush;
                std::cout.flags(old_flags);
                std::cout.precision(old_precision);
            }
        }
    }

private:
    budget::BudgetTracker* tracker_;
};

}  // namespace ea::agent
