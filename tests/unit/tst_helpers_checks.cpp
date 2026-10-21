#include "akashi/config_entry.h"
#include "akashi/helpers/checks.h"

#include <QTest>
#include <QTime>

using namespace akashi::helpers;

// Named holders for check columns: Qt cannot register a std::function by
// its spelled-out template name, but it can register a plain struct.
struct CheckColumn
{
    Check check;
};
struct CheckListColumn
{
    QList<Check> checks;
};
Q_DECLARE_METATYPE(CheckColumn)
Q_DECLARE_METATYPE(CheckListColumn)

namespace tests {
namespace unittests {

// Each check is one table of values and whether the check passes them.
class tst_HelpersChecks : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void inRange_data();
    void inRange();
    void atLeast_data();
    void atLeast();
    void atMost_data();
    void atMost();
    void allOf_data();
    void allOf();
    void oneOf_data();
    void oneOf();
    void emptyOrTime_data();
    void emptyOrTime();
    void url_data();
    void url();
    void emptyOr_data();
    void emptyOr();
    void configEntry_data();
    void configEntry();

  private:
    static void addColumns();
};

void tst_HelpersChecks::addColumns()
{
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<bool>("passes");
}

// Before the helpers, every non-number read as 0 and passed any range
// holding 0 - a webhook color of "abc" was accepted. Each number check
// gets these rows.
static void addNonNumberRows()
{
    QTest::newRow("word") << QVariant(QStringLiteral("abc")) << false;
    QTest::newRow("empty text") << QVariant(QString()) << false;
    QTest::newRow("null") << QVariant() << false;
    QTest::newRow("trailing junk") << QVariant(QStringLiteral("12px")) << false;
}

void tst_HelpersChecks::inRange_data()
{
    addColumns();
    QTest::newRow("lower bound") << QVariant(0) << true;
    QTest::newRow("upper bound") << QVariant(65535) << true;
    QTest::newRow("inside as text") << QVariant(QStringLiteral("8080")) << true;
    QTest::newRow("fraction inside") << QVariant(1.5) << true;
    QTest::newRow("below") << QVariant(-1) << false;
    QTest::newRow("above") << QVariant(65536) << false;
    addNonNumberRows();
}

void tst_HelpersChecks::inRange()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::inRange(0, 65535)(value), passes);
}

void tst_HelpersChecks::atLeast_data()
{
    addColumns();
    QTest::newRow("at the bound") << QVariant(0) << true;
    QTest::newRow("above") << QVariant(100) << true;
    QTest::newRow("below") << QVariant(-1) << false;
    addNonNumberRows();
}

void tst_HelpersChecks::atLeast()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::atLeast(0)(value), passes);
}

void tst_HelpersChecks::atMost_data()
{
    addColumns();
    QTest::newRow("at the bound") << QVariant(10) << true;
    QTest::newRow("below") << QVariant(-5) << true;
    QTest::newRow("above") << QVariant(11) << false;
    addNonNumberRows();
}

void tst_HelpersChecks::atMost()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::atMost(10)(value), passes);
}

void tst_HelpersChecks::allOf_data()
{
    QTest::addColumn<CheckListColumn>("checks");
    addColumns();

    const CheckListColumn l_one_to_five{{akashi::helpers::atLeast(1), akashi::helpers::atMost(5)}};
    QTest::newRow("passes both") << l_one_to_five << QVariant(3) << true;
    QTest::newRow("fails the first") << l_one_to_five << QVariant(0) << false;
    QTest::newRow("fails the second") << l_one_to_five << QVariant(6) << false;
    QTest::newRow("no checks impose nothing") << CheckListColumn{} << QVariant(QStringLiteral("x")) << true;
    QTest::newRow("an empty check imposes nothing") << CheckListColumn{{Check()}} << QVariant(1) << true;
}

void tst_HelpersChecks::allOf()
{
    QFETCH(CheckListColumn, checks);
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::allOf(checks.checks)(value), passes);
}

void tst_HelpersChecks::oneOf_data()
{
    addColumns();
    QTest::newRow("exact") << QVariant(QStringLiteral("last_freed")) << true;
    QTest::newRow("upper case") << QVariant(QStringLiteral("LOWEST")) << true;
    QTest::newRow("unknown") << QVariant(QStringLiteral("highest")) << false;
    QTest::newRow("empty") << QVariant(QString()) << false;
}

void tst_HelpersChecks::oneOf()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::oneOf({QStringLiteral("lowest"), QStringLiteral("last_freed")})(value), passes);
}

void tst_HelpersChecks::emptyOrTime_data()
{
    addColumns();
    QTest::newRow("empty") << QVariant(QString()) << true;
    QTest::newRow("time") << QVariant(QTime(4, 30)) << true;
    QTest::newRow("not a time") << QVariant(QStringLiteral("not a time")) << false;
}

void tst_HelpersChecks::emptyOrTime()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::emptyOrTime()(value), passes);
}

void tst_HelpersChecks::url_data()
{
    addColumns();
    QTest::newRow("https") << QVariant(QStringLiteral("https://example.com/assets")) << true;
    QTest::newRow("http") << QVariant(QStringLiteral("http://example.com")) << true;
    QTest::newRow("other scheme") << QVariant(QStringLiteral("ftp://example.com")) << false;
    QTest::newRow("no host") << QVariant(QStringLiteral("https://")) << false;
    QTest::newRow("no scheme") << QVariant(QStringLiteral("example.com")) << false;
    QTest::newRow("empty") << QVariant(QString()) << false;
}

void tst_HelpersChecks::url()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::url()(value), passes);
}

void tst_HelpersChecks::emptyOr_data()
{
    QTest::addColumn<CheckColumn>("check");
    addColumns();

    const CheckColumn l_url{akashi::helpers::url()};
    QTest::newRow("empty skips the check") << l_url << QVariant(QString()) << true;
    QTest::newRow("value passing the check") << l_url << QVariant(QStringLiteral("https://example.com")) << true;
    QTest::newRow("value failing the check") << l_url << QVariant(QStringLiteral("not a url")) << false;
    QTest::newRow("no check") << CheckColumn{} << QVariant(QStringLiteral("anything")) << true;
}

void tst_HelpersChecks::emptyOr()
{
    QFETCH(CheckColumn, check);
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    QCOMPARE(akashi::helpers::emptyOr(check.check)(value), passes);
}

void tst_HelpersChecks::configEntry_data()
{
    addColumns();
    QTest::newRow("valid port") << QVariant(27016) << true;
    QTest::newRow("port zero") << QVariant(0) << false;
    QTest::newRow("not a number") << QVariant(QStringLiteral("abc")) << false;
}

void tst_HelpersChecks::configEntry()
{
    QFETCH(QVariant, value);
    QFETCH(bool, passes);

    // The config door forwards to the helpers, so both answer alike.
    const akashi::ConfigEntry l_entry(QStringLiteral("Options/port"), 27016, QStringLiteral("port"), akashi::inRange(1, 65535));
    QCOMPARE(l_entry.checkValue(value), passes);
}

} // namespace unittests
} // namespace tests

QTEST_GUILESS_MAIN(tests::unittests::tst_HelpersChecks)

#include "tst_helpers_checks.moc"
