#include "commands/music_commands.h"

#include "akashi/helpers/parse.h"
#include "akashi/permissions.h"
#include "commands/moderation_commands.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/server_context.h"
#include "world/area.h"
#include "world/jukebox.h"

#include <limits>

namespace akashi::commands {

static QString reprimand(ServerContext *f_server, bool f_positive = false)
{
    if (f_positive)
        return f_server->praiseList().at(CommandContext::genRand(0, f_server->praiseList().size() - 1));
    else
        return f_server->reprimandsList().at(CommandContext::genRand(0, f_server->reprimandsList().size() - 1));
}

void cmdPlay(CommandContext &f_context, const Args &f_args)
{
    QString l_song = f_args.text(QStringLiteral("song"));
    akashi::ClientSession *l_self = f_context.self();
    if (l_self->isDjBlocked()) {
        f_context.reply("You are blocked from changing the music.");
        return;
    }
    if (l_song == "sin.mp3") {
        l_self->closeSocket();
        return;
    }
    if (l_song.contains('/') && !akashi::Jukebox::validateSong(l_song, f_context.server()->cdnList())) {
        f_context.reply("The song you tried to play is not from an approved CDN.");
        return;
    }
    // Free play is gated by the check_free_play floor rule.
    if (auto l_refusal = l_self->playMusic(l_song, akashi::MusicSource::PlayCommand, {}, {})) {
        f_context.reply(*l_refusal);
    }
}

void cmdPlayAmbience(CommandContext &f_context, const Args &f_args)
{
    akashi::ClientSession *l_self = f_context.self();
    if (l_self->isDjBlocked()) {
        f_context.reply("You are blocked from changing the ambience.");
        return;
    }
    QString l_song = f_args.text(QStringLiteral("song"));
    if (l_song.contains('/') && !akashi::Jukebox::validateSong(l_song, f_context.server()->cdnList())) {
        f_context.reply("The song you tried to play is not from an approved CDN.");
        return;
    }
    // Free play is gated by the check_free_play floor rule.
    if (auto l_refusal = l_self->playAmbience(l_song, akashi::MusicSource::PlayCommand)) {
        f_context.reply(*l_refusal);
    }
}

void cmdCurrentMusic(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (!l_area->currentMusic().isEmpty() && !l_area->currentMusic().contains("~stop.mp3"))
        f_context.reply("The current song is " + l_area->currentMusic() + " played by " + l_area->musicPlayerBy());
    else
        f_context.reply("There is no music playing.");
}

void cmdBlockDj(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::dj_blocked);
    if (!applySanctionUntil(f_context, l_target, akashi::sanction::dj_blocked, untilOf(f_args)))
        return;
    if (l_was_sanctioned)
        f_context.reply("That player is already DJ blocked!");
    else {
        f_context.reply("DJ blocked player.");
        l_target.reply("You were blocked from changing the music by a moderator. " + reprimand(f_context.server()));
    }
}

void cmdUnblockDj(CommandContext &f_context, const Args &f_args)
{
    TargetPlayer l_target = f_args.target(QStringLiteral("target"));
    const bool l_was_sanctioned = l_target.hasSanction(akashi::sanction::dj_blocked);
    liftSanction(f_context, l_target, akashi::sanction::dj_blocked);
    if (!l_was_sanctioned)
        f_context.reply("That player is not DJ blocked!");
    else {
        f_context.reply("DJ permissions restored to player.");
        l_target.reply("A moderator restored your music permissions. " + reprimand(f_context.server(), true));
    }
}

void cmdToggleMusic(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleMusic();
    QString l_state = l_area->isMusicAllowed() ? "allowed." : "disallowed.";
    f_context.reply("Music in this area is now " + l_state);
}

void cmdToggleJukebox(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    l_area->toggleJukebox();
    QString l_state = l_area->isJukeboxEnabled() ? "enabled." : "disabled.";
    f_context.replyToArea("The jukebox in this area has been " + l_state);
}

void cmdAddMusic(CommandContext &f_context, const Args &f_args)
{
    QString l_argv_string = f_args.text(QStringLiteral("song"));
    QStringList l_argv = l_argv_string.split(",");

    if (l_argv.size() >= 4) {
        f_context.reply("Too many arguments. Addition of song has failed.");
        return;
    }

    QString l_song_name = l_argv.value(0).trimmed();
    QString l_true_name = l_argv.size() >= 2 ? l_argv.value(1).trimmed() : l_song_name;
    // The duration is optional; left empty it means unknown (0), but a
    // mistyped one is refused instead of quietly becoming 0.
    int l_song_duration = 0;
    if (const QString l_duration_text = l_argv.value(2).trimmed(); !l_duration_text.isEmpty()) {
        const auto l_duration = helpers::parseInt(l_duration_text, QStringLiteral("song_duration"), 0, std::numeric_limits<int>::max());
        if (!l_duration) {
            f_context.reply(l_duration.error());
            return;
        }
        l_song_duration = *l_duration;
    }

    akashi::Jukebox *l_jukebox = f_context.server()->areaById(f_context.areaId())->jukebox();
    f_context.reply(l_jukebox->addSong({l_song_name, l_true_name, l_song_duration}));
}

