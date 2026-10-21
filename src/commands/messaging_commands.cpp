#include "commands/messaging_commands.h"

#include "akashi/event.h"
#include "akashi/helpers/parse.h"
#include "akashi/permissions.h"
#include "commands/moderation_commands.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/server_context.h"
#include "core/server_settings.h"
#include "proto/packet.h"
#include "proto/text_utils.h"
#include "world/area.h"

#include <QDateTime>

#include <optional>
#include <utility>

namespace akashi::commands {

static QString reprimand(ServerContext *f_server, bool f_positive = false)
{
    if (f_positive)
        return f_server->praiseList().at(CommandContext::genRand(0, f_server->praiseList().size() - 1));
    else
        return f_server->reprimandsList().at(CommandContext::genRand(0, f_server->reprimandsList().size() - 1));
}

// Announce a message that left the sender's area outside the placed ic/ooc
// events - global chat, adverts, PMs, the CM area-broadcasts. The placeless
// global_message_sent event lets observers like the automated moderator
// count it toward the same flood and repeat windows as ordinary chat.
static void announceGlobalMessage(CommandContext &f_context, const QString &f_channel, const QString &f_message)
{
    akashi::GlobalMessageEvent l_event;
    l_event.client_session_id = f_context.clientId();
    l_event.ipid = f_context.ipid();
    l_event.char_name = f_context.character();
    l_event.ooc_name = f_context.name();
    l_event.channel = f_channel;
    l_event.message = f_message;
    f_context.server()->publishEvent(akashi::GlobalMessageEvent::id, akashi::eventToMap(l_event),
                                     l_event.player_state_id, l_event.client_session_id);
}

void cmdPos(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->changePosition(f_args.text(QStringLiteral("position")));
}

// /forcepos <position> <target>, or * for everyone in the area.
void cmdForcePos(CommandContext &f_context, const Args &f_args)
{
    QList<akashi::ClientSession *> l_targets;
    int l_forced_clients = 0;
    const QString l_position = f_args.text(QStringLiteral("position"));
    if (akashi::ClientSession *l_target_client = f_args.client(QStringLiteral("target"))) {
        l_targets.append(l_target_client);
    }
    else {
        const QVector<akashi::ClientSession *> l_clients = f_context.server()->clients();
        for (akashi::ClientSession *l_client : l_clients) {
            if (l_client->areaId() == f_context.areaId())
                l_targets.append(l_client);
        }
    }
    for (akashi::ClientSession *l_target : l_targets) {
        l_target->sendServerMessage("Position forcibly changed by CM.");
        l_target->changePosition(l_position);
        l_forced_clients++;
    }
    // The summary echoes what was actually stored, not the raw argument.
    f_context.reply("Forced " + QString::number(l_forced_clients) + " into pos " + akashi::sanitizePosition(l_position) + ".");
}

void cmdG(CommandContext &f_context, const Args &f_args)
{
    QString l_sender_name = f_context.name();
    QString l_sender_area = f_context.areaName();
    QString l_sender_message = f_args.text(QStringLiteral("message"));
    akashi::Packet l_mod_packet("CT", {"[G][" + f_context.ipid() + "][" + l_sender_area + "]" + l_sender_name, l_sender_message});
    akashi::Packet l_user_packet("CT", {"[G][" + l_sender_area + "]" + l_sender_name, l_sender_message});
    f_context.server()->broadcast(l_user_packet, l_mod_packet, ServerContext::TARGET_TYPE::AUTHENTICATED);
    announceGlobalMessage(f_context, QStringLiteral("global"), l_sender_message);
}

void cmdNeed(CommandContext &f_context, const Args &f_args)
{
    QString l_sender_area = f_context.areaName();
    QString l_sender_message = f_args.text(QStringLiteral("message"));
    f_context.server()->broadcast(akashi::Packet("CT", {f_context.server()->serverNickname(), "=== Advert ===\n[" + l_sender_area + "] needs " + l_sender_message + "."}), ServerContext::TARGET_TYPE::ADVERT);
    announceGlobalMessage(f_context, QStringLiteral("advert"), l_sender_message);
}

void cmdSwitch(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_self = f_context.self();
    int l_selected_char_id = f_context.server()->characterId(f_args.text(QStringLiteral("character")));
    if (l_selected_char_id == -1) {
        f_context.reply("That does not look like a valid character.");
        return;
    }
    if (auto l_refused = l_self->takeCharacter(l_selected_char_id)) {
        // A rule speaks its own reason; the mechanism refuses silently and
        // keeps this door's traditional answer.
        f_context.reply(l_refused->isEmpty() ? QStringLiteral("The character you picked is either taken or invalid.") : *l_refused);
    }
}

void cmdRandomChar(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());

