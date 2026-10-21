#include "commands/authentication_commands.h"

#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/crypto_helper.h"
#include "core/db_manager.h"
#include "core/permission_registry.h"
#include "core/server_context.h"
#include "core/server_settings.h"

#include <QRegularExpression>
#include <QSet>

namespace akashi::commands {

static QString decodeMessage(const QString &f_message)
{
    return QString(f_message)
        .replace("<num>", "#")
        .replace("<percent>", "%")
        .replace("<dollar>", "$")
        .replace("<and>", "&");
}

bool passwordMeetsRequirements(ServerSettings *f_settings, const QString &f_username, const QString &f_password)
{
    if (!f_settings->password_requirements())
        return true;

    if (f_settings->pass_min_length() > f_password.size())
        return false;

    if (f_settings->pass_max_length() < f_password.size() && f_settings->pass_max_length() != 0)
        return false;

    if (f_settings->pass_required_mix_case()) {
        if (f_password.toLower() == f_password)
            return false;
        if (f_password.toUpper() == f_password)
            return false;
    }

    if (f_settings->pass_required_numbers()) {
        static const QRegularExpression s_numbers(QStringLiteral("[0123456789]"));
        QRegularExpressionMatch l_match = s_numbers.match(f_password);
        if (!l_match.hasMatch())
            return false;
    }

    if (f_settings->pass_required_special()) {
        static const QRegularExpression s_special(QStringLiteral("[~!@#$%^&*_\\-+=`|\\\\(){}\\[\\]:;\"'<>,.?/]"));
        QRegularExpressionMatch l_match = s_special.match(f_password);
        if (!l_match.hasMatch())
            return false;
    }

    if (!f_settings->pass_can_contain_username()) {
        if (f_password.contains(f_username))
            return false;
    }

    return true;
}

// The command-facing form: OOC packets escape special characters, so the
// password is decoded before the checks.
static bool checkPasswordRequirements(ServerSettings *f_settings, const QString &f_username, const QString &f_password)
{
    return passwordMeetsRequirements(f_settings, f_username, decodeMessage(f_password));
}

void cmdLogin(CommandContext &f_context, const Args &)
{
    if (f_context.isAuthenticated()) {
        f_context.reply("You are already logged in!");
        return;
    }

    const QString l_system_id = f_context.server()->activeAuthSystemId();
    if (l_system_id == QStringLiteral("password")) {
        if (f_context.server()->serverSettings()->modpass().isEmpty()) {
            f_context.reply("No modpass is set. Please set a modpass before logging in.");
            return;
        }
        f_context.reply("Entering login prompt.\nPlease enter the server modpass.");
        f_context.setInLoginPrompt(true);
        return;
    }
    if (l_system_id == QStringLiteral("username")) {
        f_context.reply("Entering login prompt.\nPlease enter your username and password.");
        f_context.setInLoginPrompt(true);
        return;
    }

    // The prompt only speaks the core dialects; other systems log in
    // through the AUTH packet.
    f_context.reply("This server uses " + l_system_id + " authentication.");
}

void cmdAddUser(CommandContext &f_context, const Args &f_args)
{
    const QString l_username = f_args.token(QStringLiteral("username"));
    const QString l_password = f_args.token(QStringLiteral("password"));
    if (!checkPasswordRequirements(f_context.server()->serverSettings(), l_username, l_password)) {
        f_context.reply("Password does not meet server requirements.");
        return;
    }

    QByteArray l_salt = CryptoHelper::randbytes(16);

    if (f_context.server()->databaseManager()->createUser(l_username, l_salt, l_password, ACLRolesHandler::NONE_ID))
        f_context.reply("Created user " + l_username + ".\nUse /setperms to modify their permissions.");
    else
        f_context.reply("Unable to create user " + l_username + ".\nDoes a user with that name already exist?");
}

void cmdRemoveUser(CommandContext &f_context, const Args &f_args)
{
    const QString l_username = f_args.token(QStringLiteral("username"));
    if (f_context.server()->databaseManager()->deleteUser(l_username))
        f_context.reply("Successfully removed user " + l_username + ".");
    else
        f_context.reply("Unable to remove user " + l_username + ".\nDoes it exist?");
}

static void replyRolePermissions(CommandContext &f_context, const QString &f_header, const ACLRole &f_role)
{
    QStringList l_message{f_header};

    const QSet<QString> &l_permissions = f_role.permissions();
    if (l_permissions.isEmpty()) {
        l_message.append("NONE");
    }
    else if (f_role.canPerform(permission::super)) {
        l_message.append("SUPER (Be careful! This grants the user all permissions.)");
    }
    else {
        QStringList l_sorted(l_permissions.begin(), l_permissions.end());
        l_sorted.sort();
        l_message.append(l_sorted);
    }

    f_context.reply(l_message.join("\n"));
}

void cmdListPermsSelf(CommandContext &f_context, const Args &)
{
    const ACLRole l_role = f_context.server()->aclRolesHandler()->roleById(f_context.aclRoleId());
    replyRolePermissions(f_context, "You have been given the following permissions:", l_role);
}

void cmdListPermsUser(CommandContext &f_context, const Args &f_args)
{
    const QString l_username = f_args.token(QStringLiteral("username"));
    const QString l_acl_id = f_context.server()->databaseManager()->acl(l_username);
    if (l_acl_id.isEmpty()) {
        f_context.reply(l_username + " wasn't found!");
        return;
    }

    const ACLRole l_role = f_context.server()->aclRolesHandler()->roleById(l_acl_id);
    replyRolePermissions(f_context, "User " + l_username + " has the following permissions:", l_role);
}

// The role arrives checked and in its canonical spelling: the role
// parameter reads "super" as SUPER, and only the form gated on super
// accepts SUPER at all.
void cmdSetPerms(CommandContext &f_context, const Args &f_args)
{
    const QString l_target_acl = f_args.role(QStringLiteral("role"));
    const QString l_target_username = f_args.token(QStringLiteral("username"));
    if (l_target_username == "root") {
        f_context.reply("You can't change root's role!");
        return;
    }

    if (f_context.server()->databaseManager()->updateACL(l_target_username, l_target_acl))
        f_context.reply("Successfully applied role " + l_target_acl + " to user " + l_target_username);
    else
        f_context.reply(l_target_username + " wasn't found!");
}

void cmdRemovePerms(CommandContext &f_context, const Args &f_args)
{
    const QString l_username = f_args.token(QStringLiteral("username"));
    if (l_username == "root") {
        f_context.reply("You can't change root's role!");
        return;
    }

    if (f_context.server()->databaseManager()->updateACL(l_username, ACLRolesHandler::NONE_ID))
        f_context.reply("Successfully applied role " + ACLRolesHandler::NONE_ID + " to user " + l_username);
    else
        f_context.reply(l_username + " wasn't found!");
}

void cmdListUsers(CommandContext &f_context, const Args &)
{
    QStringList l_users = f_context.server()->databaseManager()->users();
    f_context.reply("All users:\n" + l_users.join("\n"));
}

void cmdLogout(CommandContext &f_context, const Args &)
{
    if (!f_context.isAuthenticated()) {
        f_context.reply("You are not logged in!");
        return;
    }

    f_context.setAuthenticated(false);
    f_context.setAclRoleId(QString());
    f_context.setModeratorName(QString());
    f_context.sendPacket(QStringLiteral("AUTH"), {QStringLiteral("-1")});
    f_context.reply("Logged out.");
}

static void changePassword(CommandContext &f_context, const QString &f_username, const QString &f_password)
{
    if (!checkPasswordRequirements(f_context.server()->serverSettings(), f_username, f_password)) {
        f_context.reply("Password does not meet server requirements.");
        return;
    }

    if (f_context.server()->databaseManager()->updatePassword(f_username, f_password))
        f_context.reply("Successfully changed password.");
    else
        f_context.reply("There was an error changing the password.");
}

void cmdChangePassSelf(CommandContext &f_context, const Args &f_args)
{
    if (f_context.moderatorName().isEmpty()) {
        f_context.reply("You do not have permission to use that command. You must be logged in.");
        return;
    }
    changePassword(f_context, f_context.moderatorName(), f_args.token(QStringLiteral("password")));
}

void cmdChangePassUser(CommandContext &f_context, const Args &f_args)
{
    changePassword(f_context, f_args.token(QStringLiteral("username")), f_args.token(QStringLiteral("password")));
}

void registerAuthenticationCommands(CommandRegistry &f_registry)
{
    const Param l_username = param::token(QStringLiteral("username"));
    const Param l_password = param::token(QStringLiteral("password")).sensitive();

    f_registry.registerCommand(singleForm(QStringLiteral("login"), {akashi::permission::user}, {}, QStringLiteral("Begins the login process."), cmdLogin), QStringLiteral("core"));
    f_registry.registerCommand(singleForm(QStringLiteral("logout"), {akashi::permission::user}, {}, QStringLiteral("Logs you out."), cmdLogout), QStringLiteral("core"));
    f_registry.registerCommand(singleForm(QStringLiteral("listusers"), {akashi::permission::modify_users}, {}, QStringLiteral("Lists all users in the database."), cmdListUsers), QStringLiteral("core"));

    CommandSpec l_adduser;
    l_adduser.name = QStringLiteral("adduser");
    l_adduser.description = QStringLiteral("Creates a new user with the given credentials.");
    l_adduser.variants = {{.id = QStringLiteral("add"), .gate = {akashi::permission::modify_users}, .params = {l_username, l_password}, .run = cmdAddUser}};
    f_registry.registerCommand(l_adduser, QStringLiteral("core"));

    CommandSpec l_removeuser;
    l_removeuser.name = QStringLiteral("removeuser");
    l_removeuser.description = QStringLiteral("Removes the user with the given name.");
    l_removeuser.variants = {{.id = QStringLiteral("remove"), .gate = {akashi::permission::modify_users}, .params = {l_username}, .run = cmdRemoveUser}};
    f_registry.registerCommand(l_removeuser, QStringLiteral("core"));

    CommandSpec l_listperms;
    l_listperms.name = QStringLiteral("listperms");
    l_listperms.description = QStringLiteral("Lists your permissions, or the permissions of the given user.");
    l_listperms.variants = {
        {.id = QStringLiteral("own"), .gate = {akashi::permission::user}, .description = QStringLiteral("Lists your permissions."), .run = cmdListPermsSelf},
        {.id = QStringLiteral("user"), .gate = {akashi::permission::modify_users}, .description = QStringLiteral("Lists the given user's permissions."), .params = {l_username}, .run = cmdListPermsUser},
    };
    f_registry.registerCommand(l_listperms, QStringLiteral("core"));

    // Handing out SUPER is its own form, gated on super. The general form
    // leaves SUPER out of its roles, so no spelling of it gets past.
    CommandSpec l_setperms;
    l_setperms.name = QStringLiteral("setperms");
    l_setperms.description = QStringLiteral("Sets the role of the given user.");
    l_setperms.variants = {
        {.id = QStringLiteral("super"), .gate = {akashi::permission::super}, .description = QStringLiteral("Assigns the SUPER role."), .params = {l_username, param::word(QStringLiteral("role"), {ACLRolesHandler::SUPER_ID})}, .run = cmdSetPerms},
        {.id = QStringLiteral("role"), .gate = {akashi::permission::modify_users}, .description = QStringLiteral("Assigns any other role."), .params = {l_username, param::role(QStringLiteral("role")).without({ACLRolesHandler::SUPER_ID})}, .run = cmdSetPerms},
    };
    f_registry.registerCommand(l_setperms, QStringLiteral("core"));

    CommandSpec l_removeperms;
    l_removeperms.name = QStringLiteral("removeperms");
    l_removeperms.description = QStringLiteral("Removes all permissions from the given user.");
    l_removeperms.variants = {{.id = QStringLiteral("remove"), .gate = {akashi::permission::modify_users}, .params = {l_username}, .run = cmdRemovePerms}};
    f_registry.registerCommand(l_removeperms, QStringLiteral("core"));

    CommandSpec l_changepass;
    l_changepass.name = QStringLiteral("changepass");
    l_changepass.description = QStringLiteral("Changes your password, or another user's password if you have super permissions.");
    l_changepass.variants = {
        {.id = QStringLiteral("own"), .gate = {akashi::permission::user}, .description = QStringLiteral("Changes your password."), .params = {l_password}, .run = cmdChangePassSelf},
        {.id = QStringLiteral("user"), .gate = {akashi::permission::super}, .description = QStringLiteral("Changes another user's password."), .params = {l_password, l_username}, .run = cmdChangePassUser},
    };
    f_registry.registerCommand(l_changepass, QStringLiteral("core"));
}

} // namespace akashi::commands
