#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/permission_registry.h"

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using akashi::ACLRole;

namespace tests {
namespace unittests {

class tst_CommandRegistry : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void registerAndLookup();
    void lookupByAlias();
    void caseInsensitiveLookup();
    void duplicateRegistrationFails();
    void collidingAliasRegistrationFails();
    void unregisterByOwner();
    void unregisterUnknownOwnerIsANoOp();
    void containsCheck();
    void commandNamesList();

    void registerRefusesATakenSlot();
    void replaceTakesOverAndDropsThePrevious();
    void replaceKeepsTheOperatorsAliases();
    void replaceRefusesAnAliasIntoAnotherSlot();
    void vacateEmptiesTheSlot();
    void unloadLeavesATakenSlotEmpty();
    void unloadOfAPluginsOwnCommandIsQuiet();

    void aclRoleStringCanPerform();
    void aclRoleSuperWildcard();
    void aclRoleEmptyIsNone();

    void applyExtensionsAddsAliases();
    void applyExtensionsReportsAndIgnoresPermissionOverrides();
    void requirementGroupsExpressAnd();
    void applyExtensionsSkipsUnknownCommand();
    void applyExtensionsSkipsCollidingAlias();
    void applyExtensionsMissingFileIsNoop();
    void applyExtensionsSilentlySkipsSameCommandAlias();

    void canUseMirrorsTheDispatchGate();
    void canUseChecksEveryVariantForm();

    void variantMatchPicksByArgCount();
    void registerCommandValidatesVariants();

  private:
    static QString dispatchedDescription(const akashi::CommandRegistry &f_registry, const QString &f_name);
};

// A two-form command like /listperms: a free self form and a gated
// one-or-more-argument form.
static akashi::CommandSpec makeVariantSpec(const QString &f_name)
{
    return akashi::CommandSpec{
        .name = f_name,
        .usage = "/" + f_name + " [target]",
        .variants = {
            {QStringLiteral("own"), 0, 0, {}, {}, {}, [](akashi::CommandContext &) {}},
            {QStringLiteral("user"), 1, -1, {QStringLiteral("modify_users")}, {}, {}, [](akashi::CommandContext &) {}},
        },
    };
}

void tst_CommandRegistry::registerAndLookup()
{
    akashi::CommandRegistry l_registry;
    bool l_handler_called = false;
    akashi::CommandSpec l_spec{QStringLiteral("test"), {}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(l_spec, [&](akashi::CommandContext &) { l_handler_called = true; }));
    QVERIFY(l_registry.contains("test"));

    if (auto l_found = l_registry.spec("test")) {
        QCOMPARE(l_found->name, QStringLiteral("test"));
    }
    else {
        QFAIL("spec not found");
    }

    // Only the dispatcher (and this friend test) can reach the handler.
    const auto l_resolved = l_registry.lookup("test");
    QVERIFY(l_resolved.has_value());
    QVERIFY(l_resolved->handler);
}

static akashi::CommandSpec simpleSpec(const QString &f_name, const QStringList &f_aliases = {})
{
    return akashi::CommandSpec{f_name, f_aliases, {}, 0, {}, {}};
}

// Which definition the dispatcher would run for the name, told apart by
// its description; empty when nothing would run.
QString tst_CommandRegistry::dispatchedDescription(const akashi::CommandRegistry &f_registry, const QString &f_name)
{
    const auto l_resolved = f_registry.lookup(f_name);
    if (!l_resolved || !l_resolved->handler)
        return {};
    return l_resolved->spec.description;
}

void tst_CommandRegistry::registerRefusesATakenSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("roll"), {QStringLiteral("dice")}), [](akashi::CommandContext &) {}, QStringLiteral("core")));

    // The name, or an alias of it, is a take-over: that is replace().
    QVERIFY(!l_registry.registerCommand(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("dice-plus")));
    QVERIFY(!l_registry.registerCommand(simpleSpec(QStringLiteral("dice")), [](akashi::CommandContext &) {}, QStringLiteral("dice-plus")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("core"));
}

