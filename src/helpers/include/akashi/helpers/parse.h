#pragma once

#include "akashi/helpers/result.h"
#include "akashi_core_export.h"

#include <QString>
#include <QStringList>

// Readers for typed input. Each one accepts exactly the documented shape
// and nothing looser; a refusal is a ParseError whose errorString() a
// player can act on. f_subject is a stable key naming what the input was
// meant to be ("area_id", "dice_count") - see ParseError::subjectLabel().
namespace akashi::helpers {

// A whole decimal number with an optional sign and no other characters.
// Out-of-range values (beyond int) are refused, never wrapped or zeroed.
AKASHI_CORE_EXPORT Result<int> parseInt(const QString &f_text, const QString &f_subject);

// The same, refused unless f_min <= value <= f_max.
AKASHI_CORE_EXPORT Result<int> parseInt(const QString &f_text, const QString &f_subject, int f_min, int f_max);

// One of the given words, compared without case. Answers with the word as
// spelled in f_words, so callers compare against their own constants.
AKASHI_CORE_EXPORT Result<QString> parseWord(const QString &f_text, const QStringList &f_words, const QString &f_subject);

// The longest duration parseDuration accepts unless told otherwise.
inline constexpr qint64 c_max_duration_seconds = 100ll * 365 * 24 * 60 * 60;

// A compound duration in seconds, such as "1w2d3h30m". Units are y (365
// days), w, d, h, m and s, in any order, each a non-negative count; the
// total must be above zero and at most f_max_seconds, so a caller's own
// limit is part of the read rather than a check after it.
AKASHI_CORE_EXPORT Result<qint64> parseDuration(const QString &f_text, qint64 f_max_seconds = c_max_duration_seconds);

// Seconds written back in the units parseDuration reads, largest first:
// 5400 is "1h30m". Zero or less is "0s".
AKASHI_CORE_EXPORT QString formatDuration(qint64 f_seconds);

// One roll in dice notation: f_count dice with f_sides faces each, and a
// modifier added to every die.
struct Dice
{
    int count = 1;
    int sides = 6;
    int modifier = 0;
};

// Dice notation such as "2d6", "1d20+3" or "3D8-1": a count from 1 to
// f_max_count, sides from 1 to f_max_sides, and an optional modifier of
// at most f_max_sides in either direction. The count may be left off
// ("d6"), meaning one die.
AKASHI_CORE_EXPORT Result<Dice> parseDice(const QString &f_text, int f_max_count, int f_max_sides);

// The arguments from f_from onward joined by single spaces - the
// "rest of the line" every free-text command reads. Empty when f_from is
// past the end.
AKASHI_CORE_EXPORT QString joinFrom(const QStringList &f_arguments, int f_from);

// The text itself, refused when longer than f_max characters.
AKASHI_CORE_EXPORT Result<QString> limitLength(const QString &f_text, int f_max, const QString &f_subject);

} // namespace akashi::helpers
