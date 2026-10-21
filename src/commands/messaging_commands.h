#pragma once

namespace akashi {
class CommandRegistry;
class Args;
class CommandContext;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdPos(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdForcePos(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdG(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNeed(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSwitch(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRandomChar(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleGlobal(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdPM(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAnnounce(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdM(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdGM(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLM(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdGimp(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUngimp(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdDisemvowel(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnDisemvowel(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdShake(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnShake(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMedieval(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnmedieval(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMedievalMode(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdMutePM(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleAdverts(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAfk(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCharCurse(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnCharCurse(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCharSelect(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdForceCharSelect(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdA(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdS(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdFirstPerson(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerMessagingCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
