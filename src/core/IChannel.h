#pragma once
#include "common/base/Result.h"
#include <string>

namespace ea {

class IChannel {
public:
    virtual ~IChannel() = default;
    virtual std::string name() const = 0;
    virtual Result<void> send(const std::string& message) = 0;
};

}  // namespace ea
