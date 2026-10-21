#include "akashi/helpers/parse.h"

#include <QTest>

using namespace akashi::helpers;

Q_DECLARE_METATYPE(akashi::helpers::ParseError)

namespace tests {
namespace unittests {

// Every reader is one table: each row is an input and either the value it
// reads as or the ParseError code it is refused with. Only errorString()
// knows the words, so only its own table at the end checks them.
class tst_HelpersParse : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void parseInt_data();
    void parseInt();
    void parseIntRange_data();
    void parseIntRange();
    void parseWord_data();
    void parseWord();
    void parseDuration_data();
    void parseDuration();
    void parseDurationWithLimit_data();
    void parseDurationWithLimit();
    void formatDuration_data();
    void formatDuration();
    void parseDice_data();
    void parseDice();
    void joinFrom_data();
    void joinFrom();
    void limitLength_data();
    void limitLength();
    void resultValueOr();
    void errorString_data();
    void errorString();
    void subjectLabel_data();
    void subjectLabel();

  private:
    // The shared verdict for one row: an accepted row matches its value
    // and reports NoError, a refused row reports its code and no value.
    template <typename T>
    static void verify(const Result<T> &f_result, ParseError::Error f_error, const T &f_value);
};

template <typename T>
void tst_HelpersParse::verify(const Result<T> &f_result, ParseError::Error f_error, const T &f_value)
{
    QCOMPARE(f_result.error().error, f_error);
    QCOMPARE(f_result.ok(), f_error == ParseError::NoError);
    if (f_result.ok()) {
        QCOMPARE(*f_result, f_value);
    }
}

void tst_HelpersParse::parseInt_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<int>("value");

    QTest::newRow("zero") << "0" << ParseError::NoError << 0;
    QTest::newRow("positive") << "42" << ParseError::NoError << 42;
    QTest::newRow("explicit plus") << "+7" << ParseError::NoError << 7;
    QTest::newRow("negative") << "-3" << ParseError::NoError << -3;
    QTest::newRow("leading zeros") << "007" << ParseError::NoError << 7;
    QTest::newRow("int max") << "2147483647" << ParseError::NoError << 2147483647;
    QTest::newRow("int min") << "-2147483648" << ParseError::NoError << (-2147483647 - 1);

    QTest::newRow("empty") << "" << ParseError::NotANumber << 0;
    QTest::newRow("word") << "abc" << ParseError::NotANumber << 0;
    QTest::newRow("trailing junk") << "12abc" << ParseError::NotANumber << 0;
    QTest::newRow("leading space") << " 12" << ParseError::NotANumber << 0;
    QTest::newRow("trailing space") << "12 " << ParseError::NotANumber << 0;
    QTest::newRow("decimal") << "1.5" << ParseError::NotANumber << 0;
    QTest::newRow("hex") << "0x10" << ParseError::NotANumber << 0;
    QTest::newRow("sign only") << "-" << ParseError::NotANumber << 0;
    QTest::newRow("double sign") << "--1" << ParseError::NotANumber << 0;
    QTest::newRow("full-width digits") << QString::fromUtf8("１２") << ParseError::NotANumber << 0;
    QTest::newRow("above int max") << "2147483648" << ParseError::NumberTooLarge << 0;
    QTest::newRow("far above int max") << "99999999999999999999999" << ParseError::NumberTooLarge << 0;
    QTest::newRow("below int min") << "-2147483649" << ParseError::NumberTooLarge << 0;
}

void tst_HelpersParse::parseInt()
{
    QFETCH(QString, text);
    QFETCH(ParseError::Error, error);
    QFETCH(int, value);

    const Result<int> l_result = akashi::helpers::parseInt(text, QStringLiteral("client_id"));
    verify(l_result, error, value);
    if (!l_result) {
        // A refusal says what was typed and what it was meant to be.
        QCOMPARE(l_result.error().input, text);
        QCOMPARE(l_result.error().subject, QStringLiteral("client_id"));
    }
}

