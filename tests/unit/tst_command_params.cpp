#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_params.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/player_directory.h"
#include "testtools/fake_transport.h"

#include <QSet>
#include <QTest>

using akashi::Args;
using akashi::CommandSpec;
using akashi::CommandVariant;
using akashi::FormReading;
using akashi::Gate;
using akashi::Param;
using akashi::helpers::ParseError;
namespace param = akashi::param;

namespace tests {
namespace unittests {

// The world as parameters see it, set up by hand: clients 0 (the
// invoker), 1 and 2 are connected; 0 and 1 stand in area 2, client 2 in
// area 3; there are four areas.
class FakeScope : public akashi::ParamScope
{
  public:
    FakeScope()
    {
        m_directory.setCapacity(4);
        for (int l_id = 0; l_id < 3; ++l_id) {
            auto *l_client = new akashi::ClientSession(nullptr, new akashi::FakeTransport(true), l_id);
            l_client->setAreaId(l_id == 2 ? 3 : 2);
            m_directory.addClient(l_id, l_client);
            m_clients << l_client;
        }
    }
    ~FakeScope() override { qDeleteAll(m_clients); }

    int invokerId() const override { return 0; }
    int invokerArea() const override { return 2; }
    int areaCount() const override { return 4; }
    bool invokerCan(const QString &f_permission) const override { return invoker_permissions.contains(f_permission); }
    akashi::helpers::Result<akashi::ClientSession *> findClient(const QString &f_text, const akashi::PlayerFilter &f_filter) const override
    {
        return m_directory.find(f_text, f_filter);
    }
    bool clientCan(akashi::ClientSession *f_client, const QString &f_permission) const override
    {
        return client_permissions.value(f_client->clientId()).contains(f_permission);
    }
    bool ownsInvokerArea(akashi::ClientSession *f_client) const override { return area_owners.contains(f_client->clientId()); }
    int setting(const QString &f_name) const override { return f_name == QLatin1String("max_dice") ? 10 : 100; }

    akashi::ClientSession *client(int f_id) const { return m_clients.at(f_id); }

    QSet<QString> invoker_permissions;
    QHash<int, QSet<QString>> client_permissions;
    QSet<int> area_owners;

