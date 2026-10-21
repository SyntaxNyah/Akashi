#include "commands/roleplay_commands.h"

#include "akashi/helpers/parse.h"
#include "akashi/logging_categories.h"
#include "akashi/permissions.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/server_context.h"
#include "core/server_settings.h"
#include "proto/packet.h"
#include "proto/timer_packets.h"
#include "world/area.h"

#include <QTime>

namespace akashi::commands {

static void diceThrower(CommandContext &f_context, int f_sides, int f_dice, bool f_private, int f_modifier = 0)
{
    const int l_max_value = f_context.server()->serverSettings()->max_value();
    // The modifier is client-supplied; bound it to the same range as a face so
    // genRand(...) + f_modifier below cannot signed-overflow. The comparison
    // avoids qAbs(), which is itself UB at INT_MIN.
    if (f_sides < 0 || f_dice < 0 || f_sides > l_max_value || f_dice > f_context.server()->serverSettings()->max_dice() || f_modifier > l_max_value || f_modifier < -l_max_value) {
        f_context.reply("Dice or side number out of bounds.");
        return;
    }
    QStringList l_results;
    for (int i = 1; i <= f_dice; i++) {
        l_results.append(QString::number(CommandContext::genRand(1, f_sides) + f_modifier));
    }
    QString l_total = l_results.join(" ");
    if (f_private) {
        if (f_modifier)
            f_context.reply("You rolled a " + QString::number(f_dice) + "d" + QString::number(f_sides) + "+" + QString::number(f_modifier) + ". Results: " + l_total);
        else
            f_context.reply("You rolled a " + QString::number(f_dice) + "d" + QString::number(f_sides) + ". Results: " + l_total);
        return;
    }
    if (f_modifier)
        f_context.replyToArea(f_context.name() + " rolled a " + QString::number(f_dice) + "d" + QString::number(f_sides) + "+" + QString::number(f_modifier) + ". Results: " + l_total);
    else
        f_context.replyToArea(f_context.name() + " rolled a " + QString::number(f_dice) + "d" + QString::number(f_sides) + ". Results: " + l_total);
}

static QString areaTimer(CommandContext &f_context, int f_area_idx, int f_timer_idx)
{
    akashi::Area *l_area = f_context.server()->areaById(f_area_idx);
    QTimer *l_timer;
    QString l_name = (f_timer_idx == 0) ? "Global timer" : "Timer " + QString::number(f_timer_idx);
    if (f_timer_idx == 0)
        l_timer = f_context.server()->timer;
    else if (f_timer_idx > 0 && f_timer_idx <= 4)
        l_timer = l_area->timers().at(f_timer_idx - 1);
    else
        return "Invalid timer ID.";
    if (l_timer->isActive()) {
        QTime l_current = QTime(0, 0).addMSecs(l_timer->remainingTime());
        return l_name + " is at " + l_current.toString("hh:mm:ss.zzz");
    }
    return l_name + " is inactive.";
}

void cmdCoinFlip(CommandContext &f_context, const Args &)
{
    QString l_sender_name = f_context.name();
    QStringList l_faces = {"heads", "tails"};
    QString l_face = l_faces[CommandContext::genRand(0, 1)];
    f_context.replyToArea(l_sender_name + " flipped a coin and got " + l_face + ".");
}

// /roll and /rollp share one body: either dice notation ("2d6+1") or the
// plain form "[sides] [count]", both read within the server's limits.
static void rollDice(CommandContext &f_context, const Args &f_args, bool f_private)
{
    if (f_args.has(QStringLiteral("dice"))) {
        const helpers::Dice l_dice = f_args.dice(QStringLiteral("dice"));
        diceThrower(f_context, l_dice.sides, l_dice.count, f_private, l_dice.modifier);
        return;
    }
    const int l_sides = f_args.has(QStringLiteral("dice_sides")) ? int(f_args.number(QStringLiteral("dice_sides"))) : 6;
    const int l_dice = f_args.has(QStringLiteral("dice_count")) ? int(f_args.number(QStringLiteral("dice_count"))) : 1;
    diceThrower(f_context, l_sides, l_dice, f_private);
}

void cmdRoll(CommandContext &f_context, const Args &f_args)
{
    rollDice(f_context, f_args, false);
}

void cmdRollA(CommandContext &f_context, const Args &f_args)
{
    QString l_dice_name = f_args.text(QStringLiteral("name"));

    if (f_context.server()->diceFaces(l_dice_name).isEmpty()) {
        qCWarning(akashiCommands) << "Unknown dice.";
        f_context.reply("Unknown dice.");
    }
    else {
        QString l_response = f_context.server()->diceFaces(l_dice_name).at(CommandContext::genRand(0, f_context.server()->diceFaces(l_dice_name).size() - 1));
        QString l_sender_name = f_context.name();
        f_context.replyToArea(l_sender_name + " rolled from the \"" + l_dice_name + "\" set and got: " + l_response);
    }
}

void cmdRollP(CommandContext &f_context, const Args &f_args)
{
    rollDice(f_context, f_args, true);
}

void cmdTimer(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());

