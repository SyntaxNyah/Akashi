#include "akashi/config_store.h"
#include "commands/area_commands.h"
#include "commands/plugin_commands.h"
#include "commands/roleplay_commands.h"
#include "commands/rule_commands.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_params.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/player_directory.h"
#include "core/server_settings.h"
#include "testtools/fake_transport.h"

#include <QSet>
#include <QTemporaryDir>
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
    QStringList roleIds() const override { return {QStringLiteral("MODERATOR"), QStringLiteral("NONE"), QStringLiteral("SUPER")}; }

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
    void setpermsKeepsSuperBehindItsOwnForm_data();
    void setpermsKeepsSuperBehindItsOwnForm();
    void tiedWordRefusalsListEveryChoice();
    void commandForms_data();
    void commandForms();
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
        param::dice(QStringLiteral("roll"), [](const akashi::ParamScope &) { return 10; }, [](const akashi::ParamScope &) { return 100; }),
        param::text(QStringLiteral("note")),
    };
    const QStringList l_arguments{"1", "3", "5", "ON", "1h30m", "2d6+1", "hello", "there"};

    const FormReading l_reading = akashi::readForm(l_params, l_arguments, l_scope);
    QVERIFY2(l_reading.args.has_value(), qPrintable(l_reading.error.errorString()));
    const Args &l_args = *l_reading.args;
    QCOMPARE(l_args.client(QStringLiteral("target")), l_scope.client(1));
    QCOMPARE(l_args.number(QStringLiteral("destination")), qint64(3));
    QCOMPARE(l_args.number(QStringLiteral("count")), qint64(5));
    QCOMPARE(l_args.text(QStringLiteral("mode")), QStringLiteral("on"));
    QCOMPARE(l_args.number(QStringLiteral("for")), qint64(5400));
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
        return {param::client(QStringLiteral("target")), param::text(QStringLiteral("message")).max(10)};
    if (f_name == QLatin1String("sanction"))
        return {param::client(QStringLiteral("target")), param::when(QStringLiteral("until")).optional()};
    if (f_name == QLatin1String("account"))
        return {param::token(QStringLiteral("username")), param::token(QStringLiteral("password")).max(8)};
    if (f_name == QLatin1String("server_limit"))
        return {param::integer(QStringLiteral("rooms"), 1, 0).max([](const akashi::ParamScope &f_scope) { return qint64(f_scope.areaCount()); })};
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
    QTest::newRow("no time given") << "sanction" << QStringList{"1"} << ParseError::NoError << 4;
    QTest::newRow("a duration") << "sanction" << QStringList{"1", "1d12h"} << ParseError::NoError << 4;
    QTest::newRow("a weekday") << "sanction" << QStringList{"1", "friday"} << ParseError::NoError << 4;
    QTest::newRow("a date and time, two words") << "sanction" << QStringList{"1", "01.01.2099", "18:00"} << ParseError::NoError << 4;
    QTest::newRow("not a time") << "sanction" << QStringList{"1", "someday"} << ParseError::NotATime << 3;
    QTest::newRow("a date already past") << "sanction" << QStringList{"1", "01.01.2000"} << ParseError::NotATime << 3;
    QTest::newRow("two tokens") << "account" << QStringList{"bob", "Secret1!"} << ParseError::NoError << 4;
    QTest::newRow("a token over its length") << "account" << QStringList{"bob", "Secret12!"} << ParseError::TextTooLong << 3;
    QTest::newRow("a token is one word") << "account" << QStringList{"bob", "Secret1!", "more"} << ParseError::ExtraValue << 4;
    QTest::newRow("a limit from the server") << "server_limit" << QStringList{"4"} << ParseError::NoError << 2;
    QTest::newRow("over a limit from the server") << "server_limit" << QStringList{"5"} << ParseError::OutOfRange << 1;
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

