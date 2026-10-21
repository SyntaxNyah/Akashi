#include "commands/moderation_commands.h"

#include "akashi/helpers/parse.h"
#include "akashi/scheduler.h"
#include "akashi/service_registry.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/db_manager.h"
#include "core/permission_registry.h"
#include "core/plugin_manager.h"
#include "core/server_context.h"
#include "core/server_settings.h"
#include "world/area.h"
#include "world/floor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace akashi::commands {

static QString reprimand(ServerContext *f_server, bool f_positive = false)
{
    if (f_positive) {
        return f_server->praiseList().at(CommandContext::genRand(0, f_server->praiseList().size() - 1));
    }
    else {
        return f_server->reprimandsList().at(CommandContext::genRand(0, f_server->reprimandsList().size() - 1));
    }
}

static void sendNotice(CommandContext &f_context, const QString &f_notice, bool f_global = false)
{
    QString l_message = "A moderator sent this ";
    if (f_global)
        l_message += "server-wide ";
    l_message += "notice:\n\n" + f_notice;
    f_context.replyToArea(l_message);
    akashi::Packet l_packet("BB", {l_message});
    if (f_global)
        f_context.server()->broadcast(l_packet);
    else
        f_context.server()->broadcast(l_packet, f_context.areaId());
}

// The /ban door: parses the duration and phrases the replies; the act
// itself is the server's ban verb. This door's KB carries the ban id and
// the until text.
void cmdBan(CommandContext &f_context, const Args &f_args)
{
    const QString l_args_str = f_args.text(QStringLiteral("reason"));
    // The "perma" form gives the word, the other a checked duration.
    const long long l_duration_seconds = f_args.word(QStringLiteral("duration")) == QLatin1String("perma")
                                             ? -2
                                             : f_args.duration(QStringLiteral("duration"));

    QString l_moderator;
    switch (f_context.server()->authType()) {
    case AuthType::SIMPLE:
        l_moderator = "moderator";
        break;
    case AuthType::ADVANCED:
        l_moderator = f_context.moderatorName();
        break;
    }

    const QString l_ipid = f_args.token(QStringLiteral("ipid"));
    const auto l_result = f_context.server()->banPlayers(
        l_ipid, l_duration_seconds, l_args_str, l_moderator, QStringLiteral("Permanently."),
        [&l_args_str](int f_ban_id, const QString &f_until) {
            return l_args_str + "\nID: " + QString::number(f_ban_id) + "\nUntil: " + f_until;
        });

    if (l_result.clients_kicked == 0) {
        f_context.reply("Banned " + l_ipid + " for reason: " + l_args_str);
        return;
    }
    f_context.reply("Banned user with ipid " + l_ipid + " for reason: " + l_args_str);
    if (l_result.clients_kicked > 1)
        f_context.reply("Kicked " + QString::number(l_result.clients_kicked) + " clients with matching ipids.");
}

// The /kick door: everyone on the IPID; the act itself is the server's
// kick verb.
void cmdKick(CommandContext &f_context, const Args &f_args)
{
    const QString l_target_ipid = f_args.token(QStringLiteral("ipid"));
    const QString l_reason = f_args.text(QStringLiteral("reason"));

    const QList<akashi::ClientSession *> l_targets = f_context.server()->clientsByIpid(l_target_ipid);
    if (l_targets.isEmpty()) {
        f_context.reply("User with ipid not found!");
        return;
    }
    const QString l_moderator = f_context.server()->authType() == AuthType::ADVANCED
                                    ? f_context.moderatorName()
                                    : QStringLiteral("Moderator");
    f_context.server()->kickClients(l_targets, l_target_ipid, l_reason, l_moderator);
    f_context.reply("Kicked " + QString::number(l_targets.size()) + " client(s) with ipid " + l_target_ipid + " for reason: " + l_reason);
}

void cmdMods(CommandContext &f_context, const Args &)
{
    // Everyone may see who is around to ask for help; the where-and-as-
    // whom detail is a staff radar that lets an abuser time their
    // behavior to where staff aren't, so it stays with staff.
    const bool l_staff_radar = f_context.canPerform(akashi::permission::see_staff_presence);
    QStringList l_entries;
    int l_online_count = 0;
    const QVector<akashi::ClientSession *> l_clients = f_context.server()->clients();
    for (akashi::ClientSession *l_client : l_clients) {
        if (l_client->isAuthenticated()) {
            l_entries << "---";
            if (f_context.server()->authType() != AuthType::SIMPLE) {
                l_entries << "Moderator: " + l_client->moderatorName();
            }
            l_entries << "OOC name: " + l_client->name();
            l_entries << "ID: " + QString::number(l_client->clientId());
            if (l_staff_radar) {
                if (f_context.server()->authType() != AuthType::SIMPLE) {
                    l_entries << "Role:" << l_client->aclRoleId();
                }
                l_entries << "Area: " + QString::number(l_client->areaId());
                l_entries << "Character: " + l_client->character();
            }
            l_online_count++;
        }
    }
    l_entries << "---";
    l_entries << "Total online: " << QString::number(l_online_count);
    f_context.reply(l_entries.join("\n"));
}

// The caller's permissions, asked the way the dispatch gate asks.
static std::function<bool(const QString &)> callerPermissions(CommandContext &f_context)
{
    return [&f_context](const QString &f_permission) {
        return f_context.canPerform(f_permission);
    };
}

