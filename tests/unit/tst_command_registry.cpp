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

    void registerCommandValidatesVariants();
};

static void noop(akashi::CommandContext &, const akashi::Args &) {}

// A one-form command, open or behind the given gate.
static akashi::CommandSpec gatedSpec(const QString &f_name, const QStringList &f_aliases = {}, const akashi::Gate &f_gate = {})
{
    akashi::CommandSpec l_spec;
    l_spec.name = f_name;
    l_spec.aliases = f_aliases;
    l_spec.variants = {{.id = QStringLiteral("run"), .gate = f_gate, .run = noop}};
    return l_spec;
}

// A two-form command like /listperms: a free self form and a gated form
// that names someone.
static akashi::CommandSpec makeVariantSpec(const QString &f_name)
{
    akashi::CommandSpec l_spec;
    l_spec.name = f_name;
    l_spec.variants = {
        {.id = QStringLiteral("own"), .run = noop},
        {.id = QStringLiteral("user"), .gate = {QStringLiteral("modify_users")}, .params = {akashi::param::token(QStringLiteral("target"))}, .run = noop},
    };
    return l_spec;
}

void tst_CommandRegistry::registerAndLookup()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("test")), QStringLiteral("core")));
    QVERIFY(l_registry.contains("test"));

    if (auto l_found = l_registry.spec("test")) {
        QCOMPARE(l_found->name, QStringLiteral("test"));
        // The stored definition carries its generated usage.
        QCOMPARE(l_found->usage, QStringLiteral("/test"));
    }
    else {
        QFAIL("spec not found");
    }
}

// Which definition the dispatcher would run for the name, told apart by
// its description; empty when nothing would run.
static QString dispatchedDescription(const akashi::CommandRegistry &f_registry, const QString &f_name)
{
    const auto l_spec = f_registry.spec(f_name);
    return l_spec ? l_spec->description : QString();
}

void tst_CommandRegistry::registerRefusesATakenSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("roll"), {QStringLiteral("dice")}), QStringLiteral("core")));

    // The name, or an alias of it, is a take-over: that is replace().
    QVERIFY(!l_registry.registerCommand(gatedSpec(QStringLiteral("roll")), QStringLiteral("dice-plus")));
    QVERIFY(!l_registry.registerCommand(gatedSpec(QStringLiteral("dice")), QStringLiteral("dice-plus")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("core"));
}

void tst_CommandRegistry::replaceTakesOverAndDropsThePrevious()
{
    akashi::CommandRegistry l_registry;
    akashi::CommandSpec l_core = gatedSpec(QStringLiteral("roll"), {QStringLiteral("dice")});
    l_core.description = QStringLiteral("core roll");
    QVERIFY(l_registry.registerCommand(l_core, QStringLiteral("core")));

    akashi::CommandSpec l_plugin = gatedSpec(QStringLiteral("roll"), {QStringLiteral("rr")});
    l_plugin.description = QStringLiteral("plugin roll");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("dice-plus.*replaced the core command /roll"));
    QVERIFY(l_registry.replace(l_plugin, QStringLiteral("dice-plus")));

    // The plugin's definition answers now, and says whose it replaced.
    QCOMPARE(dispatchedDescription(l_registry, QStringLiteral("roll")), QStringLiteral("plugin roll"));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("dice-plus"));
    QCOMPARE(l_registry.replacedOwnerOf(QStringLiteral("roll")), QStringLiteral("core"));
    // Core's own alias left with core's definition; the plugin's arrived.
    QVERIFY(!l_registry.contains(QStringLiteral("dice")));
    QVERIFY(l_registry.contains(QStringLiteral("rr")));

    // Last wins: a later replace takes it again, quietly between plugins.
    akashi::CommandSpec l_later = gatedSpec(QStringLiteral("roll"));
    l_later.description = QStringLiteral("later roll");
    QVERIFY(l_registry.replace(l_later, QStringLiteral("dice-pro")));
    QCOMPARE(dispatchedDescription(l_registry, QStringLiteral("roll")), QStringLiteral("later roll"));
    QCOMPARE(l_registry.replacedOwnerOf(QStringLiteral("roll")), QStringLiteral("dice-plus"));
    QVERIFY(!l_registry.contains(QStringLiteral("rr")));
}