// /setperms as core declares it: SUPER only through the form gated on
// super; every other role through the general form, which leaves SUPER out.
static CommandSpec setpermsSpec()
{
    const auto l_run = [](akashi::CommandContext &, const Args &) {};
    CommandSpec l_spec;
    l_spec.name = QStringLiteral("setperms");
    l_spec.variants = {
        {.id = QStringLiteral("super"), .gate = {QStringLiteral("super")}, .params = {param::token(QStringLiteral("username")), param::word(QStringLiteral("role"), {QStringLiteral("SUPER")})}, .run = l_run},
        {.id = QStringLiteral("role"), .gate = {QStringLiteral("modify_users")}, .params = {param::token(QStringLiteral("username")), param::role(QStringLiteral("role")).without({QStringLiteral("SUPER")})}, .run = l_run},
    };
    return l_spec;
}

void tst_CommandParams::setpermsKeepsSuperBehindItsOwnForm_data()
{
    QTest::addColumn<QString>("role");
    QTest::addColumn<QStringList>("permissions");
    QTest::addColumn<QString>("form");
    QTest::addColumn<QString>("stored");
    QTest::addColumn<bool>("denied");

    const QStringList l_supervisor{QStringLiteral("modify_users")};
    const QStringList l_super{QStringLiteral("super"), QStringLiteral("modify_users")};

    QTest::newRow("an ordinary role") << "moderator" << l_supervisor << "role" << "MODERATOR" << false;
    QTest::newRow("SUPER without super") << "SUPER" << l_supervisor << "" << "" << true;
    QTest::newRow("super without super") << "super" << l_supervisor << "" << "" << true;
    QTest::newRow("Super without super") << "Super" << l_supervisor << "" << "" << true;
    QTest::newRow("super with super") << "super" << l_super << "super" << "SUPER" << false;
    QTest::newRow("an ordinary role with super") << "none" << l_super << "role" << "NONE" << false;
    QTest::newRow("an unknown role") << "admin" << l_supervisor << "" << "" << false;
}

void tst_CommandParams::setpermsKeepsSuperBehindItsOwnForm()
{
    QFETCH(QString, role);
    QFETCH(QStringList, permissions);
    QFETCH(QString, form);
    QFETCH(QString, stored);
    QFETCH(bool, denied);

    FakeScope l_scope;
    const CommandSpec l_spec = setpermsSpec();
    const auto l_can = [&permissions](const QString &f_permission) { return permissions.contains(f_permission); };
    const akashi::FormChoice l_choice = akashi::chooseForm(l_spec, {QStringLiteral("bob"), role}, l_scope, l_can);

    QCOMPARE(l_choice.form ? l_choice.form->id : QString(), form);
    QCOMPARE(l_choice.denied, denied);
    if (l_choice.form)
        QCOMPARE(l_choice.args->text(QStringLiteral("role")), stored);
    if (form.isEmpty() && !denied) {
        // An unknown role lists the ones that may be given - never SUPER.
        QCOMPARE(l_choice.error.error, ParseError::UnknownWord);
        QVERIFY(!l_choice.error.choices.contains(QStringLiteral("SUPER")));
    }
}

void tst_CommandParams::tiedWordRefusalsListEveryChoice()
{
    // /update_ban's forms differ by the field word; a wrong field is refused
    // with every field any form would have taken.
    const auto l_run = [](akashi::CommandContext &, const Args &) {};
    const Param l_id = param::integer(QStringLiteral("ban_id"), 0, 100);
    CommandSpec l_spec;
    l_spec.name = QStringLiteral("update_ban");
    l_spec.variants = {
        {.id = QStringLiteral("duration"), .params = {l_id, param::word(QStringLiteral("field"), {QStringLiteral("duration")}), param::duration(QStringLiteral("duration"))}, .run = l_run},
        {.id = QStringLiteral("reason"), .params = {l_id, param::word(QStringLiteral("field"), {QStringLiteral("reason")}), param::text(QStringLiteral("reason"))}, .run = l_run},
    };
    FakeScope l_scope;
    const auto l_open = [](const QString &) { return true; };

    const akashi::FormChoice l_choice = akashi::chooseForm(l_spec, {QStringLiteral("3"), QStringLiteral("colour"), QStringLiteral("red")}, l_scope, l_open);
    QVERIFY(!l_choice.form);
    QCOMPARE(l_choice.error.error, ParseError::UnknownWord);
    QCOMPARE(l_choice.error.choices, (QStringList{QStringLiteral("duration"), QStringLiteral("reason")}));
    QCOMPARE(l_choice.error.errorString(), QStringLiteral("\"colour\" is not a valid field. Use one of: duration, reason."));
}

