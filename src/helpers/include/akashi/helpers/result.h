#pragma once

#include "akashi/helpers/parse_error.h"

#include <QtGlobal>

#include <optional>
#include <utility>

namespace akashi::helpers {

// A read value, or the ParseError saying why there is none. On failure
// there is no value to fall back on, so a missed check cannot quietly
// carry a 0 or a nullptr onward; reading one anyway asserts in debug.
template <typename T>
class Result
{
  public:
    static Result success(T f_value)
    {
        Result l_result;
        l_result.m_value = std::move(f_value);
        return l_result;
    }

    static Result failure(ParseError f_error)
    {
        Q_ASSERT(f_error.error != ParseError::NoError);
        Result l_result;
        l_result.m_error = std::move(f_error);
        return l_result;
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    // Only valid when ok().
    const T &value() const
    {
        Q_ASSERT_X(m_value.has_value(), "helpers::Result", "value read without checking ok() first");
        return *m_value;
    }
    const T &operator*() const { return value(); }
    const T *operator->() const { return &value(); }

    // NoError when ok().
    const ParseError &error() const { return m_error; }

  private:
    Result() = default;

    std::optional<T> m_value;
    ParseError m_error;
};

} // namespace akashi::helpers
