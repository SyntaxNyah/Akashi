#pragma once

#include "akashi/helpers/parse.h"
#include "akashi_core_export.h"
#include "core/player_directory.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace akashi {

class ClientSession;
class TargetPlayer;

// What a parameter may ask about the world while it reads: who is asking,
// where, and who else is connected. The dispatcher hands one over for the
// person running the command; tests hand over a fake.
class AKASHI_CORE_EXPORT ParamScope
{
  public:
    virtual ~ParamScope() = default;

    virtual int invokerId() const = 0;
    virtual int invokerArea() const = 0;
    virtual int areaCount() const = 0;
    virtual bool invokerCan(const QString &f_permission) const = 0;
    virtual helpers::Result<ClientSession *> findClient(const QString &f_text, const PlayerFilter &f_filter) const = 0;
    virtual bool clientCan(ClientSession *f_client, const QString &f_permission) const = 0;
    // True when the client owns (is CM of) the area the invoker stands in.
    virtual bool ownsInvokerArea(ClientSession *f_client) const = 0;
    // A server setting a limit may depend on, by name ("max_dice").
    virtual int setting(const QString &f_name) const = 0;
};

// One declared parameter of a command form. Built with the param::
// functions and chained options:
//   param::client("target").notSelf().inArea().immuneIfAreaOwner()
//   param::integer("sides", 1, 100).optional()
//   param::text("reason").optional().maxLength(200).sensitive()
// Each kind reads through the matching akashi.helpers reader, so a
// refusal is that reader's ParseError.
class AKASHI_CORE_EXPORT Param
{
  public:
    enum class Kind
    {
        Client,
        Area,
        Integer,
        Word,
        Duration,
        Dice,
        Text,
    };

    // A limit that may depend on the server, read when the parameter is.
    using Limit = std::function<qint64(const ParamScope &)>;

    Param(Kind f_kind, const QString &f_name);

    // Options for every kind.
    Param optional() const;
    Param sensitive() const;
    // Client: refuse the asker, or anyone outside the asker's area.
    Param notSelf() const;
    Param inArea() const;
    // Client: refuse targets holding the permission (unless the asker
    // holds super), or targets that own the asker's area.
    Param immuneIf(const QString &f_permission) const;
    Param immuneIfAreaOwner() const;
    // Integer, Duration, Text: the upper limit, fixed or from the server.
    Param max(qint64 f_max) const;
    Param max(Limit f_max) const;
    // Text: the most characters accepted.
    Param maxLength(int f_length) const;
    // Dice: the most dice and sides, fixed or from the server.
    Param diceLimits(Limit f_max_count, Limit f_max_sides) const;

    Kind kind() const { return m_kind; }
    QString name() const { return m_name; }
    bool isOptional() const { return m_optional; }
    bool isSensitive() const { return m_sensitive; }
    QString immunePermission() const { return m_immune_permission; }
    bool isImmuneIfAreaOwner() const { return m_immune_area_owner; }
    // How the parameter reads in a usage line: <target>, [reason...], all.
    QString placeholder() const;

  private:
    // Builds and reads parameters; internal to command_params.cpp.
    friend class FormReader;

    Kind m_kind;
    QString m_name;
    bool m_optional = false;
    bool m_sensitive = false;
    bool m_not_self = false;
    bool m_in_area = false;
    QString m_immune_permission;
    bool m_immune_area_owner = false;
    qint64 m_min = 0;
    Limit m_max;
    QStringList m_words;
    Limit m_max_count;
    Limit m_max_sides;
};

namespace param {
AKASHI_CORE_EXPORT Param client(const QString &f_name);
AKASHI_CORE_EXPORT Param area(const QString &f_name);
AKASHI_CORE_EXPORT Param integer(const QString &f_name, qint64 f_min, qint64 f_max);
AKASHI_CORE_EXPORT Param word(const QString &f_name, const QStringList &f_words);
AKASHI_CORE_EXPORT Param duration(const QString &f_name);
AKASHI_CORE_EXPORT Param dice(const QString &f_name);
// The rest of the line, joined by spaces. Must be the form's last parameter.
AKASHI_CORE_EXPORT Param text(const QString &f_name);
} // namespace param

// The checked values of the form that ran, by parameter name. A getter
// for a parameter that is not set (an optional one left off) answers a
// default; ask has() first for optional parameters.
class AKASHI_CORE_EXPORT Args
{
  public:
    bool has(const QString &f_name) const;

    ClientSession *client(const QString &f_name) const;
    TargetPlayer target(const QString &f_name) const;
    int area(const QString &f_name) const;
    int integer(const QString &f_name) const;
    QString word(const QString &f_name) const;
    qint64 duration(const QString &f_name) const;
    helpers::Dice dice(const QString &f_name) const;
    QString text(const QString &f_name) const;

  private:
    friend class FormReader;

    QHash<QString, ClientSession *> m_clients;
    QHash<QString, qint64> m_numbers;
    QHash<QString, QString> m_texts;
    QHash<QString, helpers::Dice> m_dice;
};

// Reads one form's parameters against the typed arguments.
struct FormReading
{
    std::optional<Args> args;  // set when every parameter read
    helpers::ParseError error; // why not, otherwise
    int score = 0;             // how far it got, for picking which refusal to show
};

AKASHI_CORE_EXPORT FormReading readForm(const QList<Param> &f_params, const QStringList &f_arguments, const ParamScope &f_scope);

// The usage line of a declared form: "/area_kick <target> <destination>".
AKASHI_CORE_EXPORT QString usageOf(const QString &f_command, const QList<Param> &f_params);

// Whether a form's parameter list is well made: optional parameters only
// at the end, text only as the last parameter, names unique and set.
AKASHI_CORE_EXPORT bool validParams(const QList<Param> &f_params);

} // namespace akashi
