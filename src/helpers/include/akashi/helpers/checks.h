#pragma once

#include "akashi_core_export.h"

#include <QList>
#include <QStringList>
#include <QVariant>

#include <functional>

// Yes/no checks over a stored value, composable with allOf and emptyOr.
// Config settings declare these; anything else holding a QVariant may use
// them too. Number checks refuse values that are not numbers at all.
namespace akashi::helpers {

using Check = std::function<bool(const QVariant &)>;

// The value must be a number between min and max.
AKASHI_CORE_EXPORT Check inRange(double f_min, double f_max);
// The value must be a number of at least min.
AKASHI_CORE_EXPORT Check atLeast(double f_min);
// The value must be a number of at most max.
AKASHI_CORE_EXPORT Check atMost(double f_max);
// Every given check must pass.
AKASHI_CORE_EXPORT Check allOf(const QList<Check> &f_checks);
// The value must be one of the given words, compared without case.
AKASHI_CORE_EXPORT Check oneOf(const QStringList &f_words);
// The value must be empty or a time of day like 04:30.
AKASHI_CORE_EXPORT Check emptyOrTime();
// The value must be a well-formed http or https URL.
AKASHI_CORE_EXPORT Check url();
// An empty value passes; anything else must pass the given check.
AKASHI_CORE_EXPORT Check emptyOr(Check f_check);

} // namespace akashi::helpers