    // Collect the free characters in one pass. The old retry loop drew random
    // ids until one was free, which never terminated once every character was
    // taken - one command could hang the whole single-threaded server.
    const QList<int> l_taken = l_area->charactersTaken();
    QList<int> l_free;
    const int l_count = f_context.server()->characterCount();
    for (int i = 0; i < l_count; i++) {
        if (!l_taken.contains(i)) {
            l_free.append(i);
        }
    }
    if (l_free.isEmpty()) {
        f_context.reply("Every character in this area is already taken.");
        return;
    }

    const int l_selected_char_id = l_free.at(CommandContext::genRand(0, l_free.size() - 1));
    if (auto l_refused = l_self->takeCharacter(l_selected_char_id); l_refused && !l_refused->isEmpty()) {
        f_context.reply(*l_refused);
    }
}

void cmdToggleGlobal(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->setGlobalEnabled(!l_self->isGlobalEnabled());
    QString l_str_en = l_self->isGlobalEnabled() ? "shown" : "hidden";
    f_context.reply("Global chat set to " + l_str_en);
}

// /pm <target> <message>: the target is a connected client and the
// message is the rest of the line, both already checked.
void cmdPM(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_target_client = f_args.client(QStringLiteral("target"));
    if (l_target_client->isPmMuted()) {
        f_context.reply("That user is not recieving PMs.");
        return;
    }
    const QString l_message = f_args.text(QStringLiteral("message"));
    l_target_client->sendServerMessage("Message from " + f_context.name() + " (" + QString::number(f_context.clientId()) + "): " + l_message);
    f_context.reply("PM sent to " + QString::number(l_target_client->clientId()) + ". Message: " + l_message);
    announceGlobalMessage(f_context, QStringLiteral("pm"), l_message);
}

void cmdAnnounce(CommandContext &f_context, const Args &f_args)
{
    f_context.replyToServer("=== Announcement ===\r\n" + f_args.text(QStringLiteral("message")) + "\r\n=============");
}

void cmdM(CommandContext &f_context, const Args &f_args)
{
    QString l_sender_name = f_context.name();
    QString l_sender_message = f_args.text(QStringLiteral("message"));
    f_context.server()->broadcast(akashi::Packet("CT", {"[M]" + l_sender_name, l_sender_message}), ServerContext::TARGET_TYPE::MODCHAT);
}

void cmdGM(CommandContext &f_context, const Args &f_args)
{
    QString l_sender_name = f_context.name();
    QString l_sender_area = f_context.areaName();
    QString l_sender_message = f_args.text(QStringLiteral("message"));
    f_context.server()->broadcast(akashi::Packet("CT", {"[G][" + l_sender_area + "]" + "[" + l_sender_name + "][M]", l_sender_message}), ServerContext::TARGET_TYPE::MODCHAT);
}

void cmdLM(CommandContext &f_context, const Args &f_args)
{
    QString l_sender_name = f_context.name();
    QString l_sender_message = f_args.text(QStringLiteral("message"));
    f_context.server()->broadcast(akashi::Packet("CT", {"[" + l_sender_name + "][M]", l_sender_message}), f_context.areaId());
}

void cmdGimp(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::gimped);
    if (!applySanctionUntil(f_context, l_target, sanction::gimped, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already gimped!");
    else {
        f_context.reply("Gimped player.");
        l_target.reply("You have been gimped! " + reprimand(f_context.server()));
    }
}

void cmdUngimp(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::gimped);
    liftSanction(f_context, l_target, sanction::gimped);
    if (!l_was_sanctioned)
        f_context.reply("That player is not gimped!");
    else {
        f_context.reply("Ungimped player.");
        l_target.reply("A moderator has ungimped you! " + reprimand(f_context.server(), true));
    }
}