// The targets a command may not act on, as declared on its spec or on the
// client parameters of its forms - once each, for /help and /why.
static QStringList immunityLines(const CommandSpec &f_spec)
{
    QStringList l_lines;
    if (!f_spec.target_immune_if.isEmpty())
        l_lines << "Cannot target holders of " + f_spec.target_immune_if + ".";
    for (const CommandVariant &l_variant : f_spec.variants) {
        for (const Param &l_param : l_variant.params) {
            if (l_param.isImmuneIfAreaOwner())
                l_lines << QStringLiteral("Cannot target the CMs of this area.");
            if (!l_param.immunePermission().isEmpty())
                l_lines << "Cannot target holders of " + l_param.immunePermission() + ".";
        }
    }
    l_lines.removeDuplicates();
    return l_lines;
}

// Who provides a command, when it is not core's own - a plugin that
// replaced a command changes what it does and needs, so it says so.
static QString providedBy(CommandRegistry *f_registry, const QString &f_name)
{
    const QString l_owner = f_registry->ownerOf(f_name);
    if (l_owner.isEmpty() || l_owner == QStringLiteral("core"))
        return {};
    const QString l_replaced = f_registry->replacedOwnerOf(f_name);
    if (l_replaced.isEmpty())
        return "Provided by " + l_owner + ".";
    return "Provided by " + l_owner + ", in place of " + l_replaced + "'s version.";
}

// The areas where a command's gate resolves for this caller, capped for
// readability, with the overflow counted instead of hidden.
static QString areasWhereUsable(CommandContext &f_context, CommandRegistry *f_registry, const QString &f_name)
{
    QStringList l_open;
    int l_open_count = 0;
    const int l_area_count = f_context.server()->areaCount();
    for (int i = 0; i < l_area_count; i++) {
        if (i == f_context.areaId())
            continue;
        if (f_registry->canUse(f_name, [&f_context, i](const QString &f_permission) { return f_context.canPerformIn(f_permission, i); })) {
            l_open_count++;
            if (l_open.size() < 3)
                l_open << f_context.server()->areaName(i);
        }
    }
    if (l_open.isEmpty())
        return {};
    QString l_text = l_open.join(", ");
    if (l_open_count > l_open.size())
        l_text += ", +" + QString::number(l_open_count - l_open.size()) + " more";
    return l_text;
}

void cmdCommands(CommandContext &f_context, const Args &)
{
    QStringList l_entries;
    l_entries << "Allowed commands:";

    // Verbs closed here but open in another area's offers are annotated,
    // never hidden - a command blinking away reads as "removed".
    QStringList l_elsewhere;
    CommandRegistry *l_registry = f_context.server()->commandRegistry();
    for (const QString &l_name : l_registry->commandNames()) {
        auto l_spec = l_registry->spec(l_name);
        if (!l_spec)
            continue;
        QString l_info = "/" + l_name;
        if (!l_spec->aliases.isEmpty()) {
            l_info += " [aka: " + l_spec->aliases.join(", ") + "]";
        }
        if (l_registry->canUse(l_name, callerPermissions(f_context))) {
            l_entries << l_info;
            continue;
        }
        const QString l_where = areasWhereUsable(f_context, l_registry, l_name);
        if (!l_where.isEmpty()) {
            l_elsewhere << l_info + " (" + l_where + ")";
        }
    }
    if (!l_elsewhere.isEmpty()) {
        l_entries << "Elsewhere on this server:" << l_elsewhere;
    }
    f_context.reply(l_entries.join("\n"));
}

void cmdHelp(CommandContext &f_context, const Args &f_args)
{
    CommandRegistry *l_registry = f_context.server()->commandRegistry();

    if (!f_args.has(QStringLiteral("command"))) {
        f_context.reply("Type /help <command> for help on a specific command, or /help all to list all commands.");
        return;
    }

    QString l_command_name = f_args.token(QStringLiteral("command")).toLower();

    auto l_check_permission = [&f_context, l_registry](const QString &f_command_name) -> bool {
        return l_registry->canUse(f_command_name, callerPermissions(f_context));
    };

    auto l_format_command = [l_registry](const QString &f_command_name) -> QString {
        auto l_spec = l_registry->spec(f_command_name);
        if (!l_spec)
            return {};
        QString l_text = "/" + l_spec->name;
        if (!l_spec->usage.isEmpty())
            l_text = l_spec->usage;
        l_text += "\n" + (l_spec->description.isEmpty() ? QString("No details available.") : l_spec->description);
        // A variant command lists each of its forms with its own gate; an
        // all-of group renders its members joined by "+".
        for (const CommandVariant &l_variant : l_spec->variants) {
            if (l_variant.usage.isEmpty())
                continue;
            l_text += "\n  " + l_variant.usage;
            if (!l_variant.description.isEmpty())
                l_text += " - " + l_variant.description;
            if (!l_variant.gate.isEmpty())
                l_text += " [" + l_variant.gate.describe() + "]";
        }
        // The declared value-dependent escalation and target immunity are
        // part of the gate, so the help shows them.
        if (!l_spec->escalates_to.isEmpty()) {
            l_text += "\nNeeds " + l_spec->escalates_to;
            if (!l_spec->escalates_when.isEmpty())
                l_text += " when " + l_spec->escalates_when;
            l_text += ".";
        }
        for (const QString &l_line : immunityLines(*l_spec))
            l_text += "\n" + l_line;
        if (const QString l_provider = providedBy(l_registry, f_command_name); !l_provider.isEmpty())
            l_text += "\n" + l_provider;
        return l_text;
    };

    QString l_message = "==Help==\n";

    if (l_command_name == "all") {
        QStringList l_entries;
        for (const QString &l_name : l_registry->commandNames()) {
            if (l_check_permission(l_name)) {
                l_entries.append(l_format_command(l_name));
            }
        }
        f_context.reply(l_message + l_entries.join("\n\n"));
        return;
    }

    if (!l_registry->contains(l_command_name)) {
        f_context.reply(l_message + "Unable to find the command " + l_command_name + ".");
        return;
    }

    if (!l_check_permission(l_command_name)) {
        // A verb closed here may resolve in another area's offers - say
        // where instead of reading as "no such power anywhere".
        QString l_refusal = l_message + "You are not allowed to use the command " + l_command_name + ".";
        const QString l_where = areasWhereUsable(f_context, l_registry, l_command_name);
        if (!l_where.isEmpty())
            l_refusal += " It works in: " + l_where + ".";
        f_context.reply(l_refusal);
        return;
    }

    f_context.reply(l_message + l_format_command(l_command_name));
}