  private:
    akashi::PlayerDirectory m_directory;
    QList<akashi::ClientSession *> m_clients;
};

class tst_CommandParams : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void readsEveryKind();
    void readForm_data();
    void readForm();
    void clientFilters_data();
    void clientFilters();
    void chooseForm_data();
    void chooseForm();
    void usageIsGenerated();
    void registryRefusesBadlyMadeForms();
    void sensitiveParametersAreRedacted();
};

void tst_CommandParams::readsEveryKind()
{
    FakeScope l_scope;
    const QList<Param> l_params{
        param::client(QStringLiteral("target")),
        param::area(QStringLiteral("destination")),
        param::integer(QStringLiteral("count"), 1, 10),
        param::word(QStringLiteral("mode"), {QStringLiteral("on"), QStringLiteral("off")}),
        param::duration(QStringLiteral("for")),
        param::dice(QStringLiteral("roll")),
        param::text(QStringLiteral("note")),
    };
    const QStringList l_arguments{"1", "3", "5", "ON", "1h30m", "2d6+1", "hello", "there"};

    const FormReading l_reading = akashi::readForm(l_params, l_arguments, l_scope);
    QVERIFY2(l_reading.args.has_value(), qPrintable(l_reading.error.errorString()));
    const Args &l_args = *l_reading.args;
    QCOMPARE(l_args.client(QStringLiteral("target")), l_scope.client(1));
    QCOMPARE(l_args.area(QStringLiteral("destination")), 3);
    QCOMPARE(l_args.integer(QStringLiteral("count")), 5);
    QCOMPARE(l_args.word(QStringLiteral("mode")), QStringLiteral("on"));
    QCOMPARE(l_args.duration(QStringLiteral("for")), qint64(5400));
    QCOMPARE(l_args.dice(QStringLiteral("roll")).count, 2);
    QCOMPARE(l_args.dice(QStringLiteral("roll")).modifier, 1);
    // The text parameter takes the rest of the line.
    QCOMPARE(l_args.text(QStringLiteral("note")), QStringLiteral("hello there"));
}

// The forms the readForm rows read against, by name.
static QList<Param> formNamed(const QString &f_name)
{
    if (f_name == QLatin1String("kick"))
        return {param::client(QStringLiteral("target")), param::area(QStringLiteral("destination"))};
    if (f_name == QLatin1String("optional"))
        return {param::integer(QStringLiteral("sides"), 1, 100).optional(), param::integer(QStringLiteral("dice"), 1, 10).optional()};
    if (f_name == QLatin1String("message"))
        return {param::client(QStringLiteral("target")), param::text(QStringLiteral("message")).maxLength(10)};
    if (f_name == QLatin1String("server_limit"))
        return {param::integer(QStringLiteral("dice"), 1, 0).max([](const akashi::ParamScope &f_scope) { return qint64(f_scope.setting(QStringLiteral("max_dice"))); })};
    return {};
}

void tst_CommandParams::readForm_data()
{
    QTest::addColumn<QString>("form");
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<int>("score");

    // The score counts the values read, twice, plus one for a value refusal.
    QTest::newRow("fits") << "kick" << QStringList{"1", "3"} << ParseError::NoError << 4;
    QTest::newRow("bad client") << "kick" << QStringList{"x", "3"} << ParseError::NotANumber << 1;
    QTest::newRow("bad area after a good client") << "kick" << QStringList{"1", "9"} << ParseError::OutOfRange << 3;
    QTest::newRow("missing the second") << "kick" << QStringList{"1"} << ParseError::MissingValue << 2;
    QTest::newRow("missing everything") << "kick" << QStringList{} << ParseError::MissingValue << 0;
    QTest::newRow("one too many") << "kick" << QStringList{"1", "3", "extra"} << ParseError::ExtraValue << 4;
    QTest::newRow("optionals left off") << "optional" << QStringList{} << ParseError::NoError << 4;
    QTest::newRow("first optional given") << "optional" << QStringList{"20"} << ParseError::NoError << 4;
    QTest::newRow("optional still checked") << "optional" << QStringList{"20", "11"} << ParseError::OutOfRange << 3;
    QTest::newRow("text within its length") << "message" << QStringList{"1", "hi", "there"} << ParseError::NoError << 4;
    QTest::newRow("text over its length") << "message" << QStringList{"1", "hello", "there", "again"} << ParseError::TextTooLong << 3;
    QTest::newRow("text is required") << "message" << QStringList{"1"} << ParseError::MissingValue << 2;
    QTest::newRow("a limit from the server") << "server_limit" << QStringList{"10"} << ParseError::NoError << 2;
    QTest::newRow("over a limit from the server") << "server_limit" << QStringList{"11"} << ParseError::OutOfRange << 1;
}

void tst_CommandParams::readForm()
{
    QFETCH(QString, form);
    QFETCH(QStringList, arguments);
    QFETCH(ParseError::Error, error);
    QFETCH(int, score);

    FakeScope l_scope;
    const FormReading l_reading = akashi::readForm(formNamed(form), arguments, l_scope);
    QCOMPARE(l_reading.error.error, error);
    QCOMPARE(l_reading.args.has_value(), error == ParseError::NoError);
    QCOMPARE(l_reading.score, score);
}

void tst_CommandParams::clientFilters_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("options");
    QTest::addColumn<bool>("invoker_is_super");
    QTest::addColumn<ParseError::Error>("error");

    QTest::newRow("anyone") << "0" << "" << false << ParseError::NoError;
    QTest::newRow("not self refuses the invoker") << "0" << "not_self" << false << ParseError::IsSelf;
    QTest::newRow("not self lets others through") << "1" << "not_self" << false << ParseError::NoError;
    QTest::newRow("in area refuses another area") << "2" << "in_area" << false << ParseError::NotInArea;
    QTest::newRow("in area lets this area through") << "1" << "in_area" << false << ParseError::NoError;
    QTest::newRow("immune by permission") << "1" << "immune_permission" << false << ParseError::Immune;
    QTest::newRow("super reaches past permission immunity") << "1" << "immune_permission" << true << ParseError::NoError;
    QTest::newRow("no permission, no immunity") << "2" << "immune_permission" << false << ParseError::NoError;
    QTest::newRow("immune as an area owner") << "1" << "immune_owner" << false << ParseError::Immune;
    QTest::newRow("super does not reach past owner immunity") << "1" << "immune_owner" << true << ParseError::Immune;
    QTest::newRow("not an owner") << "2" << "immune_owner" << false << ParseError::NoError;
    QTest::newRow("nobody") << "3" << "" << false << ParseError::NoSuchClient;
}

