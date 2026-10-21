#pragma once

#include "akashi/helpers/result.h"
#include "akashi_core_export.h"

#include <QString>
#include <QStringList>

// Readers for typed input. Each one accepts exactly the documented shape
// and nothing looser, and answers with a reason a player can act on. The
// f_what argument names the value in that reason ("client ID", "area").
namespace akashi::helpers {

// A whole decimal number with an optional sign and no other characters.
// Out-of-range values (beyond int) are refused, never wrapped or zeroed.
AKASHI_CORE_EXPORT Result<int> parseInt(const QString &f_text, const QString &f_what);

// The same, refused unless f_min <= value <= f_max.
AKASHI_CORE_EXPORT Result<int> parseInt(const QString &f_text, const QString &f_what, int f_min, int f_max);

// One of the given words, compared without case. Answers with the word as
// spelled in f_words, so callers compare against their own constants.
AKASHI_CORE_EXPORT Result<QString> parseWord(const QString &f_text, const QStringList &f_words, const QString &f_what);

// A compound duration in seconds, such as "1w2d3h30m". Units are y (365
// days), w, d, h, m and s, in any order, each a non-negative count; the
// total must be above zero and at most 100 years.
AKASHI_CORE_EXPORT Result<qint64> parseDuration(const QString &f_text);

// The arguments from f_from onward joined by single spaces - the
// "rest of the line" every free-text command reads. Empty when f_from is
// past the end.
AKASHI_CORE_EXPORT QString joinFrom(const QStringList &f_arguments, int f_from);

// The text itself, refused when longer than f_max characters.
AKASHI_CORE_EXPORT Result<QString> limitLength(const QString &f_text, int f_max, const QString &f_what);

} // namespace akashi::helpers