void tst_CommandRegistry::replaceKeepsTheOperatorsAliases()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("roll")), QStringLiteral("core")));

    QTemporaryDir l_dir;
    QVERIFY(l_dir.isValid());
    const QString l_path = l_dir.filePath(QStringLiteral("ext.json"));
    QFile l_file(l_path);
    QVERIFY(l_file.open(QIODevice::WriteOnly));
    l_file.write(R"({"roll": {"aliases": "r"}})");
    l_file.close();
    l_registry.applyExtensions(l_path);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("replaced the core command /roll"));
    QVERIFY(l_registry.replace(gatedSpec(QStringLiteral("roll")), QStringLiteral("dice-plus")));

    // The operator's alias belongs to the slot, so it reaches the new occupant.
    QVERIFY(l_registry.contains(QStringLiteral("r")));
    QCOMPARE(l_registry.spec(QStringLiteral("r"))->name, QStringLiteral("roll"));
    QVERIFY(l_registry.spec(QStringLiteral("roll"))->aliases.contains(QStringLiteral("r")));
}

void tst_CommandRegistry::replaceRefusesAnAliasIntoAnotherSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("roll")), QStringLiteral("core")));
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("kick"), {QStringLiteral("k")}), QStringLiteral("core")));

    // One replace may not take a second command through an alias, nor
    // replace a command by naming another's alias.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not replace /roll"));
    QVERIFY(!l_registry.replace(gatedSpec(QStringLiteral("roll"), {QStringLiteral("kick")}), QStringLiteral("p")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("could not replace /k"));
    QVERIFY(!l_registry.replace(gatedSpec(QStringLiteral("k")), QStringLiteral("p")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("roll")), QStringLiteral("core"));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("kick")), QStringLiteral("core"));
}

void tst_CommandRegistry::vacateEmptiesTheSlot()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("8ball"), {QStringLiteral("eightball")}), QStringLiteral("core")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no-fun vacated /8ball"));
    QVERIFY(l_registry.vacate(QStringLiteral("8ball"), QStringLiteral("no-fun")));

    QVERIFY(!l_registry.contains(QStringLiteral("8ball")));
    QVERIFY(!l_registry.contains(QStringLiteral("eightball")));
    QVERIFY(!l_registry.spec(QStringLiteral("8ball")));
    QVERIFY(!l_registry.commandNames().contains(QStringLiteral("8ball")));
    QCOMPARE(l_registry.vacatedSlots().value(QStringLiteral("8ball")), QStringLiteral("no-fun"));
    // An empty slot cannot be emptied again.
    QVERIFY(!l_registry.vacate(QStringLiteral("8ball"), QStringLiteral("no-fun")));

    // It stays empty until something registers it.
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("8ball")), QStringLiteral("fortune")));
    QCOMPARE(l_registry.ownerOf(QStringLiteral("8ball")), QStringLiteral("fortune"));
    QVERIFY(l_registry.vacatedSlots().isEmpty());
}

void tst_CommandRegistry::unloadLeavesATakenSlotEmpty()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("roll")), QStringLiteral("core")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("replaced the core command /roll"));
    QVERIFY(l_registry.replace(gatedSpec(QStringLiteral("roll")), QStringLiteral("dice-plus")));

    // The plugin unloads: nothing is restored, and the hole is reported.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("dice-plus.*left /roll empty"));
    l_registry.unregisterAll(QStringLiteral("dice-plus"));
    QVERIFY(!l_registry.contains(QStringLiteral("roll")));
    QCOMPARE(l_registry.vacatedSlots().value(QStringLiteral("roll")), QStringLiteral("dice-plus"));
}

void tst_CommandRegistry::unloadOfAPluginsOwnCommandIsQuiet()
{
    akashi::CommandRegistry l_registry;
    QVERIFY(l_registry.registerCommand(gatedSpec(QStringLiteral("slowmode")), QStringLiteral("slowmode")));

    // Its own command leaving with it is ordinary: no warning, no hole.
    QTest::failOnWarning(QRegularExpression("left /slowmode empty"));
    l_registry.unregisterAll(QStringLiteral("slowmode"));
    QVERIFY(!l_registry.contains(QStringLiteral("slowmode")));
    QVERIFY(l_registry.vacatedSlots().isEmpty());
}

