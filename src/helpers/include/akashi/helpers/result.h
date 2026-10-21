#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace akashi::helpers {

// A read value or the reason it could not be read. The reason is written
// for the person who typed the input, so a caller can reply with it as is.
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

    static Result failure(QString f_reason)
    {
        Result l_result;
        l_result.m_error = std::move(f_reason);
        return l_result;
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    // Only valid when ok().
    const T &value() const { return *m_value; }
    const T &operator*() const { return *m_value; }
    const T *operator->() const { return &*m_value; }

    T valueOr(T f_fallback) const { return m_value.value_or(std::move(f_fallback)); }

    // Empty when ok().
    const QString &error() const { return m_error; }

  private:
    Result() = default;

    std::optional<T> m_value;
    QString m_error;
};

} // namespace akashi::helpers
