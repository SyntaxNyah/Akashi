#pragma once

namespace akashi {
class Args;
class CommandContext;
class CommandRegistry;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdDoc(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdClearDoc(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdEvidenceMod(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdEvidenceSwap(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdTestify(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdExamine(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdTestimony(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdDelete(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUpdate(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdPause(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAdd(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSaveTestimony(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLoadTestimony(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerCasingCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