void tst_CommandParams::clientFilters()
{
    QFETCH(QString, text);
    QFETCH(QString, options);
    QFETCH(bool, invoker_is_super);
    QFETCH(ParseError::Error, error);

    FakeScope l_scope;
    l_scope.client_permissions.insert(1, {QStringLiteral("sanction.immune")});
    l_scope.area_owners.insert(1);
    if (invoker_is_super)
        l_scope.invoker_permissions.insert(QStringLiteral("super"));

    Param l_param = param::client(QStringLiteral("target"));
    if (options == QLatin1String("not_self"))
        l_param = l_param.notSelf();
    if (options == QLatin1String("in_area"))
        l_param = l_param.inArea();
    if (options == QLatin1String("immune_permission"))
        l_param = l_param.immuneIf(QStringLiteral("sanction.immune"));
    if (options == QLatin1String("immune_owner"))
        l_param = l_param.immuneIfAreaOwner();

    const FormReading l_reading = akashi::readForm({l_param}, {text}, l_scope);
    QCOMPARE(l_reading.error.error, error);
}

// /area_kick as core declares it: one client or everyone, to the lobby
// with cm.kick or elsewhere with area.cm and kick.
static CommandSpec areaKickSpec()
{
    const Param l_target = param::client(QStringLiteral("target")).inArea().immuneIfAreaOwner();
    const Param l_all = param::word(QStringLiteral("who"), {QStringLiteral("all")});
    const Param l_destination = param::area(QStringLiteral("destination"));
    const Gate l_cross = Gate::allOf({QStringLiteral("area.cm"), QStringLiteral("kick")});
    const auto l_run = [](akashi::CommandContext &, const Args &) {};
    CommandSpec l_spec;
    l_spec.name = QStringLiteral("area_kick");
    l_spec.variants = {
        {.id = QStringLiteral("one"), .gate = {QStringLiteral("cm.kick")}, .params = {l_target}, .run = l_run},
        {.id = QStringLiteral("all"), .gate = {QStringLiteral("cm.kick")}, .params = {l_all}, .run = l_run},
        {.id = QStringLiteral("one_to"), .gate = l_cross, .params = {l_target, l_destination}, .run = l_run},
        {.id = QStringLiteral("all_to"), .gate = l_cross, .params = {l_all, l_destination}, .run = l_run},
    };
    return l_spec;
}

void tst_CommandParams::chooseForm_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QStringList>("permissions");
    QTest::addColumn<QString>("form");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<bool>("denied");
    QTest::addColumn<bool>("target_owns_area");

    const QStringList l_cm{QStringLiteral("cm.kick")};
    const QStringList l_staff{QStringLiteral("cm.kick"), QStringLiteral("area.cm"), QStringLiteral("kick")};

    QTest::newRow("a client") << QStringList{"1"} << l_cm << "one" << ParseError::NoError << false << false;
    QTest::newRow("all is a word, not a client") << QStringList{"all"} << l_cm << "all" << ParseError::NoError << false << false;
    QTest::newRow("to another area") << QStringList{"1", "3"} << l_staff << "one_to" << ParseError::NoError << false << false;
    QTest::newRow("all to another area") << QStringList{"all", "3"} << l_staff << "all_to" << ParseError::NoError << false << false;
    // The form that read furthest explains the refusal: a wrong area beats
    // "one argument too many" from the single-client form.
    QTest::newRow("a bad area") << QStringList{"1", "9"} << l_staff << "" << ParseError::OutOfRange << false << false;
    QTest::newRow("a client elsewhere") << QStringList{"2"} << l_cm << "" << ParseError::NotInArea << false << false;
    QTest::newRow("a CM of this area") << QStringList{"1"} << l_cm << "" << ParseError::Immune << false << true;
    QTest::newRow("neither client nor all") << QStringList{"bob"} << l_cm << "" << ParseError::NotANumber << false << false;
    // Only a form the caller may not use would fit: a permission matter,
    // and the caller hears nothing about what its lookups found.
    QTest::newRow("cross-area without its permissions") << QStringList{"1", "3"} << l_cm << "" << ParseError::NoError << true << false;
    QTest::newRow("no form open at all") << QStringList{"1"} << QStringList{} << "" << ParseError::NoError << true << false;
}