void tst_CommandRegistry::replaceTakesOverAndDropsThePrevious()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_core = simpleSpec(QStringLiteral("roll"), {QStringLiteral("dice")});
    l_core.description = QStringLiteral("core roll");
    QVERIFY(l_registry.registerCommand(l_core, [](akashi::CommandContext &) {}, QStringLiteral("core")));

    akashi::CommandSpec l_plugin = simpleSpec(QStringLiteral("roll"), {QStringLiteral("rr")});
    l_plugin.description = QStringLiteral("plugin roll");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("dice-plus.*replaced the core command /roll"));
    QVERIFY(l_registry.replace(l_plugin, [](akashi::CommandContext &) {}, QStringLiteral("dice-plus")));

    // The plugin's definition answers now, and says whose it replaced.
    QCOMPARE(dispatchedDescription(l_registry, QStringLiteral("roll")), QStringLiteral("plugin roll"));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("dice-plus"));
    QCOMPARE(l_registry.replacedOwnerOf(QStringLiteral("roll")), QStringLiteral("core"));
    // Core's own alias left with core's definition; the plugin's arrived.
    QVERIFY(!l_registry.contains(QStringLiteral("dice")));
    QVERIFY(l_registry.contains(QStringLiteral("rr")));

    // Last wins: a later replace takes it again, quietly between plugins.
    akashi::CommandSpec l_later = simpleSpec(QStringLiteral("roll"));
    l_later.description = QStringLiteral("later roll");
    QVERIFY(l_registry.replace(l_later, [](akashi::CommandContext &) {}, QStringLiteral("dice-pro")));
    QCOMPARE(dispatchedDescription(l_registry, QStringLiteral("roll")), QStringLiteral("later roll"));
    QCOMPARE(l_registry.replacedOwnerOf(QStringLiteral("roll")), QStringLiteral("dice-plus"));
    QVERIFY(!l_registry.contains(QStringLiteral("rr")));
}

void tst_CommandRegistry::replaceKeepsTheOperatorsAliases()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("core")));

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"roll": {"aliases": "r"}})");
    l_file.close();
    l_registry.applyExtensions(l_path);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("replaced the core command /roll"));
    QVERIFY(l_registry.replace(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("dice-plus")));

    // The operator's alias belongs to the slot, so it reaches the new occupant.
    QVERIFY(l_registry.contains(QStringLiteral("r")));
    QCOMPARE(l_registry.spec(QStringLiteral("r"))->name, QStringLiteral("roll"));
    QVERIFY(l_registry.spec(QStringLiteral("roll"))->aliases.contains(QStringLiteral("r")));
}

void tst_CommandRegistry::replaceRefusesAnAliasIntoAnotherSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("core")));
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("kick"), {QStringLiteral("k")}), [](akashi::CommandContext &) {}, QStringLiteral("core")));

    // One replace may not take a second command through an alias, nor
    // replace a command by naming another's alias.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not replace /roll"));
    QVERIFY(!l_registry.replace(simpleSpec(QStringLiteral("roll"), {QStringLiteral("kick")}), [](akashi::CommandContext &) {}, QStringLiteral("p")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not replace /k"));
    QVERIFY(!l_registry.replace(simpleSpec(QStringLiteral("k")), [](akashi::CommandContext &) {}, QStringLiteral("p")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("core"));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("kick")), QStringLiteral("core"));
}

void tst_CommandRegistry::vacateEmptiesTheSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("8ball"), {QStringLiteral("eightball")}), [](akashi::CommandContext &) {}, QStringLiteral("core")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no-fun vacated /8ball"));
    QVERIFY(l_registry.vacate(QStringLiteral("8ball"), QStringLiteral("no-fun")));

    QVERIFY(!l_registry.contains(QStringLiteral("8ball")));
    QVERIFY(!l_registry.contains(QStringLiteral("eightball")));
    QVERIFY(!l_registry.lookup(QStringLiteral("8ball")));
    QVERIFY(!l_registry.commandNames().contains(QStringLiteral("8ball")));
    QCOMPARE(l_registry.vacatedSlots().value(QStringLiteral("8ball")), QStringLiteral("no-fun"));
    // An empty slot cannot be emptied again.
    QVERIFY(!l_registry.vacate(QStringLiteral("8ball"), QStringLiteral("no-fun")));

    // It stays empty until something registers it.
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("8ball")), [](akashi::CommandContext &) {}, QStringLiteral("fortune")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("8ball")), QStringLiteral("fortune"));
    QVERIFY(l_registry.vacatedSlots().isEmpty());
}