// One contribution phrased for a player. A personal grant is reported as
// existing, never by the identity behind it - /why must not become
// alt-linkage reconnaissance.
static QString describeContribution(CommandContext &f_context, const akashi::Contribution &f_contribution)
{
    switch (f_contribution.kind) {
    case akashi::Contribution::Kind::Role:
    {
        // The keyring an area's owners wear reads as what it is rather
        // than as a role id nobody logs into.
        if (f_contribution.id == QStringLiteral("@cm"))
            return QStringLiteral("being CM in this area");
        const bool l_super = f_context.server()->aclRolesHandler()->roleById(f_contribution.id).canPerform(akashi::permission::super);
        return "the role \"" + f_contribution.id + "\"" + (l_super ? QStringLiteral(" (holds every permission)") : QString());
    }
    case akashi::Contribution::Kind::Source:
        return f_contribution.because;
    case akashi::Contribution::Kind::Grant:
        break;
    }
    const akashi::Grant &l_grant = *f_contribution.grant;
    QString l_audience;
    switch (l_grant.audience.kind) {
    case akashi::AudienceKind::Everyone:
        l_audience = QStringLiteral("everyone");
        break;
    case akashi::AudienceKind::Participants:
        l_audience = QStringLiteral("participants");
        break;
    case akashi::AudienceKind::Role:
        l_audience = QStringLiteral("role \"") + l_grant.audience.role_id + QStringLiteral("\"");
        break;
    case akashi::AudienceKind::Person:
        l_audience = QStringLiteral("a personal grant");
        break;
    }
    QString l_place;
    switch (l_grant.scope) {
    case akashi::GrantScope::Server:
        l_place = QStringLiteral("a server-scope grant");
        break;
    case akashi::GrantScope::Floor:
        l_place = QStringLiteral("this floor's offer");
        break;
    case akashi::GrantScope::Area:
        l_place = QStringLiteral("this area's offer");
        break;
    }
    return l_place + " to " + l_audience + " (" + l_grant.owner + ")";
}

// The verdict line for one permission: the decisive fact first - a mask
// outranks any grant - and, when refused here, where it does resolve.
static QString whyLine(CommandContext &f_context, akashi::ClientSession *f_subject, const QString &f_permission)
{
    const akashi::Resolution l_resolution = f_subject->explainPermission(f_permission);
    if (!l_resolution.masked_by.isEmpty()) {
        return "[x] " + f_permission + " - blocked by the \"" + l_resolution.masked_by.join("\", \"") + "\" sanction; a sanction overrides every grant.";
    }
    if (!l_resolution.contributions.isEmpty()) {
        return "[+] " + f_permission + " - granted by " + describeContribution(f_context, l_resolution.contributions.first()) + ".";
    }
    QString l_line = "[x] " + f_permission + " - no role grant and nothing offers it here.";
    QStringList l_open;
    int l_open_count = 0;
    const int l_area_count = f_context.server()->areaCount();
    for (int i = 0; i < l_area_count; i++) {
        if (i == f_subject->areaId())
            continue;
        if (f_subject->canPerformIn(f_permission, i)) {
            l_open_count++;
            if (l_open.size() < 3)
                l_open << f_context.server()->areaName(i);
        }
    }
    if (!l_open.isEmpty()) {
        l_line += " It works in: " + l_open.join(", ");
        if (l_open_count > l_open.size())
            l_line += ", +" + QString::number(l_open_count - l_open.size()) + " more";
        l_line += ".";
    }
    return l_line;
}

