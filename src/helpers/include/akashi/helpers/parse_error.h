#pragma once

#include "akashi_core_export.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>

namespace akashi::helpers {

// Why a reader refused its input, in the shape of QJsonParseError: a code
// to branch and test on, the context a message needs, and errorString()
// as the one place codes become words. The words go through tr(), so a
// translation is a .ts file and never a code change.
struct AKASHI_CORE_EXPORT ParseError
{
    Q_GADGET
    Q_DECLARE_TR_FUNCTIONS(ParseError)

  public:
    enum Error
    {
        NoError,
        NotANumber,      // not a whole number at all
        NumberTooLarge,  // a number, but beyond what the type holds
        OutOfRange,      // a number outside min..max
        UnknownWord,     // not one of choices
        NotADuration,    // not of the 1h30m shape
        DurationEmpty,   // the right shape, but zero time
        DurationTooLong, // more than max seconds
        NotDiceNotation, // not of the 2d6+1 shape
        TextTooLong,     // more than max characters
        NoSuchClient,    // a client ID nobody holds
        IsSelf,          // the asker, where the asker is excluded
        NotInArea,       // a client standing outside the required area
    };
    Q_ENUM(Error)

    Error error = NoError;
    QString input;   // the text as typed, or the client ID it named
    QString subject; // a stable key such as "area_id"; see subjectLabel()
    qint64 min = 0;
    qint64 max = 0;
    QStringList choices; // UnknownWord: the words that would have been accepted

    // The message for the person who typed the input.
    QString errorString() const;

    // How a subject key reads in a message: the core keys have translated
    // labels, and any other key (a plugin's own) reads as itself with
    // underscores shown as spaces.
    static QString subjectLabel(const QString &f_subject);
};

} // namespace akashi::helpers