void tst_HelpersParse::parseIntRange_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<int>("value");

    QTest::newRow("lower bound") << "1" << ParseError::NoError << 1;
    QTest::newRow("inside") << "5" << ParseError::NoError << 5;
    QTest::newRow("upper bound") << "10" << ParseError::NoError << 10;
    QTest::newRow("below") << "0" << ParseError::OutOfRange << 0;
    QTest::newRow("above") << "11" << ParseError::OutOfRange << 0;
    QTest::newRow("not a number") << "ten" << ParseError::NotANumber << 0;
}

void tst_HelpersParse::parseIntRange()
{
    QFETCH(QString, text);
    QFETCH(ParseError::Error, error);
    QFETCH(int, value);

    const Result<int> l_result = akashi::helpers::parseInt(text, QStringLiteral("dice_count"), 1, 10);
    verify(l_result, error, value);
    if (error == ParseError::OutOfRange) {
        // The bounds travel with the refusal, for the message to state.
        QCOMPARE(l_result.error().min, 1);
        QCOMPARE(l_result.error().max, 10);
    }
}

void tst_HelpersParse::parseWord_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("words");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<QString>("value");

    const QStringList l_switch{QStringLiteral("on"), QStringLiteral("off")};
    QTest::newRow("exact") << "on" << l_switch << ParseError::NoError << "on";
    QTest::newRow("upper case gives the list spelling") << "ON" << l_switch << ParseError::NoError << "on";
    QTest::newRow("mixed case") << "Off" << l_switch << ParseError::NoError << "off";
    QTest::newRow("unknown word") << "maybe" << l_switch << ParseError::UnknownWord << "";
    QTest::newRow("empty") << "" << l_switch << ParseError::UnknownWord << "";
    QTest::newRow("prefix only") << "of" << l_switch << ParseError::UnknownWord << "";
    QTest::newRow("no words at all") << "on" << QStringList() << ParseError::UnknownWord << "";
}

void tst_HelpersParse::parseWord()
{
    QFETCH(QString, text);
    QFETCH(QStringList, words);
    QFETCH(ParseError::Error, error);
    QFETCH(QString, value);

    const Result<QString> l_result = akashi::helpers::parseWord(text, words, QStringLiteral("switch"));
    verify(l_result, error, value);
    if (!l_result) {
        // The accepted words travel with the refusal, for the message to list.
        QCOMPARE(l_result.error().choices, words);
    }
}

void tst_HelpersParse::parseDuration_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<qint64>("value");

    QTest::newRow("seconds") << "30s" << ParseError::NoError << qint64(30);
    QTest::newRow("minutes") << "5m" << ParseError::NoError << qint64(300);
    QTest::newRow("hours and minutes") << "1h30m" << ParseError::NoError << qint64(5400);
    QTest::newRow("any order") << "30m1h" << ParseError::NoError << qint64(5400);
    QTest::newRow("every unit") << "1y1w1d1h1m1s" << ParseError::NoError << qint64(31536000 + 604800 + 86400 + 3600 + 60 + 1);
    QTest::newRow("repeated unit adds") << "1h1h" << ParseError::NoError << qint64(7200);
    QTest::newRow("zero part is fine") << "0h30m" << ParseError::NoError << qint64(1800);
    QTest::newRow("upper case") << "2D" << ParseError::NoError << qint64(172800);
    QTest::newRow("surrounding space") << " 10m " << ParseError::NoError << qint64(600);
    QTest::newRow("exactly 100 years") << "100y" << ParseError::NoError << qint64(100ll * 365 * 24 * 60 * 60);

    QTest::newRow("empty") << "" << ParseError::NotADuration << qint64(0);
    QTest::newRow("bare number") << "10" << ParseError::NotADuration << qint64(0);
    QTest::newRow("unit only") << "h" << ParseError::NotADuration << qint64(0);
    QTest::newRow("unknown unit") << "10x" << ParseError::NotADuration << qint64(0);
    QTest::newRow("negative") << "-5m" << ParseError::NotADuration << qint64(0);
    QTest::newRow("decimal") << "1.5h" << ParseError::NotADuration << qint64(0);
    QTest::newRow("inner space") << "1h 30m" << ParseError::NotADuration << qint64(0);
    QTest::newRow("word") << "perma" << ParseError::NotADuration << qint64(0);
    QTest::newRow("all zero") << "0s" << ParseError::DurationEmpty << qint64(0);
    QTest::newRow("over 100 years") << "101y" << ParseError::DurationTooLong << qint64(0);
    QTest::newRow("over 100 years in pieces") << "100y1s" << ParseError::DurationTooLong << qint64(0);
    QTest::newRow("count beyond 64 bits") << "99999999999999999999999s" << ParseError::DurationTooLong << qint64(0);
    QTest::newRow("count that would overflow") << "9223372036854775807y" << ParseError::DurationTooLong << qint64(0);
}