void cmdWhy(CommandContext &f_context, const Args &f_args)
{
    // The target form asks about another player.
    akashi::ClientSession *l_subject = f_args.has(QStringLiteral("target")) ? f_args.client(QStringLiteral("target")) : f_context.self();
    QString l_query = f_args.token(QStringLiteral("query"));
    l_query = l_query.toLower();
    if (l_query.startsWith(QLatin1Char('/')))
        l_query.remove(0, 1);

    QStringList l_lines;
    CommandRegistry *l_registry = f_context.server()->commandRegistry();
    if (l_registry->contains(l_query)) {
        // Players think in commands: resolve the binding, explain every
        // permission the gate can ask for, and surface the declared
        // escalation and immunity - the parts outside canPerform.
        const auto l_spec = l_registry->spec(l_query);
        QStringList l_permissions;
        const auto l_collect = [&l_permissions](const Gate &f_gate) {
            for (const QString &l_permission : f_gate.permissions())
                if (!l_permissions.contains(l_permission))
                    l_permissions.append(l_permission);
        };
        if (l_spec->variants.isEmpty()) {
            l_lines << "/" + l_spec->name + " needs: " + l_spec->gate.describe();
            l_collect(l_spec->gate);
        }
        else {
            for (const CommandVariant &l_variant : l_spec->variants) {
                l_lines << l_variant.usage + " needs: " + l_variant.gate.describe();
                l_collect(l_variant.gate);
            }
        }
        if (!l_spec->escalates_to.isEmpty()) {
            l_lines << "Also needs " + l_spec->escalates_to + (l_spec->escalates_when.isEmpty() ? QString() : " when " + l_spec->escalates_when) + ".";
            if (!l_permissions.contains(l_spec->escalates_to))
                l_permissions.append(l_spec->escalates_to);
        }
        l_lines << immunityLines(*l_spec);
        if (const QString l_provider = providedBy(l_registry, l_query); !l_provider.isEmpty())
            l_lines << l_provider;
        for (const QString &l_permission : std::as_const(l_permissions))
            l_lines << whyLine(f_context, l_subject, l_permission);
    }
    else if (f_context.server()->permissionRegistry()->isRegistered(l_query)) {
        l_lines << whyLine(f_context, l_subject, l_query);
    }
    else {
        f_context.reply("There is no command or permission named \"" + l_query + "\".");
        return;
    }
    f_context.reply(l_lines.join("\n"));
}

void cmdDumpGrants(CommandContext &f_context, const Args &)
{
    // One store, so the dump is one sorted list; each row says which
    // place it stands in. Behind see_ipids, so person keys print in the
    // clear like the rest of the moderation surface.
    QStringList l_lines;
    l_lines << "== Stored grants ==";
    const QList<akashi::Grant> l_grants = f_context.server()->permissionRegistry()->grants();
    for (const akashi::Grant &l_grant : l_grants)
        l_lines << "  " + akashi::describeGrant(l_grant, f_context.server()->placeName(l_grant));
    l_lines << "Roles worn here and live offers (ownership, the area's lock) resolve per query and are not listed.";
    f_context.reply(l_lines.join("\n"));
}

void cmdMotd(CommandContext &f_context, const Args &)
{
    f_context.reply("=== MOTD ===\r\n" + f_context.server()->serverSettings()->motd() + "\r\n=============");
}

void cmdSetMotd(CommandContext &f_context, const Args &f_args)
{
    QString l_motd = f_args.text(QStringLiteral("message"));
    f_context.server()->setMotd(l_motd);
    f_context.reply("MOTD has been changed.");
}

void cmdBans(CommandContext &f_context, const Args &)
{
    QStringList l_recent_bans;
    l_recent_bans << "Last 5 bans:";
    l_recent_bans << "-----";
    const QList<DBManager::BanInfo> l_bans_list = f_context.server()->databaseManager()->recentBans();
    for (const DBManager::BanInfo &l_ban : l_bans_list) {
        QString l_banned_until;
        if (l_ban.duration == -2)
            l_banned_until = "The heat death of the universe";
        else
            l_banned_until = QDateTime::fromSecsSinceEpoch(l_ban.time).addSecs(l_ban.duration).toString("MM/dd/yyyy, hh:mm");
        l_recent_bans << "Ban ID: " + QString::number(l_ban.id);
        l_recent_bans << "Affected IPID: " + l_ban.ipid;
        l_recent_bans << "Affected HDID: " + l_ban.hdid;
        l_recent_bans << "Reason for ban: " + l_ban.reason;
        l_recent_bans << "Date of ban: " + QDateTime::fromSecsSinceEpoch(l_ban.time).toString("MM/dd/yyyy, hh:mm");
        l_recent_bans << "Ban lasts until: " + l_banned_until;
        l_recent_bans << "Moderator: " + l_ban.moderator;
        l_recent_bans << "-----";
    }
    f_context.reply(l_recent_bans.join("\n"));
}

void cmdUnban(CommandContext &f_context, const Args &f_args)
{
    const int l_target_ban = f_args.integer(QStringLiteral("ban_id"));
    if (f_context.server()->databaseManager()->invalidateBan(l_target_ban))
        f_context.reply("Successfully invalidated ban " + QString::number(l_target_ban) + ".");
    else
        f_context.reply("Couldn't invalidate ban " + QString::number(l_target_ban) + ", are you sure it exists?");
}

