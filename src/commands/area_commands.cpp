#include "commands/area_commands.h"

#include "akashi/helpers/parse.h"
#include "akashi/permissions.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/server_context.h"
#include "proto/packet.h"
#include "world/area.h"

#include <QRegularExpression>

namespace akashi::commands {

static QStringList buildAreaList(CommandContext &f_context, int f_area_index)
{
    ServerContext *l_server = f_context.server();
    akashi::Area *l_area = l_server->areaById(f_area_index);
    QStringList l_entries;

    l_entries.append("=== " + l_server->areaName(f_area_index) + " ===");
    switch (l_area->lockState()) {
    case akashi::Area::LockState::Locked:
        l_entries.append("[LOCKED]");
        break;
    case akashi::Area::LockState::Spectatable:
        l_entries.append("[SPECTATABLE]");
        break;
    case akashi::Area::LockState::Free:
        break;
    }
    l_entries.append("[" + QString::number(l_area->playerCount()) + " users][" + l_area->status() + "]");

    const QVector<akashi::ClientSession *> l_clients = l_server->clients();
    for (akashi::ClientSession *l_client : l_clients) {
        if (l_client->areaId() == f_area_index && l_client->isJoined()) {
            QString l_char_entry = "[" + QString::number(l_client->clientId()) + "] " + l_client->character();
            if (l_client->character().isEmpty()) {
                l_char_entry += "Spectator";
            }
            if (!l_client->characterName().isEmpty()) {
                l_char_entry += " (" + l_client->characterName() + ")";
            }
            if (l_area->owners().contains(l_client->clientId())) {
                l_char_entry.insert(0, "[CM] ");
            }
            if (f_context.canPerform(akashi::permission::see_ipids)) {
                l_char_entry += " (" + l_client->ipid() + "): " + l_client->name();
            }
            l_entries.append(l_char_entry);
        }
    }
    return l_entries;
}

void cmdGetArea(CommandContext &f_context, const Args &)
{
    QStringList l_entries = buildAreaList(f_context, f_context.areaId());
    f_context.reply(l_entries.join("\n"));
}

void cmdGetAreas(CommandContext &f_context, const Args &)
{
    ServerContext *l_server = f_context.server();
    QStringList l_entries;
    l_entries.append("\n== Currently Online: " + QString::number(l_server->playerCount()) + " ==");
    for (int i = 0; i < l_server->areaCount(); i++) {
        if (l_server->areaById(i)->playerCount() > 0) {
            l_entries.append(buildAreaList(f_context, i));
        }
    }
    f_context.reply(l_entries.join("\n"));
}

void cmdArea(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    l_client->changeArea(f_args.number(QStringLiteral("area")));
}

// /cm without a target. Claiming an area that has no CM is the form gated
// on area.cm; the open form only says why there is nothing to claim.
static void cmSelf(CommandContext &f_context, bool f_may_claim)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (l_area->owners().isEmpty()) {
        // Walk-in CM defaults closed: the claim needs area.cm from
        // somewhere - the area's own offer (an areas.json grants line
        // opens the classic walk-in), a role, or super. A protected area
        // is simply one that offers nothing.
        if (!f_may_claim) {
            f_context.reply("This area does not offer CM. Ask a moderator, or check /why cm.");
            return;
        }
        if (auto l_refusal = l_client->addAreaOwner(f_context.clientId())) {
            f_context.reply(*l_refusal);
            return;
        }
        f_context.replyToArea(f_context.name() + " is now CM in this area.");
    }
    else if (!l_area->owners().contains(f_context.clientId())) {
        f_context.reply("You cannot become a CM in this area when someone else is. You must be CM'ed by an existing one.");
    }
    else {
        f_context.reply("You are already a CM in this area.");
    }
}

void cmdCmClaim(CommandContext &f_context, const Args &)
{
    cmSelf(f_context, true);
}

void cmdCm(CommandContext &f_context, const Args &)
{
    cmSelf(f_context, false);
}

void cmdCmAdd(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (l_area->owners().isEmpty()) {
        f_context.reply("There are no CMs in this area. Claim it with /cm first.");
        return;
    }
    if (!l_area->owners().contains(f_context.clientId())) {
        f_context.reply("You cannot become a CM in this area when someone else is. You must be CM'ed by an existing one.");
        return;
    }
    akashi::ClientSession *l_owner_candidate = f_args.client(QStringLiteral("target"));
    if (l_area->owners().contains(l_owner_candidate->clientId())) {
        f_context.reply("User is already a CM in this area.");
        return;
    }
    if (auto l_refusal = l_client->addAreaOwner(l_owner_candidate->clientId())) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.replyToArea(l_owner_candidate->name() + " is now CM in this area.");
}

