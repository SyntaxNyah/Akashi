#include "akashi/helpers/parse.h"

#include <QRegularExpression>

#include <limits>

namespace akashi::helpers {

Result<int> parseInt(const QString &f_text, const QString &f_what)
{
    // A strict shape first: QString::toInt also accepts surrounding
    // whitespace, and the reason for a refusal should name the text as typed.
    static const QRegularExpression s_integer(QStringLiteral("^[+-]?\\d+$"));
    if (!s_integer.match(f_text).hasMatch()) {
        return Result<int>::failure(QStringLiteral("\"%1\" is not a valid %2.").arg(f_text, f_what));
    }
    bool l_ok = false;
    const int l_value = f_text.toInt(&l_ok);
    if (!l_ok) {
        return Result<int>::failure(QStringLiteral("%1 is too large for a %2.").arg(f_text, f_what));
    }
    return Result<int>::success(l_value);
}

Result<int> parseInt(const QString &f_text, const QString &f_what, int f_min, int f_max)
{
    const Result<int> l_number = parseInt(f_text, f_what);
    if (!l_number) {
        return l_number;
    }
    if (*l_number < f_min || *l_number > f_max) {
        return Result<int>::failure(QStringLiteral("The %1 must be between %2 and %3.")
                                        .arg(f_what, QString::number(f_min), QString::number(f_max)));
    }
    return l_number;
}

Result<QString> parseWord(const QString &f_text, const QStringList &f_words, const QString &f_what)
{
    for (const QString &l_word : f_words) {
        if (l_word.compare(f_text, Qt::CaseInsensitive) == 0) {
            return Result<QString>::success(l_word);
        }
    }
    return Result<QString>::failure(QStringLiteral("\"%1\" is not a valid %2. Use one of: %3.")
                                        .arg(f_text, f_what, f_words.join(QStringLiteral(", "))));
}

Result<qint64> parseDuration(const QString &f_text)
{
    static const QString s_example = QStringLiteral("Use a duration like 1h30m or 2d.");
    static const QRegularExpression s_shape(QStringLiteral("^(?:\\d+[ywdhms])+$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_token(QStringLiteral("(\\d+)([ywdhms])"), QRegularExpression::CaseInsensitiveOption);
    static const qint64 s_max_seconds = 100ll * 365 * 24 * 60 * 60;

    const QString l_text = f_text.trimmed();
    if (!s_shape.match(l_text).hasMatch()) {
        return Result<qint64>::failure(QStringLiteral("\"%1\" is not a valid duration. %2").arg(f_text, s_example));
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
        if (!l_ok || l_count > (s_max_seconds - l_total) / l_unit) {
            return Result<qint64>::failure(QStringLiteral("\"%1\" is longer than 100 years.").arg(f_text));
        }
        l_total += l_count * l_unit;
    }

    if (l_total <= 0) {
        return Result<qint64>::failure(QStringLiteral("\"%1\" adds up to no time at all. %2").arg(f_text, s_example));
    }
    return Result<qint64>::success(l_total);
}

QString joinFrom(const QStringList &f_arguments, int f_from)
{
    if (f_from < 0 || f_from >= f_arguments.size()) {
        return {};
    }
    return f_arguments.mid(f_from).join(QLatin1Char(' '));
}

Result<QString> limitLength(const QString &f_text, int f_max, const QString &f_what)
{
    if (f_text.size() > f_max) {
        return Result<QString>::failure(QStringLiteral("The %1 is too long: %2 characters, at most %3.")
                                            .arg(f_what, QString::number(f_text.size()), QString::number(f_max)));
    }
    return Result<QString>::success(f_text);
}

} // namespace akashi::helpers