void tst_HelpersParse::parseDuration()
{
    QFETCH(QString, text);
    QFETCH(ParseError::Error, error);
    QFETCH(qint64, value);

    verify(akashi::helpers::parseDuration(text), error, value);
}

void tst_HelpersParse::parseDurationWithLimit_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("max");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<qint64>("value");

    // A caller's own limit, as a plugin passes its configured maximum.
    QTest::newRow("under the limit") << "45m" << qint64(3600) << ParseError::NoError << qint64(2700);
    QTest::newRow("exactly the limit") << "1h" << qint64(3600) << ParseError::NoError << qint64(3600);
    QTest::newRow("the limit in other units") << "60m" << qint64(3600) << ParseError::NoError << qint64(3600);
    QTest::newRow("one second over") << "1h1s" << qint64(3600) << ParseError::DurationTooLong << qint64(0);
    QTest::newRow("far over") << "5h" << qint64(3600) << ParseError::DurationTooLong << qint64(0);
    QTest::newRow("shape still checked first") << "5 hours" << qint64(3600) << ParseError::NotADuration << qint64(0);
}

void tst_HelpersParse::parseDurationWithLimit()
{
    QFETCH(QString, text);
    QFETCH(qint64, max);
    QFETCH(ParseError::Error, error);
    QFETCH(qint64, value);

    const Result<qint64> l_result = akashi::helpers::parseDuration(text, max);
    verify(l_result, error, value);
    if (error == ParseError::DurationTooLong) {
        // The limit travels with the refusal, for the message to state.
        QCOMPARE(l_result.error().max, max);
    }
}

void tst_HelpersParse::formatDuration_data()
{
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("text");

    QTest::newRow("seconds") << qint64(30) << "30s";
    QTest::newRow("whole hour") << qint64(3600) << "1h";
    QTest::newRow("hour and a half") << qint64(5400) << "1h30m";
    QTest::newRow("every unit") << qint64(31536000 + 604800 + 86400 + 3600 + 60 + 1) << "1y1w1d1h1m1s";
    QTest::newRow("the default limit") << akashi::helpers::c_max_duration_seconds << "100y";
    QTest::newRow("zero") << qint64(0) << "0s";
    QTest::newRow("negative") << qint64(-5) << "0s";
}

void tst_HelpersParse::formatDuration()
{
    QFETCH(qint64, seconds);
    QFETCH(QString, text);

    QCOMPARE(akashi::helpers::formatDuration(seconds), text);
    // What it writes, parseDuration reads back to the same seconds.
    if (seconds > 0) {
        QCOMPARE(*akashi::helpers::parseDuration(text), seconds);
    }
}

void tst_HelpersParse::parseDice_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<QString>("subject");
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("sides");
    QTest::addColumn<int>("modifier");

    // Limits for every row: at most 10 dice of at most 100 sides.
    QTest::newRow("plain") << "2d6" << ParseError::NoError << "" << 2 << 6 << 0;
    QTest::newRow("count left off") << "d20" << ParseError::NoError << "" << 1 << 20 << 0;
    QTest::newRow("plus modifier") << "1d20+3" << ParseError::NoError << "" << 1 << 20 << 3;
    QTest::newRow("minus modifier") << "3d8-1" << ParseError::NoError << "" << 3 << 8 << -1;
    QTest::newRow("upper case D") << "2D6" << ParseError::NoError << "" << 2 << 6 << 0;
    QTest::newRow("at every limit") << "10d100+100" << ParseError::NoError << "" << 10 << 100 << 100;
    QTest::newRow("lowest modifier") << "1d6-100" << ParseError::NoError << "" << 1 << 6 << -100;

    QTest::newRow("bare number") << "6" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("no sides") << "2d" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("word") << "dice" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("dangling modifier") << "2d6+" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("two modifiers") << "2d6+1+1" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("negative count") << "-2d6" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    QTest::newRow("spaces") << "2d6 + 1" << ParseError::NotDiceNotation << "dice" << 0 << 0 << 0;
    // A part out of its range names that part, so the message can too.
    QTest::newRow("zero dice") << "0d6" << ParseError::OutOfRange << "dice_count" << 0 << 0 << 0;
    QTest::newRow("too many dice") << "11d6" << ParseError::OutOfRange << "dice_count" << 0 << 0 << 0;
    QTest::newRow("zero sides") << "1d0" << ParseError::OutOfRange << "dice_sides" << 0 << 0 << 0;
    QTest::newRow("too many sides") << "1d101" << ParseError::OutOfRange << "dice_sides" << 0 << 0 << 0;
    QTest::newRow("modifier too large") << "1d6+101" << ParseError::OutOfRange << "dice_modifier" << 0 << 0 << 0;
    QTest::newRow("count beyond int") << "99999999999d6" << ParseError::NumberTooLarge << "dice_count" << 0 << 0 << 0;
}