void cmdLiftSanction(CommandContext &f_context, const Args &f_args)
{
    const QString l_ipid = f_args.token(QStringLiteral("ipid"));
    // Plugin sanctions are plain strings too, so any word is looked up.
    const QString l_sanction = f_args.token(QStringLiteral("sanction")).toLower();
    if (!f_context.server()->databaseManager()->sanctionRow(l_ipid, l_sanction).has_value()) {
        f_context.reply("No stored \"" + l_sanction + "\" sanction for that IPID.");
        return;
    }
    f_context.server()->removeSanction(l_ipid, l_sanction);
    f_context.reply("Lifted \"" + l_sanction + "\" from " + l_ipid + ".");
}

void cmdAbout(CommandContext &f_context, const Args &f_args)
{
    const QString l_akashi_about = "Thank you for using akashi! Made with love by scatterflower, with help from in1tiate, Salanto, and mangosarentliterature. akashi " + QCoreApplication::applicationVersion() + ". For documentation and reporting issues, see the source: https://github.com/AttorneyOnline/akashi";
    auto l_plugins = f_context.services()->resolve<PluginManager>(QStringLiteral("akashi.plugins"));
    const QMap<QString, QString> l_abouts = l_plugins ? l_plugins->abouts() : QMap<QString, QString>();

    if (f_args.has(QStringLiteral("plugin"))) {
        const QString l_name = f_args.token(QStringLiteral("plugin"));
        if (l_name.compare(QStringLiteral("akashi"), Qt::CaseInsensitive) == 0)
            f_context.sendPacket("CT", {"The akashi dev team", l_akashi_about});
        else if (l_abouts.contains(l_name))
            f_context.sendPacket("CT", {l_name, l_abouts.value(l_name)});
        else {
            QStringList l_names(QStringLiteral("akashi"));
            l_names += l_abouts.keys();
            f_context.reply("Nothing is registered under \"" + l_name + "\". Try one of: " + l_names.join(", ") + ".");
        }
        return;
    }

    QStringList l_sections(l_akashi_about);
    for (auto it = l_abouts.cbegin(); it != l_abouts.cend(); ++it)
        l_sections << it.key() + ": " + it.value();
    f_context.sendPacket("CT", {"The akashi dev team", l_sections.join("\n\n")});
}

bool applySanction(CommandContext &f_context, TargetPlayer &f_target, const QString &f_sanction_id, const QString &f_data)
{
    std::optional<QDateTime> l_until = std::nullopt;
    if (f_context.argc() > 1) {
        const QString l_text = QStringList(f_context.arguments().mid(1)).join(QLatin1Char(' '));
        l_until = akashi::parseWhen(l_text, QDateTime::currentDateTime());
        if (!l_until.has_value()) {
            f_context.reply(helpers::ParseError(helpers::ParseError::NotATime, l_text, QStringLiteral("until")));
            return false;
        }
    }
    return applySanctionUntil(f_context, f_target, f_sanction_id, l_until, f_data);
}

bool applySanctionUntil(CommandContext &f_context, TargetPlayer &f_target, const QString &f_sanction_id, const std::optional<QDateTime> &f_until, const QString &f_data)
{
    // Staff protection lives at administration time, the one spot that
    // covers block sanctions, text transforms and charcurse alike; only
    // super reaches past it. Declared commands refuse such a target
    // before they run; this keeps the rule for every other caller.
    if (f_target.canPerform(akashi::permission::sanction_immune) && !f_context.canPerform(akashi::permission::super)) {
        f_context.reply("That player is protected from sanctions.");
        return false;
    }
    const QString l_moderator = f_context.server()->authType() == AuthType::ADVANCED
                                    ? f_context.moderatorName()
                                    : QStringLiteral("moderator");
    const std::optional<QDateTime> &l_until = f_until;
    // The issuer never sweeps themselves in by sharing an IP or machine
    // with the target; targeting their own id on purpose still works.
    akashi::ClientSession *l_exempt = nullptr;
    if (f_target.clientId() != f_context.clientId()) {
        l_exempt = f_context.self();
    }
    f_context.server()->applySanction(f_target.ipid(), f_target.hwid(), f_sanction_id, l_moderator, l_until, f_data, l_exempt);
    if (l_until.has_value()) {
        f_context.reply("The sanction lifts itself " + l_until->toString("yyyy-MM-dd hh:mm") + ".");
    }
    return true;
}

void liftSanction(CommandContext &f_context, TargetPlayer &f_target, const QString &f_sanction_id)
{
    f_context.server()->removeSanction(f_target.ipid(), f_sanction_id);
}

// The optional "until" of a sanction command, already read as a moment.
static std::optional<QDateTime> untilOf(const Args &f_args)
{
    if (!f_args.has(QStringLiteral("until")))
        return std::nullopt;
    return f_args.when(QStringLiteral("until"));
}

void cmdMute(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::muted);
    if (!applySanctionUntil(f_context, l_target, akashi::sanction::muted, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already muted!");
    else {
        f_context.reply("Muted player.");
        l_target.reply("You were muted by a moderator. " + reprimand(f_context.server()));
    }
}

void cmdUnmute(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::muted);
    liftSanction(f_context, l_target, akashi::sanction::muted);
    if (!l_was_sanctioned)
        f_context.reply("That player is not muted!");
    else {
        f_context.reply("Unmuted player.");
        l_target.reply("You were unmuted by a moderator. " + reprimand(f_context.server(), true));
    }
}

