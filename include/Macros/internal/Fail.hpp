// IWYU pragma: private

#pragma once

#define INTERNAL_FAIL_IF_IMPL(cond, why, action, msg, ...)                     \
    do {                                                                       \
        if (cond) {                                                            \
            LOG_LOC("%s: " msg, why, ##__VA_ARGS__);                           \
            action;                                                            \
        }                                                                      \
    } while (0)

#define INTERNAL_FAIL_IF_IMPL_CODE(cond, why, action, msg, code, ...)          \
    do {                                                                       \
        if (cond) {                                                            \
            LOG_LOC("%s: [%d] " msg, why, code, ##__VA_ARGS__);                \
            action;                                                            \
        }                                                                      \
    } while (0)

#define INTERNAL_FAIL_IF_IMPL_ERR(cond, why, action, msg, err, ...)            \
    do {                                                                       \
        if (cond) {                                                            \
            LOG_LOC("%s: " ERR_FMT " " msg, why, ERR_ARG(err), ##__VA_ARGS__); \
            action;                                                            \
        }                                                                      \
    } while (0)

// Fail if active
#define FAIL_IF_ACTIVE_THEN(action, msg, ...)                                  \
    INTERNAL_FAIL_IF_IMPL(this->_life.active(), "active", action, msg,         \
                          ##__VA_ARGS__)
#define ABORT_IF_ACTIVE(msg, ...)                                              \
    FAIL_IF_ACTIVE_THEN(abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_ACTIVE(ret, msg, ...)                                          \
    FAIL_IF_ACTIVE_THEN(return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_ACTIVE_VOID(msg, ...)                                          \
    FAIL_IF_ACTIVE_THEN(return, msg, ##__VA_ARGS__)

// Fail if self active
#define FAIL_IF_SELF_ACTIVE_THEN(action, msg, ...)                             \
    INTERNAL_FAIL_IF_IMPL(self->_life.active(), "self active", action, msg,    \
                          ##__VA_ARGS__)
#define ABORT_IF_SELF_ACTIVE(msg, ...)                                         \
    FAIL_IF_SELF_ACTIVE_THEN(abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_ACTIVE(ret, msg, ...)                                     \
    FAIL_IF_SELF_ACTIVE_THEN(return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_ACTIVE_VOID(msg, ...)                                     \
    FAIL_IF_SELF_ACTIVE_THEN(return, msg, ##__VA_ARGS__)

// Fail if not active
#define FAIL_IF_INACTIVE_THEN(action, msg, ...)                                \
    INTERNAL_FAIL_IF_IMPL(!this->_life.active(), "not active", action, msg,    \
                          ##__VA_ARGS__)
#define ABORT_IF_INACTIVE(msg, ...)                                            \
    FAIL_IF_INACTIVE_THEN(abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_INACTIVE(ret, msg, ...)                                        \
    FAIL_IF_INACTIVE_THEN(return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_INACTIVE_VOID(msg, ...)                                        \
    FAIL_IF_INACTIVE_THEN(return, msg, ##__VA_ARGS__)

// Common shorthands for lifecycle active state checks
#define FAIL_IF_INACTIVE_ERR(msg, ...)                                         \
    FAIL_IF_INACTIVE_THEN(return ERR(LifecycleError, NotActive), msg,          \
                                 ##__VA_ARGS__)
#define FAIL_IF_INACTIVE_UNEXPECTED(msg, ...)                                  \
    FAIL_IF_INACTIVE_THEN(                                                     \
        return std::unexpected(ERR(LifecycleError, NotActive)), msg,           \
               ##__VA_ARGS__)

// Fail if self not active
#define FAIL_IF_SELF_INACTIVE_THEN(action, msg, ...)                           \
    INTERNAL_FAIL_IF_IMPL(!self->_life.active(), "self not active", action,    \
                          msg, ##__VA_ARGS__)
#define ABORT_IF_SELF_INACTIVE(msg, ...)                                       \
    FAIL_IF_SELF_INACTIVE_THEN(abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_INACTIVE(ret, msg, ...)                                   \
    FAIL_IF_SELF_INACTIVE_THEN(return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_INACTIVE_VOID(msg, ...)                                   \
    FAIL_IF_SELF_INACTIVE_THEN(return, msg, ##__VA_ARGS__)

// Common shorthands for self lifecycle active state checks
#define FAIL_IF_SELF_INACTIVE_ERR(msg, ...)                                    \
    FAIL_IF_SELF_INACTIVE_THEN(return ERR(LifecycleError, NotActive), msg,     \
                                      ##__VA_ARGS__)
#define FAIL_IF_SELF_INACTIVE_UNEXPECTED(msg, ...)                             \
    FAIL_IF_SELF_INACTIVE_THEN(                                                \
        return std::unexpected(ERR(LifecycleError, NotActive)), msg,           \
               ##__VA_ARGS__)

// Fail if null
#define FAIL_IF_NULL_THEN(what, action, msg, ...)                              \
    INTERNAL_FAIL_IF_IMPL((what) == nullptr, "null", action, msg, ##__VA_ARGS__)
#define ABORT_IF_NULL(what, msg, ...)                                          \
    FAIL_IF_NULL_THEN(what, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_NULL(what, ret, msg, ...)                                      \
    FAIL_IF_NULL_THEN(what, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_NULL_VOID(what, msg, ...)                                      \
    FAIL_IF_NULL_THEN(what, return, msg, ##__VA_ARGS__)

// Fail if not null
#define FAIL_IF_NOT_NULL_THEN(what, action, msg, ...)                          \
    INTERNAL_FAIL_IF_IMPL((what) != nullptr, "not null", action, msg,          \
                          ##__VA_ARGS__)
#define ABORT_IF_NOT_NULL(what, msg, ...)                                      \
    FAIL_IF_NOT_NULL_THEN(what, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_NULL(what, ret, msg, ...)                                  \
    FAIL_IF_NOT_NULL_THEN(what, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_NULL_VOID(what, msg, ...)                                  \
    FAIL_IF_NOT_NULL_THEN(what, return, msg, ##__VA_ARGS__)

// Unconditional fail
#define FAIL_THEN(action, msg, ...)                                            \
    INTERNAL_FAIL_IF_IMPL(true, "unconditional", action, msg, ##__VA_ARGS__)
#define ABORT(msg, ...) FAIL_THEN(abort(), msg, ##__VA_ARGS__)
#define FAIL(ret, msg, ...) FAIL_THEN(return (ret), msg, ##__VA_ARGS__)
#define FAIL_VOID(msg, ...) FAIL_THEN(return, msg, ##__VA_ARGS__)

// Fail if condition
#define FAIL_IF_THEN(cond, action, msg, ...)                                   \
    INTERNAL_FAIL_IF_IMPL(cond, "condition true", action, msg, ##__VA_ARGS__)
#define ABORT_IF(cond, msg, ...) FAIL_IF_THEN(cond, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF(cond, ret, msg, ...)                                           \
    FAIL_IF_THEN(cond, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_VOID(cond, msg, ...)                                           \
    FAIL_IF_THEN(cond, return, msg, ##__VA_ARGS__)

// Fail if not condition
#define FAIL_IF_NOT_THEN(cond, action, msg, ...)                               \
    INTERNAL_FAIL_IF_IMPL(!(cond), "condition false", action, msg,             \
                          ##__VA_ARGS__)
#define ABORT_IF_NOT(cond, msg, ...)                                           \
    FAIL_IF_NOT_THEN(cond, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT(cond, ret, msg, ...)                                       \
    FAIL_IF_NOT_THEN(cond, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_VOID(cond, msg, ...)                                       \
    FAIL_IF_NOT_THEN(cond, return, msg, ##__VA_ARGS__)

// Fail if is value
#define FAIL_IF_IS_THEN(what, against, action, msg, ...)                       \
    INTERNAL_FAIL_IF_IMPL((what) == (against), "value is " #against, action,   \
                          msg, ##__VA_ARGS__)
#define ABORT_IF_IS(what, against, msg, ...)                                   \
    FAIL_IF_IS_THEN(what, against, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_IS(what, against, ret, msg, ...)                               \
    FAIL_IF_IS_THEN(what, against, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_IS_VOID(what, against, msg, ...)                               \
    FAIL_IF_IS_THEN(what, against, return, msg, ##__VA_ARGS__)

// Fail if is not value
#define FAIL_IF_IS_NOT_THEN(what, against, action, msg, ...)                   \
    INTERNAL_FAIL_IF_IMPL((what) != (against), "value is not " #against,       \
                          action, msg, ##__VA_ARGS__)
#define ABORT_IF_IS_NOT(what, against, msg, ...)                               \
    FAIL_IF_IS_NOT_THEN(what, against, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_IS_NOT(what, against, ret, msg, ...)                           \
    FAIL_IF_IS_NOT_THEN(what, against, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_IS_NOT_VOID(what, against, msg, ...)                           \
    FAIL_IF_IS_NOT_THEN(what, against, return, msg, ##__VA_ARGS__)

// Fail if ReturnCode error
#define FAIL_IF_ERR_THEN(expr, action, msg, ...)                               \
    if (auto _rc_ = (expr); !_rc_.ok()) {                                      \
        INTERNAL_FAIL_IF_IMPL_ERR(true, "Error", action, msg, _rc_,            \
                                  ##__VA_ARGS__);                              \
    }
#define ABORT_IF_ERR(expr, msg, ...)                                           \
    FAIL_IF_ERR_THEN(expr, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_ERR(expr, ret, msg, ...)                                       \
    FAIL_IF_ERR_THEN(expr, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_ERR_VOID(expr, msg, ...)                                       \
    FAIL_IF_ERR_THEN(expr, return, msg, ##__VA_ARGS__)
#define FAIL_IF_ERR_FWD(expr, msg, ...)                                        \
    FAIL_IF_ERR_THEN(expr, return _rc_, msg, ##__VA_ARGS__)
#define FAIL_IF_ERR_FWD_UNEXPECTED(expr, msg, ...)                             \
    FAIL_IF_ERR_THEN(expr, return std::unexpected(_rc_), msg, ##__VA_ARGS__)
#define ABORT_IF_ERR_BEGIN(expr) ABORT_IF_ERR(expr, "Failed to begin: " #expr)

// Fail if platform error
#define FAIL_IF_PLATFORM_THEN(expr, action, msg, ...)                          \
    FAIL_IF_ERR_THEN(::platform::map_platform_error(expr), action, msg,        \
                     ##__VA_ARGS__)
#define ABORT_IF_ERR_PLATFORM(expr, msg, ...)                                  \
    FAIL_IF_PLATFORM_THEN(expr, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_PLATFORM(what, ret, msg, ...)                                  \
    FAIL_IF_PLATFORM_THEN(what, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_PLATFORM_VOID(what, msg, ...)                                  \
    FAIL_IF_PLATFORM_THEN(what, return, msg, ##__VA_ARGS__)
#define FAIL_IF_PLATFORM_FWD(what, msg, ...)                                   \
    FAIL_IF_PLATFORM_THEN(what, return _rc_, msg, ##__VA_ARGS__)
#define FAIL_IF_PLATFORM_FWD_UNEXPECTED(what, msg, ...)                        \
    FAIL_IF_PLATFORM_THEN(what, return std::unexpected(_rc_), msg,             \
                          ##__VA_ARGS__)

// Fail if optional has no value
#define FAIL_IF_NOT_OPT_THEN(var, expr, action, msg, ...)                      \
    do {                                                                       \
        if (auto _opt_ = (expr); !_opt_.has_value()) {                         \
            INTERNAL_FAIL_IF_IMPL(true, "no value for " #expr, action, msg,    \
                                  ##__VA_ARGS__);                              \
        } else {                                                               \
            (var) = std::move(*_opt_);                                         \
        }                                                                      \
    } while (0)
#define ABORT_IF_NOT_OPT(var, expr, msg, ...)                                  \
    FAIL_IF_NOT_OPT_THEN(var, expr, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_OPT(var, expr, ret, msg, ...)                              \
    FAIL_IF_NOT_OPT_THEN(var, expr, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_OPT_VOID(var, expr, msg, ...)                              \
    FAIL_IF_NOT_OPT_THEN(var, expr, return, msg, ##__VA_ARGS__)

// Fail if std::expected has error and assign value if not
#define FAIL_IF_UNEXPECTED_THEN(var, expr, action, msg, ...)                   \
    auto CONCAT(_result_, var) = (expr);                                       \
    if (!CONCAT(_result_, var)) {                                              \
        auto _error_ = CONCAT(_result_, var).error();                          \
        INTERNAL_FAIL_IF_IMPL_ERR(true, "Unexpected Error", action, msg,       \
                                  _error_, ##__VA_ARGS__);                     \
    }                                                                          \
    auto var = std::move(*CONCAT(_result_, var));
#define ABORT_IF_UNEXPECTED(var, expr, msg, ...)                               \
    FAIL_IF_UNEXPECTED_THEN(var, expr, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_UNEXPECTED(var, expr, ret, msg, ...)                           \
    FAIL_IF_UNEXPECTED_THEN(var, expr, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_UNEXPECTED_VOID(var, expr, msg, ...)                           \
    FAIL_IF_UNEXPECTED_THEN(var, expr, return, msg, ##__VA_ARGS__)
#define FAIL_IF_UNEXPECTED_FWD(var, expr, msg, ...)                            \
    FAIL_IF_UNEXPECTED_THEN(var, expr, return _error_, msg, ##__VA_ARGS__)
#define FAIL_IF_UNEXPECTED_FWD_UNEXPECTED(var, expr, msg, ...)                 \
    FAIL_IF_UNEXPECTED_THEN(var, expr, return std::unexpected(_error_), msg,   \
                            ##__VA_ARGS__)

// Fail if std::expected has error and assign value to existing variable if not
#define FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, action, msg, ...)            \
    do {                                                                       \
        auto CONCAT(_result_, var) = (expr);                                   \
        if (!CONCAT(_result_, var)) {                                          \
            auto _error_ = CONCAT(_result_, var).error();                      \
            INTERNAL_FAIL_IF_IMPL_ERR(true, "Unexpected assign error", action, \
                                      msg, _error_, ##__VA_ARGS__);            \
        }                                                                      \
        (var) = std::move(*CONCAT(_result_, var));                             \
    } while (0)
#define ABORT_IF_ASSIGN_UNEXPECTED(var, expr, msg, ...)                        \
    FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, abort(), msg, ##__VA_ARGS__)
#define FAIL_IF_ASSIGN_UNEXPECTED(var, expr, ret, msg, ...)                    \
    FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_ASSIGN_UNEXPECTED_VOID(var, expr, msg, ...)                    \
    FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, return, msg, ##__VA_ARGS__)
#define FAIL_IF_ASSIGN_UNEXPECTED_FWD(var, expr, msg, ...)                     \
    FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, return _error_, msg,             \
                                   ##__VA_ARGS__)
#define FAIL_IF_ASSIGN_UNEXPECTED_FWD_UNEXPECTED(var, expr, msg, ...)          \
    FAIL_IF_ASSIGN_UNEXPECTED_THEN(var, expr, return std::unexpected(_error_), \
                                   msg, ##__VA_ARGS__)

// Fail if mutex take times out
#define FAIL_IF_MUTEX_TIMEOUT_THEN(timeout, action, msg, ...)                  \
    do {                                                                       \
        auto guard = this->_mutexGuard(timeout);                               \
        if (!guard.acquired()) {                                               \
            INTERNAL_FAIL_IF_IMPL(true, "mutex timeout", action, msg,          \
                                  ##__VA_ARGS__);                              \
        }                                                                      \
    } while (0)
#define FAIL_IF_MUTEX_TIMEOUT(timeout, ret, msg, ...)                          \
    FAIL_IF_MUTEX_TIMEOUT_THEN(timeout, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_MUTEX_TIMEOUT_VOID(timeout, msg, ...)                          \
    FAIL_IF_MUTEX_TIMEOUT_THEN(timeout, return, msg, ##__VA_ARGS__)
#define FAIL_IF_MUTEX_TIMEOUT_DEFAULT(ret, msg, ...)                           \
    FAIL_IF_MUTEX_TIMEOUT_THEN(this->_defaultMutexTimeoutMs, return (ret),     \
                               msg, ##__VA_ARGS__)
#define FAIL_IF_MUTEX_TIMEOUT_DEFAULT_VOID(msg, ...)                           \
    FAIL_IF_MUTEX_TIMEOUT_THEN(this->_defaultMutexTimeoutMs, return, msg,      \
                               ##__VA_ARGS__)

// Fail if mutex take times out
#define FAIL_IF_SELF_MUTEX_TIMEOUT_THEN(timeout, action, msg, ...)             \
    do {                                                                       \
        auto guard = self->_mutexGuard(timeout);                               \
        if (!guard.acquired()) {                                               \
            INTERNAL_FAIL_IF_IMPL(true, "mutex timeout", action, msg,          \
                                  ##__VA_ARGS__);                              \
        }                                                                      \
    } while (0)
#define FAIL_IF_SELF_MUTEX_TIMEOUT(timeout, ret, msg, ...)                     \
    FAIL_IF_SELF_MUTEX_TIMEOUT_THEN(timeout, return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_MUTEX_TIMEOUT_VOID(timeout, msg, ...)                     \
    FAIL_IF_SELF_MUTEX_TIMEOUT_THEN(timeout, return, msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_MUTEX_TIMEOUT_DEFAULT(ret, msg, ...)                      \
    FAIL_IF_SELF_MUTEX_TIMEOUT_THEN(self->_defaultMutexTimeoutMs,              \
                                    return (ret), msg, ##__VA_ARGS__)
#define FAIL_IF_SELF_MUTEX_TIMEOUT_DEFAULT_VOID(msg, ...)                      \
    FAIL_IF_SELF_MUTEX_TIMEOUT_THEN(self->_defaultMutexTimeoutMs, return, msg, \
                                    ##__VA_ARGS__)

// State machine
#define INTERNAL_FAIL_IF_NOT_STATE(var, exp, next, ret, msg, ...)              \
    do {                                                                       \
        auto _expected_ = exp;                                                 \
        INTERNAL_FAIL_IF_IMPL(!var.compare_exchange_strong(                    \
                                  _expected_, next, std::memory_order_acq_rel, \
                                  std::memory_order_acquire),                  \
                              "Cannot step " #var " from " SV_FMT              \
                              " to " SV_FMT " - need " SV_FMT,                 \
                              return ret, msg, MAGIC_SV_ARG(_expected_),       \
                              MAGIC_SV_ARG(next), MAGIC_SV_ARG(exp),           \
                              ##__VA_ARGS__);                                  \
    } while (0)

#define FAIL_IF_NOT_STATE(var, exp, next, msg, ...)                            \
    INTERNAL_FAIL_IF_NOT_STATE(var, exp, next, ERR(CoreError, InvalidState),   \
                               msg, ##__VA_ARGS__)
#define FAIL_IF_NOT_STATE_UNEXPECTED(var, exp, next, msg, ...)                 \
    INTERNAL_FAIL_IF_NOT_STATE(var, exp, next,                                 \
                               std::unexpected(ERR(CoreError, InvalidState)),  \
                               msg, ##__VA_ARGS__)