void tst_HelpersParse::parseDice()
{
    QFETCH(QString, text);
    QFETCH(ParseError::Error, error);
    QFETCH(QString, subject);
    QFETCH(int, count);
    QFETCH(int, sides);
    QFETCH(int, modifier);

    const Result<Dice> l_result = akashi::helpers::parseDice(text, 10, 100);
    QCOMPARE(l_result.error().error, error);
    QCOMPARE(l_result.ok(), error == ParseError::NoError);
    if (l_result) {
        QCOMPARE(l_result->count, count);
        QCOMPARE(l_result->sides, sides);
        QCOMPARE(l_result->modifier, modifier);
        return;
    }
    QCOMPARE(l_result.error().subject, subject);
}

void tst_HelpersParse::joinFrom_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<int>("from");
    QTest::addColumn<QString>("expected");

    const QStringList l_args{QStringLiteral("5"), QStringLiteral("hello"), QStringLiteral("there")};
    QTest::newRow("from the start") << l_args << 0 << "5 hello there";
    QTest::newRow("rest of the line") << l_args << 1 << "hello there";
    QTest::newRow("last word") << l_args << 2 << "there";
    QTest::newRow("past the end") << l_args << 3 << "";
    QTest::newRow("negative index") << l_args << -1 << "";
    QTest::newRow("no arguments") << QStringList() << 0 << "";
}

void tst_HelpersParse::joinFrom()
{
    QFETCH(QStringList, arguments);
    QFETCH(int, from);
    QFETCH(QString, expected);

    QCOMPARE(akashi::helpers::joinFrom(arguments, from), expected);
}

void tst_HelpersParse::limitLength_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("max");
    QTest::addColumn<ParseError::Error>("error");

    QTest::newRow("shorter") << "ab" << 3 << ParseError::NoError;
    QTest::newRow("exactly the limit") << "abc" << 3 << ParseError::NoError;
    QTest::newRow("empty with no room") << "" << 0 << ParseError::NoError;
    QTest::newRow("one over") << "abcd" << 3 << ParseError::TextTooLong;
    QTest::newRow("anything with no room") << "a" << 0 << ParseError::TextTooLong;
}

void tst_HelpersParse::limitLength()
{
    QFETCH(QString, text);
    QFETCH(int, max);
    QFETCH(ParseError::Error, error);

    const Result<QString> l_result = akashi::helpers::limitLength(text, max, QStringLiteral("reason"));
    verify(l_result, error, text);
    if (!l_result) {
        QCOMPARE(l_result.error().max, max);
    }
}

void tst_HelpersParse::resultValueOr()
{
    QCOMPARE(akashi::helpers::parseInt(QStringLiteral("8"), QStringLiteral("number")).valueOr(-1), 8);
    QCOMPARE(akashi::helpers::parseInt(QStringLiteral("x"), QStringLiteral("number")).valueOr(-1), -1);
}