    if (!f_args.has(QStringLiteral("timer_id"))) {
        QStringList l_timers;
        l_timers.append("Currently active timers:");
        for (int i = 0; i <= 4; i++) {
            l_timers.append(areaTimer(f_context, l_area->id(), i));
        }
        f_context.reply(l_timers.join("\n"));
        return;
    }

    // Timer 0 is the server-wide one; altering it is a form of its own,
    // gated on timer.global as well.
    const int l_timer_id = int(f_args.number(QStringLiteral("timer_id")));

    if (!f_args.has(QStringLiteral("time")) && !f_args.has(QStringLiteral("action"))) {
        f_context.reply(areaTimer(f_context, l_area->id(), l_timer_id));
        return;
    }

    QTimer *l_requested_timer = l_timer_id == 0 ? f_context.server()->timer : l_area->timers().at(l_timer_id - 1);

    bool l_is_global = l_timer_id == 0;
    // Every timer packet leaves through the shared TI builders.
    auto l_send = [&f_context, l_is_global](const akashi::Packet &f_packet) {
        l_is_global ? f_context.server()->broadcast(f_packet) : f_context.server()->broadcast(f_packet, f_context.areaId());
    };

    if (f_args.has(QStringLiteral("time"))) {
        const QString l_time_text = f_args.text(QStringLiteral("time"));
        QTime l_requested_time = QTime::fromString(l_time_text, "hh:mm:ss");
        if (!l_requested_time.isValid()) {
            f_context.reply("Could not read \"" + l_time_text + "\" as a time. Use hh:mm:ss, like 00:05:00.");
            return;
        }
        l_requested_timer->setInterval(QTime(0, 0).msecsTo(l_requested_time));
        l_requested_timer->start();
        f_context.reply("Set timer " + QString::number(l_timer_id) + " to " + l_time_text + ".");
        l_send(akashi::timerShow(l_timer_id));
        l_send(akashi::timerValue(l_timer_id, QTime(0, 0).msecsTo(l_requested_time)));
        return;
    }

    const QString l_action = f_args.text(QStringLiteral("action"));
    if (l_action == "start") {
        l_requested_timer->start();
        f_context.reply("Started timer " + QString::number(l_timer_id) + ".");
        l_send(akashi::timerShow(l_timer_id));
        l_send(akashi::timerValue(l_timer_id, QTime(0, 0).msecsTo(QTime(0, 0).addMSecs(l_requested_timer->remainingTime()))));
    }
    else if (l_action == "pause" || l_action == "stop") {
        l_requested_timer->setInterval(l_requested_timer->remainingTime());
        l_requested_timer->stop();
        f_context.reply("Stopped timer " + QString::number(l_timer_id) + ".");
        l_send(akashi::timerValue(l_timer_id, QTime(0, 0).msecsTo(QTime(0, 0).addMSecs(l_requested_timer->interval())), true));
    }
    else if (l_action == "hide" || l_action == "unset") {
        l_requested_timer->setInterval(0);
        l_requested_timer->stop();
        f_context.reply("Hid timer " + QString::number(l_timer_id) + ".");
        l_send(akashi::timerHide(l_timer_id));
    }
}

