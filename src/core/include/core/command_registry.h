#pragma once

#include "akashi/service.h"
#include "akashi_core_export.h"
#include "core/command_spec.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QThread;

namespace akashi {

class CommandContext;

class AKASHI_CORE_EXPORT CommandRegistry : public IService
{
  public:
    CommandRegistry();

    QString serviceId() const override;
    ServiceVersion serviceVersion() const override;

    bool registerCommand(const CommandSpec &f_spec, CommandHandler f_handler,
                         const QString &f_owner_id = {});

    // Registers a command whose forms live in spec.variants; every variant
    // must carry an id and a handler.
    bool registerCommand(const CommandSpec &f_spec, const QString &f_owner_id);

    void unregisterAll(const QString &f_owner_id);

    std::optional<CommandSpec> spec(const QString &f_command_name) const;
    CommandHandler handler(const QString &f_command_name) const;
    QStringList commandNames() const;
    bool contains(const QString &f_command_name) const;

    struct Resolved
    {
        CommandSpec spec;
        CommandHandler handler;
    };

    // The dispatcher's single lookup: the command's spec and handler
    // together, alias-resolved once.
    std::optional<Resolved> lookup(const QString &f_command_name) const;

    // True when any form of the command is open to a caller whose
    // permissions f_can_perform answers for; false for unknown commands.
    // /commands and /help list through this so they mirror the dispatcher.
    bool canUse(const QString &f_command, const std::function<bool(const QString &)> &f_can_perform) const;

    // Applies command_extensions.json, which adds aliases and nothing else:
    // a command's behaviour and requirements belong to its definition, and
    // a plugin that wants different ones registers its own. An old
    // "permissions" override is no longer applied and is reported
    // critically, because it may have been tightening a command.
    void applyExtensions(const QString &f_path);

  private:
    struct Entry
    {
        CommandSpec spec;
        CommandHandler handler;
        QString owner_id;
    };

    QString resolve(const QString &f_name) const;

    QHash<QString, Entry> m_entries;
    QHash<QString, QString> m_aliases;
    QThread *m_owner_thread;
};

} // namespace akashi
