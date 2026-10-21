#pragma once

class ServerSettings;

class QString;

namespace akashi {
class Args;
class CommandRegistry;
class CommandContext;
}

namespace akashi::commands {

// The command handlers this file exports to the registry.
void cmdLogin(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdAddUser(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemoveUser(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdListPermsSelf(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdListPermsUser(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdSetPerms(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdRemovePerms(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdListUsers(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdLogout(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdChangePassSelf(akashi::CommandContext &f_context, const akashi::Args &f_args);
void cmdChangePassUser(akashi::CommandContext &f_context, const akashi::Args &f_args);

void registerAuthenticationCommands(akashi::CommandRegistry &f_registry);

// True when the password satisfies the owner-configured requirements.
bool passwordMeetsRequirements(ServerSettings *f_settings, const QString &f_username, const QString &f_password);

} // namespace akashi::commands
