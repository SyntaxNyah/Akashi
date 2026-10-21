#pragma once

#include "akashi_core_export.h"
#include "core/command_params.h"
#include "core/player_directory.h"

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace akashi {
class ClientSession;
class PlayerState;
}
class ServerContext;

namespace akashi {
class Area;
class ServiceRegistry;
}

namespace akashi {

class AKASHI_CORE_EXPORT TargetPlayer
{
  public:
    explicit TargetPlayer(akashi::ClientSession *f_client);

    int clientId() const;
    QString name() const;
    QString character() const;
    int areaId() const;
    QString ipid() const;
    bool isAuthenticated() const;
    bool canPerform(const QString &f_permission) const;

    void reply(const QString &f_message);

    bool hasSanction(const QString &f_sanction_id) const;
    void setSanction(const QString &f_sanction_id, bool f_active);

    void changeArea(int f_area_id);
    void forceCharacterSelect();

    void sendPacket(const QString &f_header, const QStringList &f_fields);
    void closeSocket();
    QHostAddress remoteIp() const;
    QString hwid() const;
    QString characterName() const;
    bool isPmMuted() const;

    // The characters this person plays (core/player_state.h), oldest first.
    // A session has exactly one today; a richer protocol may
    // voice several. There is deliberately no active-character accessor:
    // plugins iterate the list or pick a PlayerState by its id. The states
    // live and die with the session: read them right away, never store them.
    QList<PlayerState *> players() const;

  private:
    akashi::ClientSession *m_client;
};

class AKASHI_CORE_EXPORT CommandContext
{
  public:
    CommandContext(akashi::ClientSession *f_invoker, ServerContext *f_server, QStringList f_arguments,
                   const QString &f_command_name = {});

    int clientId() const;
    QString name() const;
    QString character() const;
    QString characterName() const;
    int areaId() const;
    QString areaName() const;
    // The area the invoker stands in.
    Area *area() const;
    QString ipid() const;
    QString hwid() const;
    bool isAuthenticated() const;
    bool canPerform(const QString &f_permission) const;
    // The same question about another area - "would this resolve there?".
    bool canPerformIn(const QString &f_permission, int f_area_id) const;

    // The command being dispatched, as its registered name.
    QString commandName() const;

    QString aclRoleId() const;
    void setAclRoleId(const QString &f_role_id);
    QString moderatorName() const;
    void setModeratorName(const QString &f_name);
    void setAuthenticated(bool f_state);
    void setInLoginPrompt(bool f_state);

    // The arguments as typed. Declared forms read theirs from Args; the
    // scripting FFI hands these to script commands.
    const QStringList &arguments() const;

    void reply(const QString &f_message);
    // Tells the invoker why their input was refused, in the words
    // ParseError::errorString() gives the code.
    void reply(const helpers::ParseError &f_error);
    void replyToArea(const QString &f_message);
    void replyToServer(const QString &f_message);
    void sendPacket(const QString &f_header, const QStringList &f_fields);

    // The person running the command.
    akashi::ClientSession *self() const;

    // The world as declared parameters see it from the invoker's place:
    // who is asking, where, who else is connected, the server's limits.
    std::unique_ptr<ParamScope> paramScope() const;

    // The connected client an argument names, found through
    // PlayerDirectory::find, for script commands. When it names nobody
    // suitable this replies with the reason and answers nothing.
    std::optional<TargetPlayer> resolveTarget(int f_argument_index = 0, const PlayerFilter &f_filter = {});

    static int genRand(int f_min, int f_max);

    akashi::ServiceRegistry *services() const;
    ServerContext *server() const;

  private:
    akashi::ClientSession *m_invoker;
    ServerContext *m_server;
    QStringList m_arguments;
    QString m_command_name;
};

} // namespace akashi