void tst_CommandRegistry::lookupByAlias()
{
    akashi::CommandRegistry l_registry;
    const akashi::CommandSpec l_spec = gatedSpec(QStringLiteral("getarea"), {QStringLiteral("ga")});
    QVERIFY(l_registry.registerCommand(l_spec, QStringLiteral("core")));

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
    const akashi::CommandSpec l_spec = gatedSpec(QStringLiteral("GetArea"));
    QVERIFY(l_registry.registerCommand(l_spec, QStringLiteral("core")));
    QVERIFY(l_registry.contains("getarea"));
    QVERIFY(l_registry.contains("GETAREA"));
}

void tst_CommandRegistry::duplicateRegistrationFails()
{
    akashi::CommandRegistry l_registry;
    const akashi::CommandSpec l_spec = gatedSpec(QStringLiteral("test"));
    QVERIFY(l_registry.registerCommand(l_spec, QStringLiteral("core")));
    QVERIFY(!l_registry.registerCommand(l_spec, QStringLiteral("core")));
}

void tst_CommandRegistry::collidingAliasRegistrationFails()
{
    akashi::CommandRegistry l_registry;
    const akashi::CommandSpec l_first = gatedSpec(QStringLiteral("kick"), {QStringLiteral("k")});
    QVERIFY(l_registry.registerCommand(l_first, QStringLiteral("core")));

    // A new command whose name shadows an existing alias is refused.
    const akashi::CommandSpec l_name_clash = gatedSpec(QStringLiteral("k"));
    QVERIFY(!l_registry.registerCommand(l_name_clash, QStringLiteral("core")));

    // So is one whose alias shadows an existing command name.
    const akashi::CommandSpec l_alias_clash = gatedSpec(QStringLiteral("boot"), {QStringLiteral("kick")});
    QVERIFY(!l_registry.registerCommand(l_alias_clash, QStringLiteral("core")));

    // The refusal leaves nothing behind - not even the fresh name.
    QVERIFY(!l_registry.contains("boot"));
    QCOMPARE(l_registry.spec("k")->name, QStringLiteral("kick"));
}

void tst_CommandRegistry::unregisterByOwner()
{
    akashi::CommandRegistry l_registry;
    const akashi::CommandSpec l_spec = gatedSpec(QStringLiteral("test"), {QStringLiteral("t")});
    QVERIFY(l_registry.registerCommand(
        l_spec, QStringLiteral("plugin_a")));
    QVERIFY(l_registry.contains("test"));
    QVERIFY(l_registry.contains("t"));

    l_registry.unregisterAll(QStringLiteral("plugin_a"));
    QVERIFY(!l_registry.contains("test"));
    QVERIFY(!l_registry.contains("t"));
}

void tst_CommandRegistry::unregisterUnknownOwnerIsANoOp()
{
    akashi::CommandRegistry l_registry;
    const akashi::CommandSpec l_spec = gatedSpec(QStringLiteral("test"), {QStringLiteral("t")});
    QVERIFY(l_registry.registerCommand(
        l_spec, QStringLiteral("plugin_a")));
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
    l_registry.registerCommand(gatedSpec(QStringLiteral("alpha")), QStringLiteral("core"));
    l_registry.registerCommand(gatedSpec(QStringLiteral("beta")), QStringLiteral("core"));

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
    l_registry.registerCommand(gatedSpec(QStringLiteral("roll")), QStringLiteral("core"));

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
    l_registry.registerCommand(gatedSpec(QStringLiteral("kick"), {}, {QStringLiteral("kick")}), QStringLiteral("core"));
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
    QCOMPARE(l_registry.spec("kick")->variants.at(0).gate.permissions(), QStringList{QStringLiteral("kick")});
    const auto l_listperms = l_registry.spec("listperms");
    QCOMPARE(l_listperms->variants.at(1).gate.permissions(), QStringList{QStringLiteral("modify_users")});
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
        {.id = QStringLiteral("own"), .gate = {QStringLiteral("gamemaster")}, .params = {akashi::param::token(QStringLiteral("target"))}, .run = noop},
        {.id = QStringLiteral("cross"), .gate = akashi::Gate::allOf({QStringLiteral("gamemaster"), QStringLiteral("kick")}), .params = {akashi::param::token(QStringLiteral("target")), akashi::param::token(QStringLiteral("area"))}, .run = noop},
    };
    QVERIFY(l_registry.registerCommand(l_spec, QStringLiteral("core")));
    QVERIFY(l_registry.canUse(QStringLiteral("gated"), l_holding({QStringLiteral("gamemaster")})));
    QVERIFY(!l_registry.canUse(QStringLiteral("gated"), l_holding({QStringLiteral("kick")})));
}