void cmdOocMute(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::ooc_muted);
    if (!applySanctionUntil(f_context, l_target, akashi::sanction::ooc_muted, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already OOC muted!");
    else {
        f_context.reply("OOC muted player.");
        l_target.reply("You were OOC muted by a moderator. " + reprimand(f_context.server()));
    }
}

void cmdOocUnmute(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::ooc_muted);
    liftSanction(f_context, l_target, akashi::sanction::ooc_muted);
    if (!l_was_sanctioned)
        f_context.reply("That player is not OOC muted!");
    else {
        f_context.reply("OOC unmuted player.");
        l_target.reply("You were OOC unmuted by a moderator. " + reprimand(f_context.server(), true));
    }
}

void cmdBlockWtce(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::wtce_blocked);
    if (!applySanctionUntil(f_context, l_target, akashi::sanction::wtce_blocked, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already judge blocked!");
    else {
        f_context.reply("Revoked player's access to judge controls.");
        l_target.reply("A moderator revoked your judge controls access. " + reprimand(f_context.server()));
    }
}

void cmdUnblockWtce(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::wtce_blocked);
    liftSanction(f_context, l_target, akashi::sanction::wtce_blocked);
    if (!l_was_sanctioned)
        f_context.reply("That player is not judge blocked!");
    else {
        f_context.reply("Restored player's access to judge controls.");
        l_target.reply("A moderator restored your judge controls access. " + reprimand(f_context.server(), true));
    }
}

void cmdAllowBlankposting(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleBlankposting();
    if (l_area->isBlankpostingAllowed() == false) {
        f_context.replyToArea(f_context.name() + " has set blankposting in the area to forbidden.");
    }
    else {
        f_context.replyToArea(f_context.name() + " has set blankposting in the area to allowed.");
    }
}

void cmdBanInfo(CommandContext &f_context, const Args &f_args)
{
    const QString l_id = f_args.token(QStringLiteral("id"));
    QStringList l_ban_info;
    l_ban_info << ("Ban Info for " + l_id);
    l_ban_info << "-----";
    const QString l_lookup_type = f_args.has(QStringLiteral("type")) ? f_args.word(QStringLiteral("type")) : QStringLiteral("banid");
    const QList<DBManager::BanInfo> l_bans = f_context.server()->databaseManager()->banInfo(l_lookup_type, l_id);
    for (const DBManager::BanInfo &l_ban : l_bans) {
        QString l_banned_until;
        if (l_ban.duration == -2)
            l_banned_until = "The heat death of the universe";
        else
            l_banned_until = QDateTime::fromSecsSinceEpoch(l_ban.time).addSecs(l_ban.duration).toString("MM/dd/yyyy, hh:mm");
        l_ban_info << "Ban ID: " + QString::number(l_ban.id);
        l_ban_info << "Affected IPID: " + l_ban.ipid;
        l_ban_info << "Affected HDID: " + l_ban.hdid;
        l_ban_info << "Reason for ban: " + l_ban.reason;
        l_ban_info << "Date of ban: " + QDateTime::fromSecsSinceEpoch(l_ban.time).toString("MM/dd/yyyy, hh:mm");
        l_ban_info << "Ban lasts until: " + l_banned_until;
        l_ban_info << "Moderator: " + l_ban.moderator;
        l_ban_info << "-----";
    }
    f_context.reply(l_ban_info.join("\n"));
}

void cmdReload(CommandContext &f_context, const Args &)
{
    f_context.server()->reloadSettings();
    f_context.reply("Reloaded configurations");
}

void cmdForceNointPres(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleImmediate();
    QString l_state = l_area->forceImmediate() ? "on." : "off.";
    f_context.reply("Forced immediate text processing in this area is now " + l_state);
}

void cmdAllowIniswap(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleIniswap();
    QString state = l_area->isIniswapAllowed() ? "allowed." : "disallowed.";
    f_context.reply("Iniswapping in this area is now " + state);
}

void cmdPermitSaving(CommandContext &f_context, const Args &f_args)
{
    // The old per-session flag is a real person-scope grant now: it
    // covers every window the person owns and reads back through the
    // same resolution as any other permission.
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    f_context.server()->permissionRegistry()->addGrant(
        {akashi::permission::save_testimony, akashi::Audience::person(l_target.ipid()), akashi::GrantScope::Server, -1, QStringLiteral("command")});
    f_context.reply("Testimony saving has been enabled for client " + QString::number(l_target.clientId()));
}

// The /kick_uid door: one client only; the act itself is the server's
// kick verb, so unlike before this door leaves a log entry like /kick.
void cmdKickUid(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    const QString l_reason = f_args.text(QStringLiteral("reason"));
    const QString l_moderator = f_context.server()->authType() == AuthType::ADVANCED
                                    ? f_context.moderatorName()
                                    : QStringLiteral("Moderator");
    f_context.server()->kickClients({l_target}, l_target->ipid(), l_reason, l_moderator);
    f_context.reply("Kicked client with UID " + QString::number(l_target->clientId()) + " for reason: " + l_reason);
}

