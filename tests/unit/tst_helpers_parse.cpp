#include "akashi/helpers/parse.h"

#include <QTest>

using namespace akashi::helpers;

namespace tests {
namespace unittests {

// Every reader is one table: each row is an input, whether it is
// accepted, and the value or the reason expected. An empty reason column
// only requires that some reason is given.
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
    void joinFrom_data();
    void joinFrom();
    void limitLength_data();
    void limitLength();
    void resultValueOr();

  private:
    // The shared verdict for one row: accepted rows match their value and
    // carry no reason, refused rows carry the expected reason.
    template <typename T>
    static void verify(const Result<T> &f_result, bool f_accepted, const T &f_value, const QString &f_reason);
};

template <typename T>
void tst_HelpersParse::verify(const Result<T> &f_result, bool f_accepted, const T &f_value, const QString &f_reason)
{
    QCOMPARE(f_result.ok(), f_accepted);
    if (f_accepted) {
        QCOMPARE(*f_result, f_value);
        QCOMPARE(f_result.error(), QString());
        return;
    }
    QVERIFY(!f_result.error().isEmpty());
    if (!f_reason.isEmpty()) {
        QCOMPARE(f_result.error(), f_reason);
    }
}

void tst_HelpersParse::parseInt_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("reason");

    QTest::newRow("zero") << "0" << true << 0 << "";
    QTest::newRow("positive") << "42" << true << 42 << "";
    QTest::newRow("explicit plus") << "+7" << true << 7 << "";
    QTest::newRow("negative") << "-3" << true << -3 << "";
    QTest::newRow("leading zeros") << "007" << true << 7 << "";
    QTest::newRow("int max") << "2147483647" << true << 2147483647 << "";
    QTest::newRow("int min") << "-2147483648" << true << (-2147483647 - 1) << "";

    QTest::newRow("empty") << "" << false << 0 << "\"\" is not a valid client ID.";
    QTest::newRow("word") << "abc" << false << 0 << "\"abc\" is not a valid client ID.";
    QTest::newRow("trailing junk") << "12abc" << false << 0 << "";
    QTest::newRow("leading space") << " 12" << false << 0 << "";
    QTest::newRow("trailing space") << "12 " << false << 0 << "";
    QTest::newRow("decimal") << "1.5" << false << 0 << "";
    QTest::newRow("hex") << "0x10" << false << 0 << "";
    QTest::newRow("sign only") << "-" << false << 0 << "";
    QTest::newRow("double sign") << "--1" << false << 0 << "";
    QTest::newRow("full-width digits") << QString::fromUtf8("１２") << false << 0 << "";
    QTest::newRow("above int max") << "2147483648" << false << 0 << "2147483648 is too large for a client ID.";
    QTest::newRow("far above int max") << "99999999999999999999999" << false << 0 << "";
    QTest::newRow("below int min") << "-2147483649" << false << 0 << "";
}

void tst_HelpersParse::parseInt()
{
    QFETCH(QString, text);
    QFETCH(bool, accepted);
    QFETCH(int, value);
    QFETCH(QString, reason);

    verify(akashi::helpers::parseInt(text, QStringLiteral("client ID")), accepted, value, reason);
}

void tst_HelpersParse::parseIntRange_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("reason");

    QTest::newRow("lower bound") << "1" << true << 1 << "";
    QTest::newRow("inside") << "5" << true << 5 << "";
    QTest::newRow("upper bound") << "10" << true << 10 << "";
    QTest::newRow("below") << "0" << false << 0 << "The dice count must be between 1 and 10.";
    QTest::newRow("above") << "11" << false << 0 << "The dice count must be between 1 and 10.";
    QTest::newRow("not a number") << "ten" << false << 0 << "\"ten\" is not a valid dice count.";
}

void tst_HelpersParse::parseIntRange()
{
    QFETCH(QString, text);
    QFETCH(bool, accepted);
    QFETCH(int, value);
    QFETCH(QString, reason);

    verify(akashi::helpers::parseInt(text, QStringLiteral("dice count"), 1, 10), accepted, value, reason);
}

void tst_HelpersParse::parseWord_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("words");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("reason");

    const QStringList l_switch{QStringLiteral("on"), QStringLiteral("off")};
    QTest::newRow("exact") << "on" << l_switch << true << "on" << "";
    QTest::newRow("upper case gives the list spelling") << "ON" << l_switch << true << "on" << "";
    QTest::newRow("mixed case") << "Off" << l_switch << true << "off" << "";
    QTest::newRow("unknown word lists the choices") << "maybe" << l_switch << false << "" << "\"maybe\" is not a valid switch. Use one of: on, off.";
    QTest::newRow("empty") << "" << l_switch << false << "" << "";
    QTest::newRow("prefix only") << "of" << l_switch << false << "" << "";
    QTest::newRow("no words at all") << "on" << QStringList() << false << "" << "";
}

