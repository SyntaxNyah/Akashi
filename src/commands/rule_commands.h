#pragma once

namespace akashi {
class Args;
class CommandContext;
class CommandRegistry;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdRules(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRuleActions(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAddRule(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemoveRule(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdFloorRuleAdd(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdFloorRuleRemove(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdReloadRules(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerRuleCommands(akashi::CommandRegistry &f_registry);

} // namespace akashi::commands
