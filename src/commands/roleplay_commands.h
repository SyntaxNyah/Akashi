#pragma once

class ServerSettings;

namespace akashi {
class CommandRegistry;
class Args;
class CommandContext;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdCoinFlip(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRoll(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRollA(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRollP(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdTimer(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNotecard(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNotecardClear(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdNotecardReveal(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmd8Ball(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSubtheme(akashi::CommandContext &f_context, const akashi::Args &f_args);

// The dice limits read the live max_dice and max_value settings.
void registerRoleplayCommands(akashi::CommandRegistry &f_registry, const ServerSettings &f_settings);

} // namespace akashi::commands