void tst_CommandRegistry::applyExtensionsSkipsUnknownCommand()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand(gatedSpec(QStringLiteral("kick")), QStringLiteral("core"));

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
    l_registry.registerCommand(gatedSpec(QStringLiteral("kick"), {QStringLiteral("k")}), QStringLiteral("core"));
    l_registry.registerCommand(gatedSpec(QStringLiteral("ban")), QStringLiteral("core"));

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
    l_registry.registerCommand(gatedSpec(QStringLiteral("test")), QStringLiteral("core"));

    l_registry.applyExtensions(QStringLiteral("/nonexistent/path/ext.json"));

    QVERIFY(l_registry.contains("test"));
}

void tst_CommandRegistry::applyExtensionsSilentlySkipsSameCommandAlias()
{
    akashi::CommandRegistry l_registry;
    l_registry.registerCommand(gatedSpec(QStringLiteral("roll"), {QStringLiteral("r")}), QStringLiteral("core"));

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
    const akashi::CommandSpec l_open = gatedSpec(QStringLiteral("about"));
    const akashi::CommandSpec l_gated = gatedSpec(QStringLiteral("ban"), {QStringLiteral("b")}, {QStringLiteral("ban")});
    QVERIFY(l_registry.registerCommand(l_open, QStringLiteral("core")));
    QVERIFY(l_registry.registerCommand(l_gated, QStringLiteral("core")));

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
            {.id = QStringLiteral("own"), .gate = {QStringLiteral("gamemaster")}, .run = noop},
            {.id = QStringLiteral("other"), .gate = {QStringLiteral("remove_gamemaster")}, .params = {akashi::param::token(QStringLiteral("target"))}, .run = noop},
        },
    };
    QVERIFY(l_registry.registerCommand(l_all_gated, QStringLiteral("core")));

    QVERIFY(!l_registry.canUse("uncm", [](const QString &) { return false; }));
    // Any one passing form is enough.
    QVERIFY(l_registry.canUse("uncm", [](const QString &f_permission) { return f_permission == QStringLiteral("remove_gamemaster"); }));
}

void tst_CommandRegistry::registerCommandValidatesVariants()
{
    akashi::CommandRegistry l_registry;

    // A command needs at least one form.
    const akashi::CommandSpec l_empty{.name = QStringLiteral("empty")};
    QVERIFY(!l_registry.registerCommand(l_empty, QStringLiteral("core")));

    // Every form needs an id and a body, and parameters validParams accepts.
    akashi::CommandSpec l_no_id = makeVariantSpec(QStringLiteral("noid"));
    l_no_id.variants[0].id.clear();
    QVERIFY(!l_registry.registerCommand(l_no_id, QStringLiteral("core")));

    akashi::CommandSpec l_no_handler = makeVariantSpec(QStringLiteral("nohandler"));
    l_no_handler.variants[1].run = {};
    QVERIFY(!l_registry.registerCommand(l_no_handler, QStringLiteral("core")));

    akashi::CommandSpec l_bad_params = makeVariantSpec(QStringLiteral("badparams"));
    l_bad_params.variants[1].params.prepend(akashi::param::text(QStringLiteral("rest")));
    QVERIFY(!l_registry.registerCommand(l_bad_params, QStringLiteral("core")));

    QVERIFY(l_registry.registerCommand(makeVariantSpec(QStringLiteral("listperms")), QStringLiteral("core")));
    QCOMPARE(l_registry.spec("listperms")->variants.size(), 2);
}

} // namespace unittests
} // namespace tests

QTEST_MAIN(tests::unittests::tst_CommandRegistry)
#include "tst_command_registry.moc"