void cmdNotecard(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    QString l_notecard = f_args.text(QStringLiteral("message"));
    l_area->addNotecard(f_context.character(), l_notecard);
    f_context.replyToArea(f_context.character() + " wrote a note card.");
}

void cmdNotecardClear(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    if (!l_area->addNotecard(f_context.character(), QString())) {
        f_context.replyToArea(f_context.character() + " erased their note card.");
    }
}

void cmdNotecardReveal(CommandContext &f_context, const Args &)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    const QStringList l_notecards = l_area->takeNotecards();

    if (l_notecards.isEmpty()) {
        f_context.reply("There are no cards to reveal in this area.");
        return;
    }

    QString l_message("Note cards have been revealed.\n");
    l_message.append(l_notecards.join(""));

    f_context.replyToArea(l_message);
}

void cmd8Ball(CommandContext &f_context, const Args &f_args)
{
    if (f_context.server()->magic8BallAnswers().isEmpty()) {
        qCWarning(akashiCommands) << "8ball.txt is empty!";
        f_context.reply("8ball.txt is empty.");
    }
    else {
        QString l_response = f_context.server()->magic8BallAnswers().at(CommandContext::genRand(1, f_context.server()->magic8BallAnswers().size() - 1));
        QString l_sender_name = f_context.name();
        QString l_sender_message = f_args.text(QStringLiteral("question"));
        f_context.replyToArea(l_sender_name + " asked the magic 8-ball, \"" + l_sender_message + "\" and the answer is: " + l_response);
    }
}

void cmdSubtheme(CommandContext &f_context, const Args &f_args)
{
    QString l_subtheme = f_args.text(QStringLiteral("name"));
    const QVector<akashi::ClientSession *> l_clients = f_context.server()->clients();
    for (akashi::ClientSession *l_client : l_clients) {
        if (l_client->areaId() == f_context.areaId())
            l_client->sendPacket("ST", {l_subtheme, "1"});
    }
    f_context.replyToArea("Subtheme was set to " + l_subtheme);
}

