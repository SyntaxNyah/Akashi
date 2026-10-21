#pragma once

namespace akashi {
class Args;
class CommandContext;
class CommandRegistry;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdGetArea(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdGetAreas(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdArea(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCmClaim(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCmAdd(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCm(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUncmOwn(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUncmAll(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUncmOther(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdInvite(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUninvite(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAreaLock(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAreaSpectate(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAreaUnlock(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdBackground(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSide(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLockBackground(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnlockBackground(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdStatus(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdJudgeLog(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdIgnoreBgList(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAreaMessage(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleMessage(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdClearMessage(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleWtce(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleShouts(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdFloors(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCreateArea(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCreateFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRenameArea(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRenameFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemoveArea(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemoveFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSaveWorld(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLoadWorld(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSaveFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLoadFloor(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdWebfiles(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerAreaCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