void cmdUncmOwn(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->owners().isEmpty()) {
        f_context.reply("There are no CMs in this area.");
        return;
    }
    if (!l_area->owners().contains(f_context.clientId())) {
        f_context.reply("You are not a CM in this area.");
        return;
    }

    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->removeAreaOwner(f_context.clientId())) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.reply("You are no longer CM in this area.");
}

void cmdUncmAll(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->owners().isEmpty()) {
        f_context.reply("There are no CMs in this area.");
        return;
    }

    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }

    const QList<int> l_owners = l_area->owners();
    for (int l_uid : l_owners) {
        if (l_uid != f_context.clientId()) {
            // A mid-loop block leaves the earlier removals applied.
            if (auto l_refusal = l_client->removeAreaOwner(l_uid)) {
                f_context.reply(*l_refusal);
                return;
            }
            akashi::ClientSession *l_target = f_context.server()->clientById(l_uid);
            if (l_target != nullptr) {
                l_target->sendServerMessage("You have been unCMed.");
            }
        }
    }
    f_context.reply("All CMs except yourself have been unCMed.");
}

void cmdUncmOther(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->owners().isEmpty()) {
        f_context.reply("There are no CMs in this area.");
        return;
    }

    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }

    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    const int l_uid = l_target->clientId();
    if (!l_area->owners().contains(l_uid)) {
        f_context.reply("That user is not CMed.");
        return;
    }
    if (auto l_refusal = l_client->removeAreaOwner(l_uid)) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.reply(l_target->name() + " was successfully unCMed.");
    l_target->sendServerMessage("You have been unCMed by a moderator.");
}

void cmdInvite(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    if (!l_area->invite(l_target->clientId())) {
        f_context.reply("That ID is already on the invite list.");
        return;
    }
    f_context.reply("You invited ID " + QString::number(l_target->clientId()));
    l_target->sendServerMessage("You were invited and given access to " + l_area->name());
}

// The target is already checked: not one of this area's CMs.
void cmdUninvite(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    const int l_uninvited_id = l_target->clientId();
    if (!l_area->uninvite(l_uninvited_id)) {
        f_context.reply("That ID is not on the invite list.");
        return;
    }
    f_context.reply("You uninvited ID " + QString::number(l_uninvited_id));
    l_target->sendServerMessage("You were uninvited from " + l_area->name());
}

void cmdAreaLock(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->lockState() == akashi::Area::LockState::Locked) {
        f_context.reply("This area is already locked.");
        return;
    }
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->setAreaLock(akashi::Area::LockState::Locked)) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.replyToArea("This area is now locked.");
}

void cmdAreaSpectate(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->lockState() == akashi::Area::LockState::Spectatable) {
        f_context.reply("This area is already in spectate mode.");
        return;
    }
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->setAreaLock(akashi::Area::LockState::Spectatable)) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.replyToArea("This area is now spectatable.");
}

void cmdAreaUnlock(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->lockState() == akashi::Area::LockState::Free) {
        f_context.reply("This area is not locked.");
        return;
    }
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->setAreaLock(akashi::Area::LockState::Free)) {
        f_context.reply(*l_refusal);
        return;
    }
    f_context.replyToArea("This area is now unlocked.");
}

// Moves one client out of the invoker's area, uninvited, and tells them.
static void kickToArea(CommandContext &f_context, akashi::ClientSession *f_target, int f_area_id)
{
    akashi::Area *l_to = f_context.server()->areaById(f_area_id);
    f_target->changeArea(f_area_id);
    f_context.area()->uninvite(f_target->clientId());
    f_target->sendServerMessage("You have been kicked to area " + l_to->displayName() + ".");
}

// /area_kick <target> [destination]: the target is already checked - in
// this area, and not one of its CMs.
static void cmdAreaKickOne(CommandContext &f_context, const Args &f_args)
{
    const int l_to = f_args.has(QStringLiteral("destination")) ? f_args.number(QStringLiteral("destination")) : 0;
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    kickToArea(f_context, l_target, l_to);
    f_context.reply("Client " + QString::number(l_target->clientId()) + " kicked to area " + f_context.server()->areaById(l_to)->displayName() + ".");
}