void cmdAddMusicCategory(CommandContext &f_context, const Args &f_args)
{
    akashi::Jukebox *l_jukebox = f_context.server()->areaById(f_context.areaId())->jukebox();
    f_context.reply(l_jukebox->addCategory(f_args.text(QStringLiteral("name"))));
}

void cmdRemoveCustomMusic(CommandContext &f_context, const Args &f_args)
{
    akashi::Jukebox *l_jukebox = f_context.server()->areaById(f_context.areaId())->jukebox();
    bool l_success = l_jukebox->removeSong(f_args.text(QStringLiteral("name")));
    QString l_message = l_success ? "succeeded." : "failed.";
    f_context.reply("The removal of the entry has " + l_message);
}

void cmdToggleCustomMusic(CommandContext &f_context, const Args &)
{
    akashi::Jukebox *l_jukebox = f_context.server()->areaById(f_context.areaId())->jukebox();
    l_jukebox->resetToFloor();
    f_context.reply("Custom music list has been reset to the floor defaults.");
}

void cmdClearCustomMusic(CommandContext &f_context, const Args &)
{
    f_context.server()->areaById(f_context.areaId())->jukebox()->resetToFloor();
    f_context.reply("Custom songs have been cleared.");
}

void cmdJukeboxSkip(CommandContext &f_context, const Args &)
{
    QString l_name = f_context.character();
    if (!f_context.characterName().isEmpty()) {
        l_name = f_context.characterName();
    }

    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());

    if (l_area->isJukeboxEnabled()) {
        if (l_area->jukebox()->skip()) {
            f_context.replyToArea(l_name + " has forced a skip. Playing the next available song.");
            return;
        }
        f_context.reply("Unable to skip song. Jukebox is currently empty.");
        return;
    }
    f_context.reply("Unable to skip song. The jukebox is not running.");
}

void registerMusicCommands(CommandRegistry &f_registry)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");
    const Param l_song = param::text(QStringLiteral("song"));
    const Param l_name = param::text(QStringLiteral("name"));
    const Param l_target = param::client(QStringLiteral("target"));
    // Sanctions refuse staff who hold sanction.immune, unless the
    // moderator holds super.
    const Param l_sanctionable = param::client(QStringLiteral("target")).immuneIf(sanction_immune);

    f_registry.registerCommand(singleForm(QStringLiteral("play"), {music_play}, {l_song}, QStringLiteral("Plays a song in the area."), cmdPlay), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("play_ambience"), {music_play_ambience}, {l_song}, QStringLiteral("Plays ambient music in the area."), cmdPlayAmbience, {QStringLiteral("playambience"), QStringLiteral("playa")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("currentmusic"), {music_currentmusic}, {}, QStringLiteral("Shows the currently playing song in the area."), cmdCurrentMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("block_dj"), {sanction_block_dj}, {l_sanctionable, param::when(QStringLiteral("until")).optional()},
                                          QStringLiteral("Blocks a client from changing music, until a time like 1d12h if one is given."), cmdBlockDj, {QStringLiteral("blockdj")}),
                               l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("unblock_dj"), {sanction_block_dj}, {l_target}, QStringLiteral("Restores a client's music permissions."), cmdUnblockDj, {QStringLiteral("unblockdj")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("togglemusic"), {cm_togglemusic}, {}, QStringLiteral("Toggles music playing in the area."), cmdToggleMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("togglejukebox"), {music_jukebox}, {}, QStringLiteral("Toggles the jukebox in the area."), cmdToggleJukebox), l_core);
    // The song's fields are comma-separated within the text, so the text
    // stays one parameter: <name>[, truename][, duration].
    f_registry.registerCommand(singleForm(QStringLiteral("addmusic"), {cm_addmusic}, {l_song}, QStringLiteral("Adds a song to the custom music list: name, then optionally a true name and a duration, separated by commas."), cmdAddMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("addmusiccategory"), {cm_addmusiccategory}, {l_name}, QStringLiteral("Adds a category to the custom music list."), cmdAddMusicCategory), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("removecustommusic"), {cm_removecustommusic}, {l_name}, QStringLiteral("Removes a song or category from the custom list."), cmdRemoveCustomMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("togglecustommusic"), {cm_togglecustommusic}, {}, QStringLiteral("Resets the custom music list to floor defaults."), cmdToggleCustomMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("clearcustommusic"), {cm_clearcustommusic}, {}, QStringLiteral("Clears all custom songs in the area."), cmdClearCustomMusic), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("jukebox_skip"), {cm_jukebox_skip}, {}, QStringLiteral("Skips the current jukebox song."), cmdJukeboxSkip, {QStringLiteral("jukeboxskip")}), l_core);
}

} // namespace akashi::commands
