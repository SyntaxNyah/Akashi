#include "core/command_context.h"

#include "akashi/helpers/parse.h"
#include "akashi/permissions.h"
#include "core/client_session.h"
#include "core/command_registry.h"
#include "core/permission_registry.h"
#include "core/server_context.h"
#include "world/area.h"

#include <QRandomGenerator>

namespace akashi {

// -- TargetPlayer --

TargetPlayer::TargetPlayer(akashi::ClientSession *f_client) :
    m_client(f_client)
{}

int TargetPlayer::clientId() const { return m_client->clientId(); }
QString TargetPlayer::name() const { return m_client->name(); }
QString TargetPlayer::character() const { return m_client->character(); }
int TargetPlayer::areaId() const { return m_client->areaId(); }
QString TargetPlayer::ipid() const { return m_client->ipid(); }
bool TargetPlayer::isAuthenticated() const { return m_client->isAuthenticated(); }
bool TargetPlayer::canPerform(const QString &f_permission) const { return m_client->canPerform(f_permission); }

void TargetPlayer::reply(const QString &f_message)
{
    m_client->sendServerMessage(f_message);
}

bool TargetPlayer::hasSanction(const QString &f_sanction_id) const
{
    return m_client->hasSanction(f_sanction_id);
}

void TargetPlayer::setSanction(const QString &f_sanction_id, bool f_active)
{
    m_client->setSanction(f_sanction_id, f_active);
}

void TargetPlayer::changeArea(int f_area_id)
{
    m_client->changeArea(f_area_id);
}

void TargetPlayer::forceCharacterSelect()
{
    m_client->takeCharacter(-1, true);
}

void TargetPlayer::sendPacket(const QString &f_header, const QStringList &f_fields)
{
    m_client->sendPacket(f_header, f_fields);
}

void TargetPlayer::closeSocket()
{
    m_client->closeSocket();
}

QHostAddress TargetPlayer::remoteIp() const { return m_client->remoteIp(); }
QString TargetPlayer::hwid() const { return m_client->hwid(); }
QString TargetPlayer::characterName() const { return m_client->characterName(); }
bool TargetPlayer::isPmMuted() const { return m_client->isPmMuted(); }
QList<PlayerState *> TargetPlayer::players() const { return m_client->players; }

// -- CommandContext --

CommandContext::CommandContext(akashi::ClientSession *f_invoker, ServerContext *f_server, QStringList f_arguments,
                               const QString &f_command_name) :
    m_invoker(f_invoker),
    m_server(f_server),
    m_arguments(std::move(f_arguments)),
    m_command_name(f_command_name)
{}

int CommandContext::clientId() const { return m_invoker->clientId(); }
QString CommandContext::name() const { return m_invoker->name(); }
QString CommandContext::character() const { return m_invoker->character(); }
QString CommandContext::characterName() const { return m_invoker->characterName(); }
int CommandContext::areaId() const { return m_invoker->areaId(); }

Area *CommandContext::area() const { return m_server->areaById(m_invoker->areaId()); }
QString CommandContext::areaName() const { return m_invoker->areaName(); }
QString CommandContext::ipid() const { return m_invoker->ipid(); }
QString CommandContext::hwid() const { return m_invoker->hwid(); }
bool CommandContext::isAuthenticated() const { return m_invoker->isAuthenticated(); }

bool CommandContext::canPerform(const QString &f_permission) const
{
    return m_invoker->canPerform(f_permission);
}

bool CommandContext::canPerformIn(const QString &f_permission, int f_area_id) const
{
    return m_invoker->canPerformIn(f_permission, f_area_id);
}

QString CommandContext::commandName() const
{
    return m_command_name;
}

QString CommandContext::aclRoleId() const { return m_invoker->aclRoleId(); }
void CommandContext::setAclRoleId(const QString &f_role_id) { m_invoker->setAclRoleId(f_role_id); }
QString CommandContext::moderatorName() const { return m_invoker->moderatorName(); }
void CommandContext::setModeratorName(const QString &f_name) { m_invoker->setModeratorName(f_name); }
void CommandContext::setAuthenticated(bool f_state) { m_invoker->setAuthenticated(f_state); }
void CommandContext::setInLoginPrompt(bool f_state) { m_invoker->setInLoginPrompt(f_state); }

const QStringList &CommandContext::arguments() const { return m_arguments; }

void CommandContext::reply(const QString &f_message)
{
    m_invoker->sendServerMessage(f_message);
}

void CommandContext::reply(const helpers::ParseError &f_error)
{
    reply(f_error.errorString());
}

void CommandContext::replyToArea(const QString &f_message)
{
    m_invoker->sendServerMessageArea(f_message);
}

void CommandContext::replyToServer(const QString &f_message)
{
    m_invoker->sendServerBroadcast(f_message);
}

void CommandContext::sendPacket(const QString &f_header, const QStringList &f_fields)
{
    m_invoker->sendPacket(f_header, f_fields);
}

namespace {

// ParamScope for a live session: answers from the server's state.
class SessionScope : public ParamScope
{
  public:
    SessionScope(akashi::ClientSession *f_invoker, ServerContext *f_server) :
        m_invoker(f_invoker),
        m_server(f_server)
    {}

    int invokerId() const override { return m_invoker->clientId(); }
    int invokerArea() const override { return m_invoker->areaId(); }
    int areaCount() const override { return m_server->areaCount(); }
    bool invokerCan(const QString &f_permission) const override { return m_invoker->canPerform(f_permission); }

    helpers::Result<akashi::ClientSession *> findClient(const QString &f_text, const PlayerFilter &f_filter) const override
    {
        return m_server->playerDirectory().find(f_text, f_filter);
    }

    bool clientCan(akashi::ClientSession *f_client, const QString &f_permission) const override
    {
        return f_client->canPerform(f_permission);
    }

    bool ownsInvokerArea(akashi::ClientSession *f_client) const override
    {
        const Area *l_area = m_server->areaById(m_invoker->areaId());
        return l_area && l_area->owners().contains(f_client->clientId());
    }

    QStringList roleIds() const override { return m_server->aclRolesHandler()->roleIds(); }

  private:
    akashi::ClientSession *m_invoker;
    ServerContext *m_server;
};

} // namespace

std::unique_ptr<ParamScope> CommandContext::paramScope() const
{
    return std::make_unique<SessionScope>(m_invoker, m_server);
}

akashi::ClientSession *CommandContext::self() const
{
    return m_invoker;
}

std::optional<TargetPlayer> CommandContext::resolveTarget(int f_argument_index, const PlayerFilter &f_filter)
{
    const auto l_found = m_server->playerDirectory().find(m_arguments.value(f_argument_index), f_filter);
    if (!l_found) {
        reply(l_found.error());
        return std::nullopt;
    }
    return TargetPlayer(*l_found);
}

int CommandContext::genRand(int f_min, int f_max)
{
    return QRandomGenerator::system()->bounded(f_min, f_max + 1);
}

akashi::ServiceRegistry *CommandContext::services() const
{
    return m_server->services();
}

ServerContext *CommandContext::server() const
{
    return m_server;
}

} // namespace akashi
