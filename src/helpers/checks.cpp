#include "akashi/helpers/checks.h"

#include <QTime>
#include <QUrl>

#include <optional>

namespace akashi::helpers {

// The value as a number, or nothing when it is not one - so "abc" never
// passes a range check by reading as zero.
static std::optional<double> numberOf(const QVariant &f_value)
{
    bool l_ok = false;
    const double l_number = f_value.toDouble(&l_ok);
    if (!l_ok) {
        return std::nullopt;
    }
    return l_number;
}

Check inRange(double f_min, double f_max)
{
    return [f_min, f_max](const QVariant &f_value) {
        const std::optional<double> l_number = numberOf(f_value);
        return l_number && *l_number >= f_min && *l_number <= f_max;
    };
}

Check atLeast(double f_min)
{
    return [f_min](const QVariant &f_value) {
        const std::optional<double> l_number = numberOf(f_value);
        return l_number && *l_number >= f_min;
    };
}

Check atMost(double f_max)
{
    return [f_max](const QVariant &f_value) {
        const std::optional<double> l_number = numberOf(f_value);
        return l_number && *l_number <= f_max;
    };
}

Check allOf(const QList<Check> &f_checks)
{
    return [f_checks](const QVariant &f_value) {
        for (const Check &l_check : f_checks) {
            if (l_check && !l_check(f_value)) {
                return false;
            }
        }
        return true;
    };
}

Check oneOf(const QStringList &f_words)
{
    return [f_words](const QVariant &f_value) {
        return f_words.contains(f_value.toString(), Qt::CaseInsensitive);
    };
}

Check emptyOrTime()
{
    return [](const QVariant &f_value) {
        return f_value.toString().isEmpty() || f_value.toTime().isValid();
    };
}

Check url()
{
    return [](const QVariant &f_value) {
        const QUrl l_url(f_value.toString(), QUrl::StrictMode);
        return l_url.isValid() && !l_url.host().isEmpty() &&
               (l_url.scheme() == QStringLiteral("http") || l_url.scheme() == QStringLiteral("https"));
    };
}

Check emptyOr(Check f_check)
{
    return [f_check = std::move(f_check)](const QVariant &f_value) {
        return f_value.toString().isEmpty() || !f_check || f_check(f_value);
    };
}

} // namespace akashi::helpers