void tst_CommandRegistry::unloadLeavesATakenSlotEmpty()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("core")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("replaced the core command /roll"));
    QVERIFY(l_registry.replace(simpleSpec(QStringLiteral("roll")), [](akashi::CommandContext &) {}, QStringLiteral("dice-plus")));

    // The plugin unloads: nothing is restored, and the hole is reported.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("dice-plus.*left /roll empty"));
    l_registry.unregisterAll(QStringLiteral("dice-plus"));
    QVERIFY(!l_registry.contains(QStringLiteral("roll")));
    QCOMPARE(l_registry.vacatedSlots().value(QStringLiteral("roll")), QStringLiteral("dice-plus"));
}

void tst_CommandRegistry::unloadOfAPluginsOwnCommandIsQuiet()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(simpleSpec(QStringLiteral("slowmode")), [](akashi::CommandContext &) {}, QStringLiteral("slowmode")));

    // Its own command leaving with it is ordinary: no warning, no hole.
    QTest::failOnWarning(QRegularExpression("left /slowmode empty"));
    l_registry.unregisterAll(QStringLiteral("slowmode"));
    QVERIFY(!l_registry.contains(QStringLiteral("slowmode")));
    QVERIFY(l_registry.vacatedSlots().isEmpty());
}

void tst_CommandRegistry::lookupByAlias()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec{QStringLiteral("getarea"), {QStringLiteral("ga")}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(l_spec, [](akashi::CommandContext &) {}));

    QVERIFY(l_registry.contains("ga"));
    if (auto l_found = l_registry.spec("ga")) {
        QCOMPARE(l_found->name, QStringLiteral("getarea"));
    }
    else {
        QFAIL("alias lookup failed");
    }
}

void tst_CommandRegistry::caseInsensitiveLookup()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec{QStringLiteral("GetArea"), {}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(l_spec, [](akashi::CommandContext &) {}));
    QVERIFY(l_registry.contains("getarea"));
    QVERIFY(l_registry.contains("GETAREA"));
}

void tst_CommandRegistry::duplicateRegistrationFails()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec{QStringLiteral("test"), {}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(l_spec, [](akashi::CommandContext &) {}));
    QVERIFY(!l_registry.registerCommand(l_spec, [](akashi::CommandContext &) {}));
}

void tst_CommandRegistry::collidingAliasRegistrationFails()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_first{QStringLiteral("kick"), {QStringLiteral("k")}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(l_first, [](akashi::CommandContext &) {}));

    // A new command whose name shadows an existing alias is refused.
    akashi::CommandSpec l_name_clash{QStringLiteral("k"), {}, {}, 0, {}, {}};
    QVERIFY(!l_registry.registerCommand(l_name_clash, [](akashi::CommandContext &) {}));

    // So is one whose alias shadows an existing command name.
    akashi::CommandSpec l_alias_clash{QStringLiteral("boot"), {QStringLiteral("kick")}, {}, 0, {}, {}};
    QVERIFY(!l_registry.registerCommand(l_alias_clash, [](akashi::CommandContext &) {}));

    // The refusal leaves nothing behind - not even the fresh name.
    QVERIFY(!l_registry.contains("boot"));
    QCOMPARE(l_registry.spec("k")->name, QStringLiteral("kick"));
}

void tst_CommandRegistry::unregisterByOwner()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec{QStringLiteral("test"), {QStringLiteral("t")}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(
        l_spec, [](akashi::CommandContext &) {}, QStringLiteral("plugin_a")));
    QVERIFY(l_registry.contains("test"));
    QVERIFY(l_registry.contains("t"));

    l_registry.unregisterAll(QStringLiteral("plugin_a"));
    QVERIFY(!l_registry.contains("test"));
    QVERIFY(!l_registry.contains("t"));
}

void tst_CommandRegistry::unregisterUnknownOwnerIsANoOp()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec{QStringLiteral("test"), {QStringLiteral("t")}, {}, 0, {}, {}};
    QVERIFY(l_registry.registerCommand(
        l_spec, [](akashi::CommandContext &) {}, QStringLiteral("plugin_a")));
    QVERIFY(l_registry.registerCommand(makeVariantSpec(QStringLiteral("listperms")), QStringLiteral("plugin_a")));

    l_registry.unregisterAll(QStringLiteral("nobody"));

    QVERIFY(l_registry.contains("test"));
    QVERIFY(l_registry.contains("t"));
    QCOMPARE(l_registry.spec("listperms")->variants.size(), 2);
}