// The one table that knows the English words: one row per code. A wording
// change or a translation touches this table and nothing else.
void tst_HelpersParse::errorString_data()
{
    QTest::addColumn<ParseError>("error");
    QTest::addColumn<QString>("expected");

    const auto l_make = [](ParseError::Error f_code, const QString &f_input, const QString &f_subject,
                           qint64 f_min = 0, qint64 f_max = 0, const QStringList &f_choices = {}) {
        ParseError l_error;
        l_error.error = f_code;
        l_error.input = f_input;
        l_error.subject = f_subject;
        l_error.min = f_min;
        l_error.max = f_max;
        l_error.choices = f_choices;
        return l_error;
    };

    QTest::newRow("NoError") << l_make(ParseError::NoError, "", "") << "";
    QTest::newRow("NotANumber") << l_make(ParseError::NotANumber, "abc", "client_id") << "\"abc\" is not a valid client ID.";
    QTest::newRow("NumberTooLarge") << l_make(ParseError::NumberTooLarge, "99999999999", "client_id") << "99999999999 is too large for a client ID.";
    QTest::newRow("OutOfRange") << l_make(ParseError::OutOfRange, "11", "dice_count", 1, 10) << "The number of dice must be between 1 and 10.";
    QTest::newRow("UnknownWord") << l_make(ParseError::UnknownWord, "maybe", "switch", 0, 0, {"on", "off"}) << "\"maybe\" is not a valid switch. Use one of: on, off.";
    QTest::newRow("NotADuration") << l_make(ParseError::NotADuration, "10", "duration") << "\"10\" is not a valid duration. Use a duration like 1h30m or 2d.";
    QTest::newRow("DurationEmpty") << l_make(ParseError::DurationEmpty, "0s", "duration") << "\"0s\" adds up to no time at all. Use a duration like 1h30m or 2d.";
    QTest::newRow("DurationTooLong") << l_make(ParseError::DurationTooLong, "5h", "duration", 1, 3600) << "\"5h\" is longer than 1h.";
    QTest::newRow("NotDiceNotation") << l_make(ParseError::NotDiceNotation, "6", "dice") << "\"6\" is not valid dice notation. Use a form like 2d6 or 1d20+3.";
    QTest::newRow("TextTooLong") << l_make(ParseError::TextTooLong, "abcd", "reason", 0, 3) << "The reason is too long: 4 characters, at most 3.";
    QTest::newRow("NoSuchClient") << l_make(ParseError::NoSuchClient, "7", "client_id") << "No client with ID 7 is connected.";
    QTest::newRow("IsSelf") << l_make(ParseError::IsSelf, "0", "client_id") << "You cannot target yourself with this command.";
    QTest::newRow("NotInArea") << l_make(ParseError::NotInArea, "1", "client_id") << "Client 1 is not in this area.";
}

void tst_HelpersParse::errorString()
{
    QFETCH(ParseError, error);
    QFETCH(QString, expected);

    QCOMPARE(error.errorString(), expected);
}

void tst_HelpersParse::subjectLabel_data()
{
    QTest::addColumn<QString>("subject");
    QTest::addColumn<QString>("label");

    // Every subject core passes has a label of its own.
    QTest::newRow("number") << "number" << "number";
    QTest::newRow("client_id") << "client_id" << "client ID";
    QTest::newRow("area_id") << "area_id" << "area ID";
    QTest::newRow("floor_id") << "floor_id" << "floor ID";
    QTest::newRow("ban_id") << "ban_id" << "ban ID";
    QTest::newRow("timer_id") << "timer_id" << "timer ID";
    QTest::newRow("evidence_id") << "evidence_id" << "evidence ID";
    QTest::newRow("dice_count") << "dice_count" << "number of dice";
    QTest::newRow("dice_sides") << "dice_sides" << "number of sides";
    QTest::newRow("dice_modifier") << "dice_modifier" << "modifier";
    QTest::newRow("song_duration") << "song_duration" << "song duration";
    // Any other key - a plugin's own - reads as itself.
    QTest::newRow("plugin key") << "slowmode_cap" << "slowmode cap";
    QTest::newRow("plain word") << "reason" << "reason";
}

void tst_HelpersParse::subjectLabel()
{
    QFETCH(QString, subject);
    QFETCH(QString, label);

    QCOMPARE(ParseError::subjectLabel(subject), label);
}

} // namespace unittests
} // namespace tests

QTEST_GUILESS_MAIN(tests::unittests::tst_HelpersParse)

#include "tst_helpers_parse.moc"