// /area_kick all [destination]: everyone here except the invoker and the
// area's CMs.
static void cmdAreaKickAll(CommandContext &f_context, const Args &f_args)
{
    const int l_to = f_args.has(QStringLiteral("destination")) ? f_args.number(QStringLiteral("destination")) : 0;
    const QList<int> l_owners = f_context.area()->owners();
    const QVector<akashi::ClientSession *> l_clients = f_context.server()->clients();
    for (akashi::ClientSession *l_client : l_clients) {
        if (l_client->areaId() == f_context.areaId() && l_client->clientId() != f_context.clientId() && !l_owners.contains(l_client->clientId())) {
            kickToArea(f_context, l_client, l_to);
        }
    }
    f_context.reply("All clients kicked to area " + f_context.server()->areaById(l_to)->displayName() + ".");
}

void cmdBackground(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (!l_client->canActInArea()) {
        f_context.reply("Spectators are blocked from changing the background.");
        return;
    }
    // The verb owns the background rules, the validation, the BN broadcast,
    // the background's ambience and the area notice.
    if (auto l_refusal = l_client->changeBackground(f_args.text(QStringLiteral("background")))) {
        f_context.reply(*l_refusal);
    }
}

void cmdSide(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    // The background lock is enforced by the check_background floor rule.
    const QString l_side = f_args.text(QStringLiteral("side"));
    if (auto l_refusal = l_client->changeBackgroundSide(l_side)) {
        f_context.reply(*l_refusal);
        return;
    }
    if (l_side.isEmpty()) {
        f_context.replyToArea(f_context.character() + " unlocked the background side");
    }
    else {
        f_context.replyToArea(f_context.character() + " locked the background side to " + l_side);
    }
}

void cmdLockBackground(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());

    if (l_area->isBgLocked() == false) {
        l_area->toggleBgLock();
    };

    f_context.server()->broadcast(akashi::Packet("CT", {f_context.server()->serverNickname(), f_context.character() + " locked the background.", "1"}), f_context.areaId());
}

void cmdUnlockBackground(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());

    if (l_area->isBgLocked() == true) {
        l_area->toggleBgLock();
    };

    f_context.server()->broadcast(akashi::Packet("CT", {f_context.server()->serverNickname(), f_context.character() + " unlocked the background.", "1"}), f_context.areaId());
}

void cmdStatus(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    // The verb validates before it commits, so a blocked change needs no
    // rollback and a refused one sends no area update at all.
    if (auto l_refusal = l_client->changeAreaStatus(f_args.text(QStringLiteral("status")))) {
        f_context.reply(*l_refusal);
    }
}

void cmdJudgeLog(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (l_area->judgelog().isEmpty()) {
        f_context.reply("There have been no judge actions in this area.");
        return;
    }
    QString l_message = l_area->judgelog().join("\n");
    if (f_context.canPerform(akashi::permission::see_real_names)) {
        f_context.reply(l_message);
    }
    else {
        static const QRegularExpression s_parenthetical("[(].*[)]");
        QString filteredmessage = l_message.remove(s_parenthetical);
        f_context.reply(filteredmessage);
    }
}

void cmdIgnoreBgList(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleIgnoreBgList();
    QString l_state = l_area->ignoreBgList() ? "ignored." : "enforced.";
    f_context.reply("BG list in this area is now " + l_state);
}

void cmdAreaMessage(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (!f_args.has(QStringLiteral("message"))) {
        f_context.reply(l_area->areaMessage());
        return;
    }

    l_area->changeAreaMessage(f_args.text(QStringLiteral("message")));
    f_context.reply("Updated this area's message.");
}

void cmdToggleMessage(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleAreaMessageJoin();
    QString l_state = l_area->sendAreaMessageOnJoin() ? "enabled." : "disabled.";
    f_context.reply("Sending message on area join is now " + l_state);
}

void cmdClearMessage(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->clearAreaMessage();
    if (l_area->sendAreaMessageOnJoin()) {
        cmdToggleMessage(f_context, f_args);
    }
}

void cmdToggleWtce(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleWtceAllowed();
    QString l_state = l_area->isWtceAllowed() ? "enabled." : "disabled.";
    f_context.reply("Using testimony animations is now " + l_state);
}

void cmdToggleShouts(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleShoutAllowed();
    QString l_state = l_area->isShoutAllowed() ? "enabled." : "disabled.";
    f_context.reply("Using shouts is now " + l_state);
}