void tst_CommandParams::chooseForm()
{
    QFETCH(QStringList, arguments);
    QFETCH(QStringList, permissions);
    QFETCH(QString, form);
    QFETCH(ParseError::Error, error);
    QFETCH(bool, denied);
    QFETCH(bool, target_owns_area);

    FakeScope l_scope;
    if (target_owns_area)
        l_scope.area_owners.insert(1);
    const CommandSpec l_spec = areaKickSpec();
    const auto l_can = [&permissions](const QString &f_permission) { return permissions.contains(f_permission); };

    const akashi::FormChoice l_choice = akashi::chooseForm(l_spec, arguments, l_scope, l_can);
    QCOMPARE(l_choice.form ? l_choice.form->id : QString(), form);
    QCOMPARE(l_choice.denied, denied);
    if (!form.isEmpty() || denied)
        return;
    QCOMPARE(l_choice.error.error, error);
}

void tst_CommandParams::usageIsGenerated()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(areaKickSpec(), QStringLiteral("core")));

    const auto l_spec = l_registry.spec(QStringLiteral("area_kick"));
    QStringList l_usages;
    for (const CommandVariant &l_variant : l_spec->variants)
        l_usages << l_variant.usage;
    QCOMPARE(l_usages, (QStringList{"/area_kick <target>", "/area_kick all", "/area_kick <target> <destination>",
                                    "/area_kick all <destination>"}));
    QCOMPARE(l_spec->usage, l_usages.join(QLatin1Char('\n')));

    // The other placeholders: optional, a choice of words, the rest of the line.
    QCOMPARE(akashi::usageOf(QStringLiteral("x"), {param::integer(QStringLiteral("sides"), 1, 6).optional(),
                                                   param::word(QStringLiteral("mode"), {QStringLiteral("on"), QStringLiteral("off")}),
                                                   param::text(QStringLiteral("reason")).optional()}),
             QStringLiteral("/x [sides] <on|off> [reason...]"));
}

void tst_CommandParams::registryRefusesBadlyMadeForms()
{
    const auto l_run = [](akashi::CommandContext &, const Args &) {};
    const auto l_declared = [&l_run](const QList<Param> &f_params) {
        CommandSpec l_spec;
        l_spec.name = QStringLiteral("probe");
        l_spec.variants = {{.id = QStringLiteral("form"), .params = f_params, .run = l_run}};
        return l_spec;
    };
    akashi::CommandRegistry l_registry;

    // Text takes the rest of the line, so nothing may follow it.
    QVERIFY(!l_registry.registerCommand(l_declared({param::text(QStringLiteral("a")), param::integer(QStringLiteral("b"), 0, 1)}), QStringLiteral("core")));
    // Optional parameters come last.
    QVERIFY(!l_registry.registerCommand(l_declared({param::integer(QStringLiteral("a"), 0, 1).optional(), param::integer(QStringLiteral("b"), 0, 1)}), QStringLiteral("core")));
    // Names are unique.
    QVERIFY(!l_registry.registerCommand(l_declared({param::integer(QStringLiteral("a"), 0, 1), param::integer(QStringLiteral("a"), 0, 1)}), QStringLiteral("core")));
    // A command is declared or not as a whole.
    CommandSpec l_mixed = l_declared({});
    l_mixed.variants.append({.id = QStringLiteral("legacy"), .min_args = 1, .handler = [](akashi::CommandContext &) {}});
    QVERIFY(!l_registry.registerCommand(l_mixed, QStringLiteral("core")));

    QVERIFY(l_registry.registerCommand(l_declared({param::integer(QStringLiteral("a"), 0, 1), param::text(QStringLiteral("b")).optional()}), QStringLiteral("core")));
}

void tst_CommandParams::sensitiveParametersAreRedacted()
{
    // The log runs before a form is picked, so the earliest sensitive
    // position of any form is where hiding starts.
    const auto l_run = [](akashi::CommandContext &, const Args &) {};
    CommandSpec l_spec;
    l_spec.name = QStringLiteral("changepass");
    l_spec.variants = {
        {.id = QStringLiteral("own"), .params = {param::text(QStringLiteral("password")).sensitive()}, .run = l_run},
        {.id = QStringLiteral("user"), .params = {param::word(QStringLiteral("user"), {QStringLiteral("x")}), param::text(QStringLiteral("password")).sensitive()}, .run = l_run},
    };
    QCOMPARE(l_spec.sensitiveFrom(), 0);

    l_spec.variants.removeFirst();
    QCOMPARE(l_spec.sensitiveFrom(), 1);

    l_spec.variants.first().params.last() = param::text(QStringLiteral("password"));
    QCOMPARE(l_spec.sensitiveFrom(), -1);
}

} // namespace unittests
} // namespace tests

QTEST_GUILESS_MAIN(tests::unittests::tst_CommandParams)

#include "tst_command_params.moc"