// /update_ban <ban_id> duration <duration|perma> or <ban_id> reason <text>:
// the field is a fixed word per form, so its value arrives checked.
void cmdUpdateBan(CommandContext &f_context, const Args &f_args)
{
    const int l_ban_id = f_args.integer(QStringLiteral("ban_id"));
    const QString l_field = f_args.word(QStringLiteral("field"));
    QVariant l_updated_info;
    if (l_field == QLatin1String("reason")) {
        l_updated_info = QVariant(f_args.text(QStringLiteral("reason")));
    }
    else {
        const long long l_duration_seconds = f_args.word(QStringLiteral("duration")) == QLatin1String("perma")
                                                 ? -2
                                                 : f_args.duration(QStringLiteral("duration"));
        l_updated_info = QVariant(l_duration_seconds);
    }
    if (!f_context.server()->databaseManager()->updateBan(l_ban_id, l_field, l_updated_info)) {
        f_context.reply("There was an error updating the ban. Please confirm the ban ID is valid.");
        return;
    }
    f_context.reply("Ban updated.");
}

void cmdNotice(CommandContext &f_context, const Args &f_args)
{
    sendNotice(f_context, f_args.text(QStringLiteral("message")));
}

void cmdNoticeG(CommandContext &f_context, const Args &f_args)
{
    sendNotice(f_context, f_args.text(QStringLiteral("message")), true);
}

void cmdKickOther(CommandContext &f_context, const Args &)
{
    int l_kick_counter = 0;

    QList<akashi::ClientSession *> l_target_clients;
    const QList<akashi::ClientSession *> l_targets_hwid = f_context.server()->clientsByHwid(f_context.hwid());
    l_target_clients = f_context.server()->clientsByIpid(f_context.ipid());

    for (akashi::ClientSession *l_target_candidate : std::as_const(l_targets_hwid)) {
        if (!l_target_clients.contains(l_target_candidate)) {
            l_target_clients.append(l_target_candidate);
        }
    }

    QMutableListIterator<akashi::ClientSession *> it(l_target_clients);
    while (it.hasNext()) {
        if (it.next()->clientId() == f_context.clientId()) {
            it.remove();
        }
    }

    for (akashi::ClientSession *l_target_client : std::as_const(l_target_clients)) {
        l_target_client->closeSocket();
        l_kick_counter++;
    }
    f_context.reply("Kicked " + QString::number(l_kick_counter) + " multiclients from the server.");
}