void cmdFloors(CommandContext &f_context, const Args &)
{
    ServerContext *l_server = f_context.server();
    QStringList l_lines;
    l_lines.append("== Floors ==");
    for (int i = 0; i < l_server->floorCount(); i++) {
        const akashi::Floor *l_floor = l_server->floorById(i);
        QStringList l_area_names;
        for (int l_aid : l_floor->area_ids) {
            l_area_names.append(l_server->areaName(l_aid));
        }
        l_lines.append("[" + QString::number(i) + "] " + l_floor->name + ": " + l_area_names.join(", "));
    }
    f_context.reply(l_lines.join("\n"));
}

// A floor by its ID, or else by its name.
static const akashi::Floor *findFloor(ServerContext *f_server, const QString &f_text)
{
    const auto l_id = helpers::parseInt(f_text, QStringLiteral("floor_id"));
    return l_id ? f_server->floorById(*l_id) : f_server->floorByName(f_text);
}

void cmdFloor(CommandContext &f_context, const Args &f_args)
{
    const akashi::Floor *l_floor = findFloor(f_context.server(), f_args.text(QStringLiteral("floor")));
    if (!l_floor) {
        f_context.reply("No floor found with that name or ID. Use /floors to list them.");
        return;
    }
    if (l_floor->area_ids.isEmpty()) {
        f_context.reply("That floor has no areas.");
        return;
    }

    akashi::ClientSession *l_client = f_context.self();
    if (l_client) {
        l_client->changeArea(l_floor->area_ids.first());
    }
}

void cmdCreateArea(CommandContext &f_context, const Args &f_args)
{
    ServerContext *l_server = f_context.server();
    const QString l_name = f_args.text(QStringLiteral("name"));
    const int l_floor_id = l_server->floorIdForArea(f_context.areaId());
    const int l_area_id = l_server->createArea(l_name, l_floor_id);
    if (l_area_id < 0) {
        f_context.reply("That does not look like a usable area name.");
        return;
    }
    f_context.reply("Created area [" + QString::number(l_area_id) + "] " + l_name + " on this floor.");
}

void cmdCreateFloor(CommandContext &f_context, const Args &f_args)
{
    const QString l_name = f_args.text(QStringLiteral("name"));
    const int l_floor_id = f_context.server()->createFloor(l_name);
    if (l_floor_id < 0) {
        f_context.reply("A floor with that name already exists, or the name is unusable.");
        return;
    }
    f_context.reply("Created floor " + l_name + " with one starter area. Use /floor " + l_name + " to visit it.");
}

void cmdRenameArea(CommandContext &f_context, const Args &f_args)
{
    const QString l_name = f_args.text(QStringLiteral("name"));
    if (!f_context.server()->renameArea(f_context.areaId(), l_name)) {
        f_context.reply("That does not look like a usable area name.");
        return;
    }
    f_context.replyToArea("This area is now called " + l_name + ".");
}

void cmdRenameFloor(CommandContext &f_context, const Args &f_args)
{
    ServerContext *l_server = f_context.server();
    const QString l_name = f_args.text(QStringLiteral("name"));
    if (!l_server->renameFloor(l_server->floorIdForArea(f_context.areaId()), l_name)) {
        f_context.reply("A floor with that name already exists, or the name is unusable.");
        return;
    }
    f_context.reply("This floor is now called " + l_name + ".");
}

// One form gives the area's ID, the other its name.
void cmdRemoveArea(CommandContext &f_context, const Args &f_args)
{
    ServerContext *l_server = f_context.server();
    const int l_area_id = f_args.has(QStringLiteral("area")) ? int(f_args.number(QStringLiteral("area"))) : l_server->areaNames().indexOf(f_args.text(QStringLiteral("name")));
    const QString l_name = l_server->areaName(l_area_id);
    if (auto l_error = l_server->removeArea(l_area_id)) {
        f_context.reply(*l_error);
        return;
    }
    f_context.reply("Removed area " + l_name + ".");
}

void cmdRemoveFloor(CommandContext &f_context, const Args &f_args)
{
    ServerContext *l_server = f_context.server();
    const akashi::Floor *l_floor = findFloor(l_server, f_args.text(QStringLiteral("floor")));
    if (!l_floor) {
        f_context.reply("No floor found with that name or ID. Use /floors to list them.");
        return;
    }
    const QString l_name = l_floor->name;
    if (auto l_error = l_server->removeFloor(l_floor->id)) {
        f_context.reply(*l_error);
        return;
    }
    f_context.reply("Removed floor " + l_name + " and its areas.");
}