void tst_CommandRegistry::containsCheck()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(!l_registry.contains("nonexistent"));
}

void tst_CommandRegistry::commandNamesList()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("alpha"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});
    l_registry.registerCommand({QStringLiteral("beta"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    QStringList l_names = l_registry.commandNames();
    QCOMPARE(l_names.size(), 2);
    QVERIFY(l_names.contains("alpha"));
    QVERIFY(l_names.contains("beta"));
}

void tst_CommandRegistry::aclRoleStringCanPerform()
{
    ACLRole l_role;
    l_role.setPermission(QStringLiteral("kick"), true);
    l_role.setPermission(QStringLiteral("ban"), true);

    QVERIFY(l_role.canPerform(QStringLiteral("kick")));
    QVERIFY(l_role.canPerform(QStringLiteral("ban")));
    QVERIFY(!l_role.canPerform(QStringLiteral("mute")));
    QVERIFY(l_role.canPerform(QStringLiteral("none")));
    QVERIFY(l_role.canPerform(QStringLiteral("")));
}

void tst_CommandRegistry::aclRoleSuperWildcard()
{
    ACLRole l_role;
    l_role.setPermission(QStringLiteral("super"), true);

    QVERIFY(l_role.canPerform(QStringLiteral("kick")));
    QVERIFY(l_role.canPerform(QStringLiteral("ban")));
    QVERIFY(l_role.canPerform(QStringLiteral("anything")));
}

void tst_CommandRegistry::aclRoleEmptyIsNone()
{
    ACLRole l_role;
    QVERIFY(l_role.canPerform(QStringLiteral("none")));
    QVERIFY(l_role.canPerform(QStringLiteral("")));
    QVERIFY(!l_role.canPerform(QStringLiteral("kick")));
}

void tst_CommandRegistry::applyExtensionsAddsAliases()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("roll"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"roll": {"aliases": "r dice"}})");
    l_file.close();

    l_registry.applyExtensions(l_path);

    QVERIFY(l_registry.contains("r"));
    QVERIFY(l_registry.contains("dice"));
    auto l_spec = l_registry.spec("r");
    QVERIFY(l_spec.has_value());
    QCOMPARE(l_spec->name, QStringLiteral("roll"));
}

void tst_CommandRegistry::applyExtensionsReportsAndIgnoresPermissionOverrides()
{
    // Overrides of a command's requirements are gone. An old file that
    // still carries one must not change any gate - and must say so out
    // loud, since the override may have been tightening the command.
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("kick"), {}, {QStringLiteral("kick")}, 0, {}, {}}, [](akashi::CommandContext &) {});
    QVERIFY(l_registry.registerCommand(makeVariantSpec(QStringLiteral("listperms")), QStringLiteral("core")));

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"kick": {"permissions": "super", "aliases": "boot"}, "listperms.user": {"permissions": "super"}})");
    l_file.close();

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("kick.*no longer supported"));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("listperms\\.user.*no longer supported"));
    l_registry.applyExtensions(l_path);

    // Every gate is what the command declared.
    QCOMPARE(l_registry.spec("kick")->gate.permissions(), QStringList{QStringLiteral("kick")});
    const auto l_listperms = l_registry.spec("listperms");
    QCOMPARE(l_listperms->match(1)->gate.permissions(), QStringList{QStringLiteral("modify_users")});
    // The aliases in the same entry still apply.
    QVERIFY(l_registry.contains(QStringLiteral("boot")));
}

