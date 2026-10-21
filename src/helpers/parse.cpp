#include "akashi/helpers/parse.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>

#include <limits>

namespace akashi::helpers {

Result<int> parseInt(const QString &f_text, const QString &f_subject)
{
    // A strict shape first: QString::toInt also accepts surrounding
    // whitespace, and the refusal should name the text as typed.
    static const QRegularExpression s_integer(QStringLiteral("^[+-]?\\d+$"));
    if (!s_integer.match(f_text).hasMatch()) {
        return Result<int>::failure(ParseError(ParseError::NotANumber, f_text, f_subject));
    }
    bool l_ok = false;
    const int l_value = f_text.toInt(&l_ok);
    if (!l_ok) {
        return Result<int>::failure(ParseError(ParseError::NumberTooLarge, f_text, f_subject,
                                               std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    }
    return Result<int>::success(l_value);
}

Result<int> parseInt(const QString &f_text, const QString &f_subject, int f_min, int f_max)
{
    const Result<int> l_number = parseInt(f_text, f_subject);
    if (!l_number) {
        return l_number;
    }
    if (*l_number < f_min || *l_number > f_max) {
        return Result<int>::failure(ParseError(ParseError::OutOfRange, f_text, f_subject, f_min, f_max));
    }
    return l_number;
}

Result<QString> parseWord(const QString &f_text, const QStringList &f_words, const QString &f_subject)
{
    for (const QString &l_word : f_words) {
        if (l_word.compare(f_text, Qt::CaseInsensitive) == 0) {
            return Result<QString>::success(l_word);
        }
    }
    ParseError l_error(ParseError::UnknownWord, f_text, f_subject);
    l_error.choices = f_words;
    return Result<QString>::failure(l_error);
}

Result<qint64> parseDuration(const QString &f_text, qint64 f_max_seconds)
{
    static const QRegularExpression s_shape(QStringLiteral("^(?:\\d+[ywdhms])+$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_token(QStringLiteral("(\\d+)([ywdhms])"), QRegularExpression::CaseInsensitiveOption);

    const QString l_text = f_text.trimmed();
    if (!s_shape.match(l_text).hasMatch()) {
        return Result<qint64>::failure(ParseError(ParseError::NotADuration, f_text, QStringLiteral("duration")));
    }

    qint64 l_total = 0;
    auto l_tokens = s_token.globalMatch(l_text);
    while (l_tokens.hasNext()) {
        const auto l_token = l_tokens.next();
        bool l_ok = false;
        const qint64 l_count = l_token.captured(1).toLongLong(&l_ok);
        qint64 l_unit = 1;
        switch (l_token.captured(2).at(0).toLower().unicode()) {
        case 'y':
            l_unit = 365ll * 24 * 60 * 60;
            break;
        case 'w':
            l_unit = 7ll * 24 * 60 * 60;
            break;
        case 'd':
            l_unit = 24ll * 60 * 60;
            break;
        case 'h':
            l_unit = 60ll * 60;
            break;
        case 'm':
            l_unit = 60;
            break;
        default:
            break;
        }
        // Each step is checked against the cap before it is added, so a
        // huge count is refused instead of overflowing or reading as zero.
        if (!l_ok || l_count > (f_max_seconds - l_total) / l_unit) {
            return Result<qint64>::failure(ParseError(ParseError::DurationTooLong, f_text, QStringLiteral("duration"), 1, f_max_seconds));
        }
        l_total += l_count * l_unit;
    }

    if (l_total <= 0) {
        return Result<qint64>::failure(ParseError(ParseError::DurationEmpty, f_text, QStringLiteral("duration")));
    }
    return Result<qint64>::success(l_total);
}

Result<Dice> parseDice(const QString &f_text, int f_max_count, int f_max_sides)
{
    static const QRegularExpression s_notation(QStringLiteral("^(\\d*)d(\\d+)(?:([+-])(\\d+))?$"),
                                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch l_match = s_notation.match(f_text);
    if (!l_match.hasMatch()) {
        return Result<Dice>::failure(ParseError(ParseError::NotDiceNotation, f_text, QStringLiteral("dice")));
    }

    Dice l_dice;
    if (!l_match.captured(1).isEmpty()) {
        const Result<int> l_count = parseInt(l_match.captured(1), QStringLiteral("dice_count"), 1, f_max_count);
        if (!l_count) {
            return Result<Dice>::failure(l_count.error());
        }
        l_dice.count = *l_count;
    }
    else {
        l_dice.count = 1;
    }

    const Result<int> l_sides = parseInt(l_match.captured(2), QStringLiteral("dice_sides"), 1, f_max_sides);
    if (!l_sides) {
        return Result<Dice>::failure(l_sides.error());
    }
    l_dice.sides = *l_sides;

    if (!l_match.captured(4).isEmpty()) {
        // Bounded by the sides, so a face plus the modifier always fits an int.
        const Result<int> l_modifier = parseInt(l_match.captured(4), QStringLiteral("dice_modifier"), 0, f_max_sides);
        if (!l_modifier) {
            return Result<Dice>::failure(l_modifier.error());
        }
        l_dice.modifier = l_match.captured(3) == QStringLiteral("-") ? -*l_modifier : *l_modifier;
    }
    return Result<Dice>::success(l_dice);
}

QString formatDuration(qint64 f_seconds)
{
    static const QList<QPair<qint64, QChar>> s_units{
        {365ll * 24 * 60 * 60, QLatin1Char('y')},
        {7ll * 24 * 60 * 60, QLatin1Char('w')},
        {24ll * 60 * 60, QLatin1Char('d')},
        {60ll * 60, QLatin1Char('h')},
        {60, QLatin1Char('m')},
        {1, QLatin1Char('s')},
    };
    if (f_seconds <= 0) {
        return QStringLiteral("0s");
    }
    QString l_text;
    qint64 l_left = f_seconds;
    for (const auto &[l_size, l_unit] : s_units) {
        if (l_left >= l_size) {
            l_text += QString::number(l_left / l_size) + l_unit;
            l_left %= l_size;
        }
    }
    return l_text;
}

QString joinFrom(const QStringList &f_arguments, int f_from)
{
    if (f_from < 0 || f_from >= f_arguments.size()) {
        return {};
    }
    return f_arguments.mid(f_from).join(QLatin1Char(' '));
}

Result<QString> limitLength(const QString &f_text, int f_max, const QString &f_subject)
{
    if (f_text.size() > f_max) {
        return Result<QString>::failure(ParseError(ParseError::TextTooLong, f_text, f_subject, 0, f_max));
    }
    return Result<QString>::success(f_text);
}

} // namespace akashi::helpers