void cmdDisemvowel(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::disemvoweled);
    if (!applySanctionUntil(f_context, l_target, sanction::disemvoweled, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already disemvoweled!");
    else {
        f_context.reply("Disemvoweled player.");
        l_target.reply("You have been disemvoweled! " + reprimand(f_context.server()));
    }
}

void cmdUnDisemvowel(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::disemvoweled);
    liftSanction(f_context, l_target, sanction::disemvoweled);
    if (!l_was_sanctioned)
        f_context.reply("That player is not disemvoweled!");
    else {
        f_context.reply("Undisemvoweled player.");
        l_target.reply("A moderator has undisemvoweled you! " + reprimand(f_context.server(), true));
    }
}

void cmdShake(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::shaken);
    if (!applySanctionUntil(f_context, l_target, sanction::shaken, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already shaken!");
    else {
        f_context.reply("Shook player.");
        l_target.reply("A moderator has shaken your words! " + reprimand(f_context.server()));
    }
}

void cmdUnShake(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::shaken);
    liftSanction(f_context, l_target, sanction::shaken);
    if (!l_was_sanctioned)
        f_context.reply("That player is not shaken!");
    else {
        f_context.reply("Unshook player.");
        l_target.reply("A moderator has unshook you! " + reprimand(f_context.server(), true));
    }
}