void tst_CommandRegistry::requirementGroupsExpressAnd()
{
    // A requirement is any-of over all-of groups; the dispatcher and
    // canUse share the same answer.
    const auto l_holding = [](const QStringList &f_held) {
        return [f_held](const QString &f_permission) { return f_held.contains(f_permission); };
    };
    akashi::Gate l_gate = akashi::Gate::allOf({QStringLiteral("gamemaster"), QStringLiteral("kick")});
    l_gate.groups.append(QStringList{QStringLiteral("super")});
    QVERIFY(l_gate.passes(l_holding({QStringLiteral("gamemaster"), QStringLiteral("kick")})));
    QVERIFY(l_gate.passes(l_holding({QStringLiteral("super")})));
    QVERIFY(!l_gate.passes(l_holding({QStringLiteral("gamemaster")})));
    QVERIFY(!l_gate.passes(l_holding({QStringLiteral("kick")})));
    QCOMPARE(l_gate.permissions(), (QStringList{QStringLiteral("gamemaster"), QStringLiteral("kick"), QStringLiteral("super")}));

    // One name is a group of one, so the ordinary any-of case reads as a
    // plain list and decides the same way.
    const akashi::Gate l_any{QStringLiteral("kick"), QStringLiteral("ban")};
    QVERIFY(l_any.passes(l_holding({QStringLiteral("kick")})));
    QVERIFY(l_any.passes(l_holding({QStringLiteral("ban")})));
    QVERIFY(!l_any.passes(l_holding({QStringLiteral("gamemaster")})));

    // An empty gate is an open command.
    QVERIFY(akashi::Gate().passes(l_holding({})));
    QCOMPARE(akashi::Gate().describe(), QStringLiteral("nothing"));

    // A variant carrying a group gates canUse the same way.
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_spec;
    l_spec.name = QStringLiteral("gated");
    l_spec.variants = {
        {.id = QStringLiteral("own"), .min_args = 1, .max_args = 1, .gate = {QStringLiteral("gamemaster")}, .handler = [](akashi::CommandContext &) {}},
        {.id = QStringLiteral("cross"), .min_args = 2, .max_args = -1, .gate = akashi::Gate::allOf({QStringLiteral("gamemaster"), QStringLiteral("kick")}), .handler = [](akashi::CommandContext &) {}},
    };
    QVERIFY(l_registry.registerCommand(l_spec, QStringLiteral("core")));
    QVERIFY(l_registry.canUse(QStringLiteral("gated"), l_holding({QStringLiteral("gamemaster")})));
    QVERIFY(!l_registry.canUse(QStringLiteral("gated"), l_holding({QStringLiteral("kick")})));
}

void tst_CommandRegistry::applyExtensionsSkipsUnknownCommand()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("kick"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"nonexistent": {"aliases": "nx"}})");
    l_file.close();

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unknown command"));
    l_registry.applyExtensions(l_path);

    QVERIFY(!l_registry.contains("nx"));
}

void tst_CommandRegistry::applyExtensionsSkipsCollidingAlias()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("kick"), {QStringLiteral("k")}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});
    l_registry.registerCommand({QStringLiteral("ban"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"ban": {"aliases": "k fresh"}})");
    l_file.close();

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("collides"));
    l_registry.applyExtensions(l_path);

    QVERIFY(l_registry.contains("fresh"));
    QCOMPARE(l_registry.spec("fresh")->name, QStringLiteral("ban"));
    auto l_k_spec = l_registry.spec("k");
    QVERIFY(l_k_spec.has_value());
    QCOMPARE(l_k_spec->name, QStringLiteral("kick"));
}

void tst_CommandRegistry::applyExtensionsMissingFileIsNoop()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("test"), {}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    l_registry.applyExtensions(QStringLiteral("/nonexistent/path/ext.json"));

    QVERIFY(l_registry.contains("test"));
}

void tst_CommandRegistry::applyExtensionsSilentlySkipsSameCommandAlias()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand({QStringLiteral("roll"), {QStringLiteral("r")}, {}, 0, {}, {}}, [](akashi::CommandContext &) {});

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"roll": {"aliases": "r dice"}})");
    l_file.close();

    l_registry.applyExtensions(l_path);

    QVERIFY(l_registry.contains("r"));
    QVERIFY(l_registry.contains("dice"));
    QCOMPARE(l_registry.spec("r")->name, QStringLiteral("roll"));
    QCOMPARE(l_registry.spec("dice")->name, QStringLiteral("roll"));
}