void tst_HelpersParse::parseWord()
{
    QFETCH(QString, text);
    QFETCH(QStringList, words);
    QFETCH(bool, accepted);
    QFETCH(QString, value);
    QFETCH(QString, reason);

    verify(akashi::helpers::parseWord(text, words, QStringLiteral("switch")), accepted, value, reason);
}

void tst_HelpersParse::parseDuration_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<qint64>("value");
    QTest::addColumn<QString>("reason");

    QTest::newRow("seconds") << "30s" << true << qint64(30) << "";
    QTest::newRow("minutes") << "5m" << true << qint64(300) << "";
    QTest::newRow("hours and minutes") << "1h30m" << true << qint64(5400) << "";
    QTest::newRow("any order") << "30m1h" << true << qint64(5400) << "";
    QTest::newRow("every unit") << "1y1w1d1h1m1s" << true << qint64(31536000 + 604800 + 86400 + 3600 + 60 + 1) << "";
    QTest::newRow("repeated unit adds") << "1h1h" << true << qint64(7200) << "";
    QTest::newRow("zero part is fine") << "0h30m" << true << qint64(1800) << "";
    QTest::newRow("upper case") << "2D" << true << qint64(172800) << "";
    QTest::newRow("surrounding space") << " 10m " << true << qint64(600) << "";
    QTest::newRow("exactly 100 years") << "100y" << true << qint64(100ll * 365 * 24 * 60 * 60) << "";

    QTest::newRow("empty") << "" << false << qint64(0) << "";
    QTest::newRow("bare number") << "10" << false << qint64(0) << "\"10\" is not a valid duration. Use a duration like 1h30m or 2d.";
    QTest::newRow("unit only") << "h" << false << qint64(0) << "";
    QTest::newRow("unknown unit") << "10x" << false << qint64(0) << "";
    QTest::newRow("negative") << "-5m" << false << qint64(0) << "";
    QTest::newRow("decimal") << "1.5h" << false << qint64(0) << "";
    QTest::newRow("inner space") << "1h 30m" << false << qint64(0) << "";
    QTest::newRow("word") << "perma" << false << qint64(0) << "";
    QTest::newRow("all zero") << "0s" << false << qint64(0) << "\"0s\" adds up to no time at all. Use a duration like 1h30m or 2d.";
    QTest::newRow("over 100 years") << "101y" << false << qint64(0) << "\"101y\" is longer than 100 years.";
    QTest::newRow("over 100 years in pieces") << "100y1s" << false << qint64(0) << "";
    QTest::newRow("count beyond 64 bits") << "99999999999999999999999s" << false << qint64(0) << "";
    QTest::newRow("count that would overflow") << "9223372036854775807y" << false << qint64(0) << "";
}

void tst_HelpersParse::parseDuration()
{
    QFETCH(QString, text);
    QFETCH(bool, accepted);
    QFETCH(qint64, value);
    QFETCH(QString, reason);

    verify(akashi::helpers::parseDuration(text), accepted, value, reason);
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
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<QString>("reason");

    QTest::newRow("shorter") << "ab" << 3 << true << "";
    QTest::newRow("exactly the limit") << "abc" << 3 << true << "";
    QTest::newRow("empty with no room") << "" << 0 << true << "";
    QTest::newRow("one over") << "abcd" << 3 << false << "The reason is too long: 4 characters, at most 3.";
    QTest::newRow("anything with no room") << "a" << 0 << false << "The reason is too long: 1 characters, at most 0.";
}

void tst_HelpersParse::limitLength()
{
    QFETCH(QString, text);
    QFETCH(int, max);
    QFETCH(bool, accepted);
    QFETCH(QString, reason);

    verify(akashi::helpers::limitLength(text, max, QStringLiteral("reason")), accepted, text, reason);
}

void tst_HelpersParse::resultValueOr()
{
    QCOMPARE(akashi::helpers::parseInt(QStringLiteral("8"), QStringLiteral("n")).valueOr(-1), 8);
    QCOMPARE(akashi::helpers::parseInt(QStringLiteral("x"), QStringLiteral("n")).valueOr(-1), -1);
}

} // namespace unittests
} // namespace tests

QTEST_GUILESS_MAIN(tests::unittests::tst_HelpersParse)

#include "tst_helpers_parse.moc"