// The area commands whose forms carry a permission or a choice, as core
// declares them: /cm keeps claiming an empty area behind area.cm, /uncm
// tells "all" from a client, /removearea tells an ID from a name, and
// /uninvite cannot reach a CM of the area.
// The commands as core really registers them, so a form table checks the
// shipped declarations rather than a copy of them.
static CommandSpec registeredSpec(const QString &f_name)
{
    static QTemporaryDir s_dir;
    static akashi::ConfigStore s_store(s_dir.path());
    static ServerSettings s_settings(&s_store);
    static akashi::CommandRegistry s_registry;
    static const bool s_registered = [] {
        s_settings.declare();
        akashi::commands::registerAreaCommands(s_registry);
        akashi::commands::registerRoleplayCommands(s_registry, s_settings);
        akashi::commands::registerPluginCommands(s_registry);
        akashi::commands::registerRuleCommands(s_registry);
        return true;
    }();
    Q_UNUSED(s_registered)
    return s_registry.spec(f_name).value_or(CommandSpec{});
}

void tst_CommandParams::commandForms_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QStringList>("permissions");
    QTest::addColumn<QString>("form");
    QTest::addColumn<ParseError::Error>("error");
    QTest::addColumn<bool>("denied");

    const QStringList l_user{QStringLiteral("user")};
    const QStringList l_walk_in{QStringLiteral("user"), QStringLiteral("area.cm")};
    const QStringList l_uncm{QStringLiteral("area.uncm")};
    const QStringList l_floors{QStringLiteral("modify_floors")};
    const QStringList l_uninvite{QStringLiteral("cm.uninvite"), QStringLiteral("super")};

    QTest::newRow("cm: area.cm claims") << "cm" << QStringList{} << l_walk_in << "claim" << ParseError::NoError << false;
    QTest::newRow("cm: without area.cm the open form answers") << "cm" << QStringList{} << l_user << "own" << ParseError::NoError << false;
    QTest::newRow("cm: a target is the add form") << "cm" << QStringList{"1"} << l_user << "add" << ParseError::NoError << false;
    QTest::newRow("cm: a target with area.cm too") << "cm" << QStringList{"1"} << l_walk_in << "add" << ParseError::NoError << false;
    QTest::newRow("cm: no such client") << "cm" << QStringList{"3"} << l_user << "" << ParseError::NoSuchClient << false;
    QTest::newRow("cm: nothing open") << "cm" << QStringList{} << QStringList{} << "" << ParseError::NoError << true;
    QTest::newRow("uncm: your own") << "uncm" << QStringList{} << QStringList{"area.cm"} << "own" << ParseError::NoError << false;
    QTest::newRow("uncm: all is a word") << "uncm" << QStringList{"all"} << l_uncm << "all" << ParseError::NoError << false;
    QTest::newRow("uncm: another client") << "uncm" << QStringList{"2"} << l_uncm << "other" << ParseError::NoError << false;
    QTest::newRow("uncm: others need area.uncm") << "uncm" << QStringList{"all"} << QStringList{"area.cm"} << "" << ParseError::NoError << true;
    QTest::newRow("uncm: your own needs area.cm") << "uncm" << QStringList{} << l_uncm << "" << ParseError::NoError << true;
    QTest::newRow("removearea: an ID") << "removearea" << QStringList{"3"} << l_floors << "id" << ParseError::NoError << false;
    QTest::newRow("removearea: a name") << "removearea" << QStringList{"Bell", "Chamber"} << l_floors << "name" << ParseError::NoError << false;
    QTest::newRow("removearea: no such ID reads as a name") << "removearea" << QStringList{"9"} << l_floors << "name" << ParseError::NoError << false;
    QTest::newRow("uninvite: a client") << "uninvite" << QStringList{"2"} << l_uninvite << "run" << ParseError::NoError << false;
    QTest::newRow("uninvite: a CM of this area, even for super") << "uninvite" << QStringList{"1"} << l_uninvite << "" << ParseError::Immune << false;

    const QStringList l_timer{QStringLiteral("cm.timer")};
    const QStringList l_global{QStringLiteral("cm.timer"), QStringLiteral("timer.global")};
    QTest::newRow("timer: every timer") << "timer" << QStringList{} << l_timer << "list" << ParseError::NoError << false;
    QTest::newRow("timer: one timer") << "timer" << QStringList{"2"} << l_timer << "show" << ParseError::NoError << false;
    QTest::newRow("timer: viewing the global timer is open") << "timer" << QStringList{"0"} << l_timer << "show" << ParseError::NoError << false;
    QTest::newRow("timer: an action") << "timer" << QStringList{"2", "start"} << l_timer << "action" << ParseError::NoError << false;
    QTest::newRow("timer: a time") << "timer" << QStringList{"2", "00:05:00"} << l_timer << "set" << ParseError::NoError << false;
    QTest::newRow("timer: altering timer 0 needs timer.global") << "timer" << QStringList{"0", "start"} << l_timer << "" << ParseError::NoError << true;
    QTest::newRow("timer: with timer.global") << "timer" << QStringList{"0", "start"} << l_global << "global_action" << ParseError::NoError << false;
    QTest::newRow("timer: global set") << "timer" << QStringList{"0", "00:01:00"} << l_global << "global_set" << ParseError::NoError << false;
    QTest::newRow("timer: no such timer") << "timer" << QStringList{"5", "start"} << l_global << "" << ParseError::OutOfRange << false;

    const QStringList l_roll{QStringLiteral("roleplay.roll")};
    QTest::newRow("roll: one six-sided die") << "roll" << QStringList{} << l_roll << "plain" << ParseError::NoError << false;
    QTest::newRow("roll: sides and count") << "roll" << QStringList{"20", "2"} << l_roll << "plain" << ParseError::NoError << false;
    QTest::newRow("roll: notation") << "roll" << QStringList{"2d6+1"} << l_roll << "notation" << ParseError::NoError << false;
    QTest::newRow("roll: neither") << "roll" << QStringList{"abc"} << l_roll << "" << ParseError::NotANumber << false;

    const QStringList l_super{QStringLiteral("super")};
    QTest::newRow("plugin: list") << "plugin" << QStringList{"list"} << l_super << "list" << ParseError::NoError << false;
    QTest::newRow("plugin: unload with cascade") << "plugin" << QStringList{"unload", "akashi.x", "--cascade"} << l_super << "unload" << ParseError::NoError << false;
    QTest::newRow("plugin: no such subcommand") << "plugin" << QStringList{"frob"} << l_super << "" << ParseError::UnknownWord << false;
    QTest::newRow("plugin: needs super") << "plugin" << QStringList{"list"} << l_user << "" << ParseError::NoError << true;

    QTest::newRow("floorrule: add") << "floorrule" << QStringList{"add", "ic_message_sent", "block", "message=No", "talking"} << l_floors << "add" << ParseError::NoError << false;
    QTest::newRow("floorrule: remove") << "floorrule" << QStringList{"remove", "ic_message_sent"} << l_floors << "remove" << ParseError::NoError << false;
    QTest::newRow("floorrule: no such mode") << "floorrule" << QStringList{"nuke", "ic_message_sent"} << l_floors << "" << ParseError::UnknownWord << false;
}

void tst_CommandParams::commandForms()
{
    QFETCH(QString, command);
    QFETCH(QStringList, arguments);
    QFETCH(QStringList, permissions);
    QFETCH(QString, form);
    QFETCH(ParseError::Error, error);
    QFETCH(bool, denied);

    FakeScope l_scope;
    l_scope.area_owners.insert(1);
    for (const QString &l_permission : std::as_const(permissions))
        l_scope.invoker_permissions.insert(l_permission);
    const CommandSpec l_spec = registeredSpec(command);
    QVERIFY2(!l_spec.variants.isEmpty(), qPrintable("no declared command " + command));
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
    // Every form has a body.
    CommandSpec l_no_body = l_declared({});
    l_no_body.variants.append({.id = QStringLiteral("empty")});
    QVERIFY(!l_registry.registerCommand(l_no_body, QStringLiteral("core")));

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
