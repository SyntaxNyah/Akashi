#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace akashi {
class CommandRegistry;
class Args;
class CommandContext;
class TargetPlayer;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdBan(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdKick(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMods(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCommands(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdHelp(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMotd(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSetMotd(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdBans(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnban(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLiftSanction(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAbout(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMute(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnmute(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdOocMute(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdOocUnmute(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdBlockWtce(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnblockWtce(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAllowBlankposting(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdBanInfo(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdReload(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdForceNointPres(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAllowIniswap(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdPermitSaving(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdKickUid(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUpdateBan(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNotice(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNoticeG(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdKickOther(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdWhy(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdDumpGrants(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerModerationCommands(akashi::CommandRegistry &f_registry);

// Places a sanction through the server's one door: stored under the
// target's IPID and HWID so it survives reconnects and restarts, flagged
// on every window the person owns, lifting itself at f_until if one is
// given - without one it holds until lifted by hand. f_data is the
// sanction's payload, if it has one.
bool applySanctionUntil(akashi::CommandContext &f_context, akashi::TargetPlayer &f_target, const QString &f_sanction_id, const std::optional<QDateTime> &f_until, const QString &f_data = QString());

// The optional "until" parameter of a sanction command, already read.
std::optional<QDateTime> untilOf(const akashi::Args &f_args);

// Lifts a sanction by hand: the stored row, the pending lift and the
// session flags of everyone it covered all clear.
void liftSanction(akashi::CommandContext &f_context, akashi::TargetPlayer &f_target, const QString &f_sanction_id);

} // namespace akashi::commands
