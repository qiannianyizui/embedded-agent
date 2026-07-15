#include "ErrorClassifier.h"

namespace ea::provider {

ErrorClass classify_error(const Error& err) {
    if (err.code == ErrorCode::RateLimit) return ErrorClass::RateLimit;
    if (err.code == ErrorCode::Timeout) return ErrorClass::Retryable;
    if (err.code == ErrorCode::NetworkError && err.http_status >= 500 && err.http_status < 600) {
        return ErrorClass::Retryable;
    }
    return ErrorClass::NonRetryable;
}

}  // namespace ea::provider
