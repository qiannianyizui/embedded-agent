// IEventListener — interface for agent lifecycle event consumers
#pragma once
#include "AgentEvent.h"

namespace ea::agent {

class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void on_event(const AgentEvent& event) = 0;
};

}  // namespace ea::agent