void tst_CommandRegistry::canUseMirrorsTheDispatchGate()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_open{QStringLiteral("about"), {}, {}, 0, {}, {}};
    akashi::CommandSpec l_gated{QStringLiteral("ban"), {QStringLiteral("b")}, {QStringLiteral("ban")}, 3, {}, {}};
    QVERIFY(l_registry.registerCommand(l_open, [](akashi::CommandContext &) {}));
    QVERIFY(l_registry.registerCommand(l_gated, [](akashi::CommandContext &) {}));

    const auto l_none = [](const QString &) { return false; };
    const auto l_mod = [](const QString &f_permission) { return f_permission == QStringLiteral("ban"); };

    // An empty permission list is open, exactly like the dispatch gate.
    QVERIFY(l_registry.canUse("about", l_none));
    QVERIFY(!l_registry.canUse("ban", l_none));
    QVERIFY(l_registry.canUse("ban", l_mod));
    // Aliases resolve like every other lookup; unknown commands never pass.
    QVERIFY(l_registry.canUse("b", l_mod));
    QVERIFY(!l_registry.canUse("nosuch", l_mod));
}

void tst_CommandRegistry::canUseChecksEveryVariantForm()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(makeVariantSpec(QStringLiteral("listperms")), QStringLiteral("core")));

    // The free self form opens the command even without modify_users.
    QVERIFY(l_registry.canUse("listperms", [](const QString &) { return false; }));

    const akashi::CommandSpec l_all_gated{
        .name = QStringLiteral("uncm"),
        .variants = {
            {QStringLiteral("own"), 0, 0, {QStringLiteral("gamemaster")}, {}, {}, [](akashi::CommandContext &) {}},
            {QStringLiteral("other"), 1, -1, {QStringLiteral("remove_gamemaster")}, {}, {}, [](akashi::CommandContext &) {}},
        },
    };
    QVERIFY(l_registry.registerCommand(l_all_gated, QStringLiteral("core")));

    QVERIFY(!l_registry.canUse("uncm", [](const QString &) { return false; }));
    // Any one passing form is enough.
    QVERIFY(l_registry.canUse("uncm", [](const QString &f_permission) { return f_permission == QStringLiteral("remove_gamemaster"); }));
}

void tst_CommandRegistry::variantMatchPicksByArgCount()
{
    const akashi::CommandSpec l_spec = makeVariantSpec(QStringLiteral("listperms"));

    // First matching form in declaration order wins.
    QCOMPARE(l_spec.match(0)->id, QStringLiteral("own"));
    QCOMPARE(l_spec.match(1)->id, QStringLiteral("user"));
    QCOMPARE(l_spec.match(5)->id, QStringLiteral("user"));

    // Bounded windows reject counts outside them.
    const akashi::CommandSpec l_bounded{
        .variants = {
            {QStringLiteral("own"), 1, 1, {}, {}, {}, [](akashi::CommandContext &) {}},
            {QStringLiteral("user"), 2, 2, {}, {}, {}, [](akashi::CommandContext &) {}},
        },
    };
    QCOMPARE(l_bounded.match(0), nullptr);
    QCOMPARE(l_bounded.match(1)->id, QStringLiteral("own"));
    QCOMPARE(l_bounded.match(2)->id, QStringLiteral("user"));
    QCOMPARE(l_bounded.match(3), nullptr);
}

void tst_CommandRegistry::registerCommandValidatesVariants()
{
    akashi::CommandRegistry l_registry;

    // The handlerless overload needs at least one variant.
    const akashi::CommandSpec l_empty{.name = QStringLiteral("empty")};
    QVERIFY(!l_registry.registerCommand(l_empty, QStringLiteral("core")));

    // Every variant needs an id and a handler.
    akashi::CommandSpec l_no_id = makeVariantSpec(QStringLiteral("noid"));
    l_no_id.variants[0].id.clear();
    QVERIFY(!l_registry.registerCommand(l_no_id, QStringLiteral("core")));

    akashi::CommandSpec l_no_handler = makeVariantSpec(QStringLiteral("nohandler"));
    l_no_handler.variants[1].handler = {};
    QVERIFY(!l_registry.registerCommand(l_no_handler, QStringLiteral("core")));

    QVERIFY(l_registry.registerCommand(makeVariantSpec(QStringLiteral("listperms")), QStringLiteral("core")));
    QCOMPARE(l_registry.spec("listperms")->variants.size(), 2);
}

} // namespace unittests
} // namespace tests

QTEST_MAIN(tests::unittests::tst_CommandRegistry)
#include "tst_command_registry.moc"
