#pragma once
#include "common/base/Error.h"

namespace ea::provider {

enum class ErrorClass { Retryable, NonRetryable, RateLimit };

ErrorClass classify_error(const Error& err);

}  // namespace ea::provider
