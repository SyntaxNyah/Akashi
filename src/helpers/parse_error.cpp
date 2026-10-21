#include "akashi/helpers/parse_error.h"

#include "akashi/helpers/parse.h"

namespace akashi::helpers {

QString ParseError::errorString() const
{
    const QString l_subject = subjectLabel(subject);
    switch (error) {
    case NoError:
        return {};
    case NotANumber:
        return tr("\"%1\" is not a valid %2.").arg(input, l_subject);
    case NumberTooLarge:
        return tr("%1 is too large for a %2.").arg(input, l_subject);
    case OutOfRange:
        return tr("The %1 must be between %2 and %3.").arg(l_subject).arg(min).arg(max);
    case UnknownWord:
        return tr("\"%1\" is not a valid %2. Use one of: %3.").arg(input, l_subject, choices.join(QStringLiteral(", ")));
    case NotADuration:
        return tr("\"%1\" is not a valid duration. Use a duration like 1h30m or 2d.").arg(input);
    case DurationEmpty:
        return tr("\"%1\" adds up to no time at all. Use a duration like 1h30m or 2d.").arg(input);
    case DurationTooLong:
        return tr("\"%1\" is longer than %2.").arg(input, formatDuration(max));
    case NotDiceNotation:
        return tr("\"%1\" is not valid dice notation. Use a form like 2d6 or 1d20+3.").arg(input);
    case TextTooLong:
        return tr("The %1 is too long: %2 characters, at most %3.").arg(l_subject).arg(input.size()).arg(max);
    case NoSuchClient:
        return tr("No client with ID %1 is connected.").arg(input);
    case IsSelf:
        return tr("You cannot target yourself with this command.");
    case NotInArea:
        return tr("Client %1 is not in this area.").arg(input);
    case MissingValue:
        return tr("The %1 is missing.").arg(l_subject);
    case ExtraValue:
        return tr("\"%1\" was not expected here.").arg(input);
    case Immune:
        return tr("Client %1 cannot be targeted by this command.").arg(input);
    }
    return {};
}

QString ParseError::subjectLabel(const QString &f_subject)
{
    if (f_subject == QLatin1String("number"))
        return tr("number");
    if (f_subject == QLatin1String("client_id"))
        return tr("client ID");
    if (f_subject == QLatin1String("area_id"))
        return tr("area ID");
    if (f_subject == QLatin1String("floor_id"))
        return tr("floor ID");
    if (f_subject == QLatin1String("ban_id"))
        return tr("ban ID");
    if (f_subject == QLatin1String("timer_id"))
        return tr("timer ID");
    if (f_subject == QLatin1String("evidence_id"))
        return tr("evidence ID");
    if (f_subject == QLatin1String("dice_count"))
        return tr("number of dice");
    if (f_subject == QLatin1String("dice_sides"))
        return tr("number of sides");
    if (f_subject == QLatin1String("dice_modifier"))
        return tr("modifier");
    if (f_subject == QLatin1String("song_duration"))
        return tr("song duration");
    return QString(f_subject).replace(QLatin1Char('_'), QLatin1Char(' '));
}

} // namespace akashi::helpers