void cmdSaveWorld(CommandContext &f_context, const Args &)
{
    if (auto l_error = f_context.server()->saveWorld()) {
        f_context.reply("Unable to save: " + *l_error);
        return;
    }
    f_context.reply("Saved the floors, areas and their rules to areas.json.");
}

void cmdLoadWorld(CommandContext &f_context, const Args &)
{
    if (auto l_error = f_context.server()->reloadWorld()) {
        f_context.reply("Unable to reload: " + *l_error);
        return;
    }
    f_context.replyToServer("The floors and areas were reloaded from areas.json.");
}

void cmdSaveFloor(CommandContext &f_context, const Args &f_args)
{
    ServerContext *l_server = f_context.server();
    int l_floor_id = l_server->floorIdForArea(f_context.areaId());
    if (f_args.has(QStringLiteral("floor"))) {
        const akashi::Floor *l_floor = findFloor(l_server, f_args.text(QStringLiteral("floor")));
        if (!l_floor) {
            f_context.reply("No floor found with that name or ID. Use /floors to list them.");
            return;
        }
        l_floor_id = l_floor->id;
    }
    const QString l_name = l_server->floorById(l_floor_id)->name;
    if (auto l_error = l_server->saveFloor(l_floor_id)) {
        f_context.reply("Unable to save: " + *l_error);
        return;
    }
    f_context.reply("Saved floor " + l_name + " to config/floors. Load it with /loadfloor " + l_name + ".");
}

void cmdLoadFloor(CommandContext &f_context, const Args &f_args)
{
    const QString l_name = f_args.text(QStringLiteral("name"));
    if (auto l_error = f_context.server()->loadFloor(l_name)) {
        f_context.reply("Unable to load: " + *l_error);
        return;
    }
    f_context.reply("Loaded floor " + l_name + " from its file.");
}

void cmdWebfiles(CommandContext &f_context, const Args &)
{
    const QVector<akashi::ClientSession *> l_clients = f_context.server()->clients();
    QStringList l_weblinks;
    for (akashi::ClientSession *l_client : l_clients) {
        if (l_client->iniswap().isEmpty() || l_client->areaId() != f_context.areaId()) {
            continue;
        }

        if (l_client->character().toLower() != l_client->iniswap().toLower()) {
            l_weblinks.append("https://attorneyonline.github.io/webDownloader/index.html?char=" + l_client->iniswap());
        }
    }
    f_context.reply("Character files:\n" + l_weblinks.join("\n"));
}