void cmdMedieval(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::medieval);
    if (!applySanctionUntil(f_context, l_target, sanction::medieval, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already speaking Ye Olde English!");
    else {
        f_context.reply("It is done, sire.");
        l_target.reply("Forsooth! Thine speech will henceforth be Ye Olde!");
    }
}

void cmdUnmedieval(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(sanction::medieval);
    liftSanction(f_context, l_target, sanction::medieval);
    if (!l_was_sanctioned)
        f_context.reply("That player is not speaking Ye Olde English!");
    else {
        f_context.reply("Un-medieval'd player.");
        l_target.reply("Hark! Thine speech hast been returneth to normal.");
    }
}

void cmdMedievalMode(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.area();
    l_area->toggleMedievalMode();
    const QString l_state = l_area->isMedievalMode() ? "enabled." : "disabled.";
    f_context.replyToArea("Hear ye, hear ye! Medieval Mode is now " + l_state);
}

void cmdMutePM(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->setPmMuted(!l_self->isPmMuted());
    QString l_str_en = l_self->isPmMuted() ? "muted" : "unmuted";
    f_context.reply("PM's are now " + l_str_en);
}

void cmdToggleAdverts(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->setAdvertEnabled(!l_self->isAdvertEnabled());
    QString l_str_en = l_self->isAdvertEnabled() ? "on" : "off";
    f_context.reply("Advertisements turned " + l_str_en);
}

void cmdAfk(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->setAfk(true);
    f_context.reply("You are now AFK.");
    l_self->setCharacterName(l_self->characterName() + " [AFK]");
}

// /charcurse <target> [characters...]: the target is already checked,
// staff protection included; the characters are a comma-separated list.
void cmdCharCurse(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    if (l_target->isCharCursed()) {
        f_context.reply("That player is already charcursed!");
        return;
    }

    QStringList l_char_names;
    if (!f_args.has(QStringLiteral("characters"))) {
        l_char_names << l_target->character();
    }
    else {
        l_char_names = f_args.text(QStringLiteral("characters")).split(",");
        for (const QString &l_char_name : std::as_const(l_char_names)) {
            if (f_context.server()->characterId(l_char_name) == -1) {
                f_context.reply("Could not find character: " + l_char_name);
                return;
            }
        }
    }

    // The name list travels as the stored payload, so the curse comes
    // back with its list on reconnect.
    const QString l_moderator = f_context.server()->authType() == AuthType::ADVANCED
                                    ? f_context.moderatorName()
                                    : QStringLiteral("moderator");
    akashi::ClientSession *l_issuer = f_context.self();
    const QList<akashi::ClientSession *> l_affected = f_context.server()->applySanction(
        l_target->ipid(), l_target->hwid(), sanction::charcurse, l_moderator, std::nullopt, l_char_names.join(","),
        l_issuer == l_target ? nullptr : l_issuer);

    // The refresh shows each cursed window its narrowed character list; a
    // window caught outside the list goes back to the select screen.
    for (akashi::ClientSession *l_session : l_affected) {
        f_context.server()->updateCharsTaken(f_context.server()->areaById(l_session->areaId()));
        if (!l_session->charCurseList().contains(f_context.server()->characterId(l_session->character()))) {
            l_session->takeCharacter(-1, true);
        }
    }

    l_target->sendServerMessage("You have been charcursed!");
    f_context.reply("Charcursed player.");
}

void cmdUnCharCurse(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_target = f_args.client(QStringLiteral("target"));
    if (!l_target->isCharCursed()) {
        f_context.reply("That player is not charcursed!");
        return;
    }
    const QList<akashi::ClientSession *> l_affected = f_context.server()->removeSanction(l_target->ipid(), sanction::charcurse);
    for (akashi::ClientSession *l_session : l_affected) {
        f_context.server()->updateCharsTaken(f_context.server()->areaById(l_session->areaId()));
    }
    f_context.reply("Uncharcursed player.");
    l_target->sendServerMessage("You were uncharcursed.");
}

void cmdCharSelect(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->takeCharacter(-1, true);
}

void cmdForceCharSelect(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    l_target.forceCharacterSelect();
    f_context.reply("Client has been forced into character select!");
}

// /a <area> <message>: the area is already checked to exist.
void cmdA(CommandContext &f_context, const Args &f_args)
{
    const int l_area_id = f_args.number(QStringLiteral("area"));
    akashi::Area *l_area = f_context.server()->areaById(l_area_id);
    if (!l_area->owners().contains(f_context.clientId())) {
        f_context.reply("You are not CM in that area.");
        return;
    }

    QString l_sender_name = f_context.name();
    QString l_ooc_message = f_args.text(QStringLiteral("message"));
    f_context.server()->broadcast(akashi::Packet("CT", {"[CM]" + l_sender_name, l_ooc_message}), l_area_id);
    announceGlobalMessage(f_context, QStringLiteral("cm"), l_ooc_message);
}

void cmdS(CommandContext &f_context, const Args &f_args)
{
    int l_all_areas = f_context.server()->areaCount() - 1;
    QString l_sender_name = f_context.name();
    QString l_ooc_message = f_args.text(QStringLiteral("message"));

    for (int i = 0; i <= l_all_areas; i++) {
        if (f_context.server()->areaById(i)->owners().contains(f_context.clientId()))
            f_context.server()->broadcast(akashi::Packet("CT", {"[CM]" + l_sender_name, l_ooc_message}), i);
    }
    announceGlobalMessage(f_context, QStringLiteral("cm"), l_ooc_message);
}

void cmdFirstPerson(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_self = f_context.self();
    l_self->setFirstPerson(!l_self->isFirstPerson());
    QString l_str_en = l_self->isFirstPerson() ? "enabled" : "disabled";
    f_context.reply("First person mode " + l_str_en + ".");
}

void registerMessagingCommands(CommandRegistry &f_registry)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");
    const Param l_message = param::text(QStringLiteral("message"));
    const Param l_target = param::client(QStringLiteral("target"));
    // Sanctions refuse staff who hold sanction.immune, unless the
    // moderator holds super.
    const Param l_sanctionable = param::client(QStringLiteral("target")).immuneIf(sanction_immune);
    const Param l_until = param::when(QStringLiteral("until")).optional();

    f_registry.registerCommand(singleForm(QStringLiteral("pos"), {characters_pos}, {param::token(QStringLiteral("position"))}, QStringLiteral("Moves you to a position in the area (def, pro, wit, jud, ...)."), cmdPos), l_core);
    {
        CommandSpec l_forcepos;
        l_forcepos.name = QStringLiteral("forcepos");
        l_forcepos.description = QStringLiteral("Forces a client to a position.");
        const Param l_position = param::token(QStringLiteral("position"));
        l_forcepos.variants = {
            {.id = QStringLiteral("one"), .gate = {cm_forcepos}, .description = QStringLiteral("Forces one client."), .params = {l_position, l_target}, .run = cmdForcePos},
            {.id = QStringLiteral("all"), .gate = {cm_forcepos}, .description = QStringLiteral("Forces everyone in the area."), .params = {l_position, param::word(QStringLiteral("target"), {QStringLiteral("*")})}, .run = cmdForcePos},
        };
        f_registry.registerCommand(l_forcepos, l_core);
    }
    f_registry.registerCommand(singleForm(QStringLiteral("g"), {messaging_g}, {l_message}, QStringLiteral("Sends a message to the global chat."), cmdG), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("need"), {messaging_need}, {l_message}, QStringLiteral("Sends a player advert to everyone with adverts enabled."), cmdNeed), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("switch"), {characters_switch}, {param::text(QStringLiteral("character"))}, QStringLiteral("Switches you to the named character."), cmdSwitch), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("randomchar"), {characters_randomchar}, {}, QStringLiteral("Picks a random character for you."), cmdRandomChar), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("toggleglobal"), {messaging_toggleglobal}, {}, QStringLiteral("Toggles whether you receive global chat."), cmdToggleGlobal), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("pm"), {messaging_pm}, {l_target, l_message}, QStringLiteral("Sends a private message to a client."), cmdPM), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("announce"), {announcer}, {l_message}, QStringLiteral("Sends an announcement to the whole server."), cmdAnnounce), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("m"), {chat_moderator}, {l_message}, QStringLiteral("Sends a message to the moderator chat."), cmdM), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("gm"), {chat_moderator}, {l_message}, QStringLiteral("Sends a global message tagged as a moderator."), cmdGM), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("lm"), {chat_moderator}, {l_message}, QStringLiteral("Sends an area message tagged as a moderator."), cmdLM), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("gimp"), {sanction_gimp}, {l_sanctionable, l_until}, QStringLiteral("Replaces a client's IC messages with canned lines, until a time like 1d12h if one is given."), cmdGimp), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("ungimp"), {sanction_gimp}, {l_target}, QStringLiteral("Lifts a client's gimp."), cmdUngimp), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("disemvowel"), {sanction_disemvowel}, {l_sanctionable, l_until}, QStringLiteral("Strips the vowels from a client's IC messages, until a time like 1d12h if one is given."), cmdDisemvowel), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("undisemvowel"), {sanction_disemvowel}, {l_target}, QStringLiteral("Gives a client their vowels back."), cmdUnDisemvowel), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("shake"), {sanction_shake}, {l_sanctionable, l_until}, QStringLiteral("Shuffles the words of a client's IC messages, until a time like 1d12h if one is given."), cmdShake), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unshake"), {sanction_shake}, {l_target}, QStringLiteral("Stops shuffling a client's IC messages."), cmdUnShake), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("medieval"), {sanction_medieval}, {l_sanctionable, l_until}, QStringLiteral("Makes a client speak in medieval English, until a time like 1d12h if one is given."), cmdMedieval), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unmedieval"), {sanction_medieval}, {l_target}, QStringLiteral("Returns a client to plain speech."), cmdUnmedieval), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("medievalmode"), {sanction_medieval}, {}, QStringLiteral("Toggles medieval mode in the area."), cmdMedievalMode, {QStringLiteral("medieval_mode")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("mutepm"), {messaging_mutepm}, {}, QStringLiteral("Toggles whether you receive private messages."), cmdMutePM), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("toggleadverts"), {messaging_toggleadverts}, {}, QStringLiteral("Toggles whether you receive player adverts."), cmdToggleAdverts), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("afk"), {messaging_afk}, {}, QStringLiteral("Marks you as away."), cmdAfk), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("charcurse"), {sanction_charcurse}, {l_sanctionable, param::text(QStringLiteral("characters")).optional()}, QStringLiteral("Restricts a client to the listed characters, separated by commas."), cmdCharCurse), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("uncharcurse"), {sanction_charcurse}, {l_target}, QStringLiteral("Lifts a client's character restriction."), cmdUnCharCurse), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("charselect"), {characters_charselect}, {}, QStringLiteral("Returns you to character select."), cmdCharSelect), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("force_charselect"), {force_charselect}, {l_target}, QStringLiteral("Forces a client back to character select."), cmdForceCharSelect, {QStringLiteral("forcecharselect")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("a"), {messaging_a}, {param::area(QStringLiteral("area")), l_message}, QStringLiteral("Sends a message to an area you own."), cmdA), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("s"), {messaging_s}, {l_message}, QStringLiteral("Sends a message to every area you own."), cmdS), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("firstperson"), {messaging_firstperson}, {}, QStringLiteral("Toggles first-person mode; your emotes stay hidden from others."), cmdFirstPerson), l_core);
}

} // namespace akashi::commands