void registerModerationCommands(CommandRegistry &f_registry)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");
    const Param l_ipid = param::token(QStringLiteral("ipid"));
    const Param l_reason = param::text(QStringLiteral("reason"));
    const Param l_message = param::text(QStringLiteral("message"));
    const Param l_target = param::client(QStringLiteral("target"));
    // Sanctions refuse staff who hold sanction.immune, unless the
    // moderator holds super.
    const Param l_sanctionable = param::client(QStringLiteral("target")).immuneIf(sanction_immune);
    const Param l_until = param::when(QStringLiteral("until")).optional();
    const Param l_ban_id = param::integer(QStringLiteral("ban_id"), 0, std::numeric_limits<int>::max());

    {
        CommandSpec l_ban;
        l_ban.name = QStringLiteral("ban");
        l_ban.description = QStringLiteral("Bans a client by IPID.");
        l_ban.variants = {
            {.id = QStringLiteral("for"), .gate = {ban}, .description = QStringLiteral("Bans for a while, such as 1d12h."), .params = {l_ipid, param::duration(QStringLiteral("duration")), l_reason}, .run = cmdBan},
            {.id = QStringLiteral("perma"), .gate = {ban}, .description = QStringLiteral("Bans permanently."), .params = {l_ipid, param::word(QStringLiteral("duration"), {QStringLiteral("perma")}), l_reason}, .run = cmdBan},
        };
        f_registry.registerCommand(l_ban, l_core);
    }
    f_registry.registerCommand(singleForm(QStringLiteral("kick"), {kick}, {l_ipid, l_reason}, QStringLiteral("Kicks all clients with the given IPID."), cmdKick), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("mods"), {info_mods}, {}, QStringLiteral("Lists currently logged-in moderators."), cmdMods), l_core);

    {
        // /why mirrors the resolution pipeline: the self form is open to
        // every player, the target form is staff visibility.
        CommandSpec l_why;
        l_why.name = QStringLiteral("why");
        l_why.description = QStringLiteral("Explains why you (or another player) can or cannot do something here.");
        l_why.variants = {
            {.id = QStringLiteral("self"), .gate = {info_why}, .description = QStringLiteral("Asks about yourself."), .params = {param::token(QStringLiteral("query"))}, .run = cmdWhy},
            {.id = QStringLiteral("target"), .gate = {see_staff_presence}, .description = QStringLiteral("Asks about another player."), .params = {l_target, param::token(QStringLiteral("query"))}, .run = cmdWhy},
        };
        f_registry.registerCommand(l_why, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("dumpgrants"), {see_ipids}, {}, QStringLiteral("Lists every stored grant with its audience and owner tag."), cmdDumpGrants, {QStringLiteral("dump_grants")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("commands"), {info_commands}, {}, QStringLiteral("Lists all commands available to you."), cmdCommands), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("help"), {info_help}, {param::token(QStringLiteral("command")).optional()}, QStringLiteral("Displays help for a command, or for all of them with /help all."), cmdHelp), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("motd"), {info_motd}, {}, QStringLiteral("Displays the Message of the Day."), cmdMotd), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("set_motd"), {motd}, {l_message}, QStringLiteral("Sets the Message of the Day."), cmdSetMotd, {QStringLiteral("setmotd")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("bans"), {ban}, {}, QStringLiteral("Lists the last 5 bans."), cmdBans), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unban"), {ban}, {l_ban_id}, QStringLiteral("Invalidates a ban by its ID."), cmdUnban), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("lift_sanction"),
                                          {sanction_mute, sanction_ooc_mute, sanction_block_dj, sanction_block_wtce, sanction_gimp, sanction_disemvowel, sanction_shake, sanction_medieval, sanction_charcurse},
                                          {l_ipid, param::token(QStringLiteral("sanction"))},
                                          QStringLiteral("Lifts a stored sanction by IPID, whether the person is online or not."), cmdLiftSanction, {QStringLiteral("liftsanction")}),
                               l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("about"), {info_about}, {param::token(QStringLiteral("plugin")).optional()}, QStringLiteral("Shows who made the server and its plugins; a plugin id shows that one alone."), cmdAbout), l_core);

    f_registry.registerCommand(singleForm(QStringLiteral("mute"), {sanction_mute}, {l_sanctionable, l_until}, QStringLiteral("Mutes a client, until a time like 1d12h, friday or 01.01.2028 18:00 if one is given."), cmdMute), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unmute"), {sanction_mute}, {l_target}, QStringLiteral("Unmutes a client."), cmdUnmute), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("ooc_mute"), {sanction_ooc_mute}, {l_sanctionable, l_until}, QStringLiteral("OOC-mutes a client, until a time like 1d12h, friday or 01.01.2028 18:00 if one is given."), cmdOocMute, {QStringLiteral("mute_ooc"), QStringLiteral("oocmute")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("ooc_unmute"), {sanction_ooc_mute}, {l_target}, QStringLiteral("OOC-unmutes a client."), cmdOocUnmute, {QStringLiteral("unmute_ooc"), QStringLiteral("oocunmute")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("block_wtce"), {sanction_block_wtce}, {l_sanctionable, l_until}, QStringLiteral("Blocks a client from using judge controls, until a time like 1d12h if one is given."), cmdBlockWtce, {QStringLiteral("blockwtce")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unblock_wtce"), {sanction_block_wtce}, {l_target}, QStringLiteral("Restores a client's judge controls."), cmdUnblockWtce, {QStringLiteral("unblockwtce")}), l_core);

    f_registry.registerCommand(singleForm(QStringLiteral("allow_blankposting"), {chat_moderator}, {}, QStringLiteral("Toggles blankposting in the area."), cmdAllowBlankposting, {QStringLiteral("allowblankposting")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("baninfo"), {ban},
                                          {param::token(QStringLiteral("id")), param::word(QStringLiteral("type"), {QStringLiteral("banid"), QStringLiteral("ipid"), QStringLiteral("hdid")}).optional()},
                                          QStringLiteral("Looks up ban info by ban ID, or by IPID or HDID."), cmdBanInfo),
                               l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("reload"), {super}, {}, QStringLiteral("Reloads server configuration."), cmdReload), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("force_noint_pres"), {cm_force_noint_pres}, {}, QStringLiteral("Toggles forced immediate text processing."), cmdForceNointPres, {QStringLiteral("forceimmediate")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("allow_iniswap"), {cm_allow_iniswap}, {}, QStringLiteral("Toggles iniswap permission in the area."), cmdAllowIniswap, {QStringLiteral("allowiniswap")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("permitsaving"), {chat_moderator}, {l_target}, QStringLiteral("Grants a client permission to save testimony."), cmdPermitSaving), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("kick_uid"), {kick}, {l_target, l_reason}, QStringLiteral("Kicks a single client by UID."), cmdKickUid, {QStringLiteral("kickuid")}), l_core);

    {
        CommandSpec l_update_ban;
        l_update_ban.name = QStringLiteral("update_ban");
        l_update_ban.aliases = {QStringLiteral("updateban")};
        l_update_ban.description = QStringLiteral("Updates a ban's duration or reason.");
        const Param l_duration_field = param::word(QStringLiteral("field"), {QStringLiteral("duration")});
        l_update_ban.variants = {
            {.id = QStringLiteral("duration"), .gate = {ban}, .params = {l_ban_id, l_duration_field, param::duration(QStringLiteral("duration"))}, .run = cmdUpdateBan},
            {.id = QStringLiteral("perma"), .gate = {ban}, .params = {l_ban_id, l_duration_field, param::word(QStringLiteral("duration"), {QStringLiteral("perma")})}, .run = cmdUpdateBan},
            {.id = QStringLiteral("reason"), .gate = {ban}, .params = {l_ban_id, param::word(QStringLiteral("field"), {QStringLiteral("reason")}), l_reason}, .run = cmdUpdateBan},
        };
        f_registry.registerCommand(l_update_ban, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("notice"), {send_notice}, {l_message}, QStringLiteral("Sends a notice to the area."), cmdNotice), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("noticeg"), {send_notice}, {l_message}, QStringLiteral("Sends a server-wide notice."), cmdNoticeG), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("kick_other"), {user}, {}, QStringLiteral("Kicks your other connected clients."), cmdKickOther, {QStringLiteral("kickother")}), l_core);
}

} // namespace akashi::commands