void registerAreaCommands(CommandRegistry &f_registry)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");
    const Param l_target = param::client(QStringLiteral("target"));
    const Param l_name = param::text(QStringLiteral("name"));
    // A floor by its ID or its name.
    const Param l_floor = param::text(QStringLiteral("floor"));

    f_registry.registerCommand(singleForm(QStringLiteral("getarea"), {area_getarea}, {}, QStringLiteral("Lists all clients in the area you are in."), cmdGetArea, {QStringLiteral("ga")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("getareas"), {area_getareas}, {}, QStringLiteral("Lists all clients in all areas."), cmdGetAreas, {QStringLiteral("gas")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("area"), {area_area}, {param::area(QStringLiteral("area"))}, QStringLiteral("Moves you to the area with the given ID."), cmdArea), l_core);

    {
        // Claiming an area that has no CM is its own form, gated on
        // area.cm, so /why and /help show it. Without area.cm the bare
        // /cm falls to the open form, which says why not.
        CommandSpec l_cm;
        l_cm.name = QStringLiteral("cm");
        l_cm.description = QStringLiteral("Claims CM or adds another client as CM.");
        l_cm.variants = {
            {.id = QStringLiteral("claim"), .gate = {area_cm}, .description = QStringLiteral("Claims CM in an area that has none."), .run = cmdCmClaim},
            {.id = QStringLiteral("add"), .gate = {user}, .description = QStringLiteral("Adds another client as CM of your area."), .params = {l_target}, .run = cmdCmAdd},
            {.id = QStringLiteral("own"), .gate = {user}, .description = QStringLiteral("Says why you cannot claim CM here."), .run = cmdCm},
        };
        f_registry.registerCommand(l_cm, l_core);
    }

    {
        CommandSpec l_uncm;
        l_uncm.name = QStringLiteral("uncm");
        l_uncm.description = QStringLiteral("Removes CM status from yourself or another client.");
        l_uncm.variants = {
            {.id = QStringLiteral("own"), .gate = {area_cm}, .description = QStringLiteral("Removes your own CM status."), .run = cmdUncmOwn},
            {.id = QStringLiteral("all"), .gate = {area_uncm}, .description = QStringLiteral("Removes every CM's status except yours."), .params = {param::word(QStringLiteral("who"), {QStringLiteral("all")})}, .run = cmdUncmAll},
            {.id = QStringLiteral("other"), .gate = {area_uncm}, .description = QStringLiteral("Removes another client's CM status."), .params = {l_target}, .run = cmdUncmOther},
        };
        f_registry.registerCommand(l_uncm, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("invite"), {cm_invite}, {l_target}, QStringLiteral("Invites a client to the area."), cmdInvite), l_core);
    // The CMs of an area are immune to its uninvites.
    f_registry.registerCommand(singleForm(QStringLiteral("uninvite"), {cm_uninvite}, {param::client(QStringLiteral("target")).immuneIfAreaOwner()}, QStringLiteral("Removes a client from the area invite list."), cmdUninvite), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("area_lock"), {cm_lock}, {}, QStringLiteral("Locks the area so only invited clients may enter."), cmdAreaLock, {QStringLiteral("lock_area"), QStringLiteral("lock")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("area_spectate"), {cm_spectate}, {}, QStringLiteral("Sets the area to spectate-only mode."), cmdAreaSpectate, {QStringLiteral("spectatable")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("area_unlock"), {cm_unlock}, {}, QStringLiteral("Unlocks the area."), cmdAreaUnlock, {QStringLiteral("unlock_area"), QStringLiteral("unlock")}), l_core);

    {
        // The cross-area form needs gamemaster AND kick - an all-of group,
        // declared instead of hidden in the handler body.
        CommandSpec l_area_kick;
        l_area_kick.name = QStringLiteral("area_kick");
        l_area_kick.aliases = {QStringLiteral("kick_area"), QStringLiteral("areakick")};
        l_area_kick.description = QStringLiteral("Kicks a client or all non-CMs from the area.");
        // A client in this area that is not one of its CMs - the CMs of an
        // area are immune to its kicks, as before.
        const Param l_target = param::client(QStringLiteral("target")).inArea().immuneIfAreaOwner();
        const Param l_all = param::word(QStringLiteral("who"), {QStringLiteral("all")});
        const Param l_destination = param::area(QStringLiteral("destination"));
        const Gate l_cross_area = Gate::allOf({area_cm, kick});
        l_area_kick.variants = {
            {.id = QStringLiteral("one"), .gate = {cm_kick}, .description = QStringLiteral("Kicks a client to the first area."), .params = {l_target}, .run = cmdAreaKickOne},
            {.id = QStringLiteral("all"), .gate = {cm_kick}, .description = QStringLiteral("Kicks everyone but the CMs to the first area."), .params = {l_all}, .run = cmdAreaKickAll},
            {.id = QStringLiteral("one_to"), .gate = l_cross_area, .description = QStringLiteral("Kicks a client to a specific area."), .params = {l_target, l_destination}, .run = cmdAreaKickOne},
            {.id = QStringLiteral("all_to"), .gate = l_cross_area, .description = QStringLiteral("Kicks everyone but the CMs to a specific area."), .params = {l_all, l_destination}, .run = cmdAreaKickAll},
        };
        f_registry.registerCommand(l_area_kick, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("background"), {area_background}, {param::text(QStringLiteral("background"))}, QStringLiteral("Changes the background of the area."), cmdBackground, {QStringLiteral("bg")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("side"), {cm_side}, {param::text(QStringLiteral("side")).optional()}, QStringLiteral("Locks or unlocks the background side."), cmdSide), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("lock_background"), {cm_lock_background}, {}, QStringLiteral("Locks the background in the area."), cmdLockBackground, {QStringLiteral("lock_bg"), QStringLiteral("lockbg"), QStringLiteral("bglock")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unlock_background"), {cm_unlock_background}, {}, QStringLiteral("Unlocks the background in the area."), cmdUnlockBackground, {QStringLiteral("unlock_bg"), QStringLiteral("unlockbg"), QStringLiteral("bgunlock")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("status"), {area_status}, {param::token(QStringLiteral("status"))}, QStringLiteral("Changes the status of the area."), cmdStatus), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("judgelog"), {cm_judgelog}, {}, QStringLiteral("Displays the judge log for the area."), cmdJudgeLog), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("ignore_bglist"), {ignore_background_list}, {}, QStringLiteral("Toggles whether the background list is enforced."), cmdIgnoreBgList, {QStringLiteral("ignorebglist")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("areamessage"), {cm_areamessage}, {param::text(QStringLiteral("message")).optional()}, QStringLiteral("Views or sets the area message."), cmdAreaMessage), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("togglemessage"), {cm_togglemessage}, {}, QStringLiteral("Toggles sending the area message on join."), cmdToggleMessage), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("clearmessage"), {cm_clearmessage}, {}, QStringLiteral("Clears the area message."), cmdClearMessage), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("toggle_wtce"), {cm_toggle_wtce}, {}, QStringLiteral("Toggles testimony animations in the area."), cmdToggleWtce, {QStringLiteral("togglewtce")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("toggle_shouts"), {cm_toggle_shouts}, {}, QStringLiteral("Toggles shouts in the area."), cmdToggleShouts, {QStringLiteral("toggleshouts")}), l_core);

    {
        // /floors with a floor visits it, as /floor does.
        CommandSpec l_floors;
        l_floors.name = QStringLiteral("floors");
        l_floors.description = QStringLiteral("Lists all floors and their areas.");
        l_floors.variants = {
            {.id = QStringLiteral("list"), .gate = {area_floors}, .description = QStringLiteral("Lists all floors and their areas."), .run = cmdFloors},
            {.id = QStringLiteral("visit"), .gate = {area_floors}, .description = QStringLiteral("Moves you to the first area on the given floor."), .params = {l_floor}, .run = cmdFloor},
        };
        f_registry.registerCommand(l_floors, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("floor"), {area_floor}, {l_floor}, QStringLiteral("Moves you to the first area on the given floor."), cmdFloor), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("createarea"), {modify_floors}, {l_name}, QStringLiteral("Creates a new area on this floor."), cmdCreateArea), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("createfloor"), {modify_floors}, {l_name}, QStringLiteral("Creates a new floor with one starter area."), cmdCreateFloor), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("renamearea"), {modify_floors}, {l_name}, QStringLiteral("Renames the area you are in."), cmdRenameArea), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("renamefloor"), {modify_floors}, {l_name}, QStringLiteral("Renames the floor you are on."), cmdRenameFloor), l_core);

    {
        // An area ID picks the area; anything else is read as its name.
        CommandSpec l_removearea;
        l_removearea.name = QStringLiteral("removearea");
        l_removearea.description = QStringLiteral("Removes an empty area; later area IDs shift down.");
        l_removearea.variants = {
            {.id = QStringLiteral("id"), .gate = {modify_floors}, .description = QStringLiteral("Removes the area with the given ID."), .params = {param::area(QStringLiteral("area"))}, .run = cmdRemoveArea},
            {.id = QStringLiteral("name"), .gate = {modify_floors}, .description = QStringLiteral("Removes the area with the given name."), .params = {l_name}, .run = cmdRemoveArea},
        };
        f_registry.registerCommand(l_removearea, l_core);
    }

    f_registry.registerCommand(singleForm(QStringLiteral("removefloor"), {modify_floors}, {l_floor}, QStringLiteral("Removes a floor whose areas are all empty."), cmdRemoveFloor), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("saveworld"), {modify_floors}, {}, QStringLiteral("Saves every floor, area and their rules to areas.json."), cmdSaveWorld, {QStringLiteral("saveareas")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("loadworld"), {modify_floors}, {}, QStringLiteral("Rebuilds every floor and area from areas.json."), cmdLoadWorld, {QStringLiteral("loadareas")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("savefloor"), {modify_floors}, {l_floor.optional()}, QStringLiteral("Saves one floor to its own file under config/floors."), cmdSaveFloor), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("loadfloor"), {modify_floors}, {l_name}, QStringLiteral("Loads a floor file, replacing a same-named floor or adding a new one."), cmdLoadFloor), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("webfiles"), {area_webfiles}, {}, QStringLiteral("Lists download links for iniswapped characters."), cmdWebfiles), l_core);
}

} // namespace akashi::commands
