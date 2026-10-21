#pragma once

namespace akashi {
class CommandRegistry;
class CommandContext;
class Args;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdPluginList(akashi::CommandContext &f_ctx, const akashi::Args &f_args);
void cmdPluginLoad(akashi::CommandContext &f_ctx, const akashi::Args &f_args);
void cmdPluginUnload(akashi::CommandContext &f_ctx, const akashi::Args &f_args);
void cmdPluginReload(akashi::CommandContext &f_ctx, const akashi::Args &f_args);

void registerPluginCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