void registerRoleplayCommands(CommandRegistry &f_registry, const ServerSettings &f_settings)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");

    // The dice limits are read from the live settings each time a roll is
    // read, so a reload takes effect at once.
    const ServerSettings *l_settings = &f_settings;
    const Param::Limit l_max_dice = [l_settings](const ParamScope &) { return qint64(l_settings->max_dice()); };
    const Param::Limit l_max_sides = [l_settings](const ParamScope &) { return qint64(l_settings->max_value()); };
    // A bare roll is one six-sided die; "/roll 20 2" is two d20s; "/roll 2d20+1" is notation.
    const QList<Param> l_plain{param::integer(QStringLiteral("dice_sides"), 1, 0).max(l_max_sides).optional(),
                               param::integer(QStringLiteral("dice_count"), 1, 0).max(l_max_dice).optional()};
    const QList<Param> l_notation{param::dice(QStringLiteral("dice"), l_max_dice, l_max_sides)};

    f_registry.registerCommand(singleForm(QStringLiteral("coinflip"), {roleplay_coinflip}, {}, QStringLiteral("Flips a coin for the area to see."), cmdCoinFlip), l_core);
    {
        CommandSpec l_roll;
        l_roll.name = QStringLiteral("roll");
        l_roll.aliases = {QStringLiteral("r")};
        l_roll.description = QStringLiteral("Rolls dice for the area to see.");
        l_roll.variants = {
            {.id = QStringLiteral("plain"), .gate = {roleplay_roll}, .description = QStringLiteral("Rolls dice with that many sides, one six-sided die by default."), .params = l_plain, .run = cmdRoll},
            {.id = QStringLiteral("notation"), .gate = {roleplay_roll}, .description = QStringLiteral("Rolls dice notation such as 2d6+1."), .params = l_notation, .run = cmdRoll},
        };
        f_registry.registerCommand(l_roll, l_core);
    }
    f_registry.registerCommand(singleForm(QStringLiteral("rolla"), {roleplay_rolla}, {param::text(QStringLiteral("name"))}, QStringLiteral("Rolls a named die from the dice config."), cmdRollA), l_core);
    {
        CommandSpec l_rollp;
        l_rollp.name = QStringLiteral("rollp");
        l_rollp.description = QStringLiteral("Rolls dice only you and the moderators see.");
        l_rollp.variants = {
            {.id = QStringLiteral("plain"), .gate = {roleplay_rollp}, .description = QStringLiteral("Rolls dice with that many sides, one six-sided die by default."), .params = l_plain, .run = cmdRollP},
            {.id = QStringLiteral("notation"), .gate = {roleplay_rollp}, .description = QStringLiteral("Rolls dice notation such as 2d6+1."), .params = l_notation, .run = cmdRollP},
        };
        f_registry.registerCommand(l_rollp, l_core);
    }
    {
        // The ordinary forms take timers 1-4; altering timer 0, the
        // server-wide one, is its own form that also needs timer.global.
        // Viewing timer 0 needs nothing extra.
        const Param l_area_timer = param::integer(QStringLiteral("timer_id"), 1, 4);
        const Param l_global_timer = param::integer(QStringLiteral("timer_id"), 0, 0);
        const Param l_time = param::token(QStringLiteral("time"));
        const Param l_action = param::word(QStringLiteral("action"), {QStringLiteral("start"), QStringLiteral("pause"), QStringLiteral("stop"), QStringLiteral("hide"), QStringLiteral("unset")});
        const Gate l_global = Gate::allOf({cm_timer, timer_global});
        CommandSpec l_timer;
        l_timer.name = QStringLiteral("timer");
        l_timer.description = QStringLiteral("Shows or controls the area's timers.");
        l_timer.variants = {
            {.id = QStringLiteral("list"), .gate = {cm_timer}, .description = QStringLiteral("Shows every timer."), .params = {}, .run = cmdTimer},
            {.id = QStringLiteral("show"), .gate = {cm_timer}, .description = QStringLiteral("Shows one timer."), .params = {param::integer(QStringLiteral("timer_id"), 0, 4)}, .run = cmdTimer},
            {.id = QStringLiteral("action"), .gate = {cm_timer}, .description = QStringLiteral("Starts, pauses or hides an area timer."), .params = {l_area_timer, l_action}, .run = cmdTimer},
            {.id = QStringLiteral("set"), .gate = {cm_timer}, .description = QStringLiteral("Sets an area timer to a time like 00:05:00."), .params = {l_area_timer, l_time}, .run = cmdTimer},
            {.id = QStringLiteral("global_action"), .gate = l_global, .description = QStringLiteral("Starts, pauses or hides the server-wide timer."), .params = {l_global_timer, l_action}, .run = cmdTimer},
            {.id = QStringLiteral("global_set"), .gate = l_global, .description = QStringLiteral("Sets the server-wide timer to a time like 00:05:00."), .params = {l_global_timer, l_time}, .run = cmdTimer},
        };
        f_registry.registerCommand(l_timer, l_core);
    }
    f_registry.registerCommand(singleForm(QStringLiteral("notecard"), {roleplay_notecard}, {param::text(QStringLiteral("message"))}, QStringLiteral("Writes your hidden notecard."), cmdNotecard), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("notecard_clear"), {roleplay_notecard_clear}, {}, QStringLiteral("Clears your notecard."), cmdNotecardClear, {QStringLiteral("clear_notecard"), QStringLiteral("notecardclear")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("notecard_reveal"), {cm_notecard_reveal}, {}, QStringLiteral("Reveals every notecard in the area."), cmdNotecardReveal, {QStringLiteral("reveal_notecard"), QStringLiteral("notecardreveal")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("8ball"), {roleplay_8ball}, {param::text(QStringLiteral("question"))}, QStringLiteral("Asks the magic 8-ball a question."), cmd8Ball), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("subtheme"), {cm_subtheme}, {param::text(QStringLiteral("name"))}, QStringLiteral("Switches the area's client subtheme."), cmdSubtheme), l_core);
}

} // namespace akashi::commands
