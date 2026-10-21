#pragma once

namespace akashi {
class CommandRegistry;
class Args;
class CommandContext;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdPlay(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdPlayAmbience(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdCurrentMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdBlockDj(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdUnblockDj(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleJukebox(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAddMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAddMusicCategory(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemoveCustomMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdToggleCustomMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdClearCustomMusic(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdJukeboxSkip(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerMusicCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
