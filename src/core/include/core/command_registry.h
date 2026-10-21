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

class ClientSession;
class CommandContext;

// Every command lives in a slot: its name. A slot holds one occupant - a
// complete definition and its owner - or is empty. Outside the dispatcher
// a command is only ever described: running one goes through its gate and
// the command log.
class AKASHI_CORE_EXPORT CommandRegistry : public IService
{
  public:
    CommandRegistry();

    QString serviceId() const override;
    ServiceVersion serviceVersion() const override;

    // Fills an empty slot. Fails when the definition is malformed (a form
    // without an id or a body, or with parameters validParams refuses), or
    // when the name or one of its aliases is already taken - taking over
    // a command is replace(), on purpose.
    bool registerCommand(const CommandSpec &f_spec, const QString &f_owner_id);

    // Takes over a slot, whoever holds it, or fills it when empty. The
    // previous occupant is dropped, not kept: nothing restores it later.
    // Fails only when the spec is malformed or one of its aliases names
    // another slot. Taking a command from another owner is logged.
    bool replace(const CommandSpec &f_spec, const QString &f_owner_id);

    // Empties a slot, whoever holds it; logged as a warning. The command
    // answers "Invalid command" until something registers it again.
    bool vacate(const QString &f_command_name, const QString &f_owner_id);

    // Empties every slot the owner occupies, as its plugin unloads.
    // Nothing is put back.
    void unregisterAll(const QString &f_owner_id);

    // The occupant's description, with the slot's operator aliases added.
    // Empty for an empty slot or an unknown name.
    std::optional<CommandSpec> spec(const QString &f_command_name) const;
    // Who occupies the slot, and whose command they took it from (empty
    // when they filled it). Both empty for an empty slot.
    QString ownerOf(const QString &f_command_name) const;
    QString replacedOwnerOf(const QString &f_command_name) const;
    // Slots left empty by vacate or an unload, with who left them so.
    QHash<QString, QString> vacatedSlots() const;

    QStringList commandNames() const;
    bool contains(const QString &f_command_name) const;

    // True when any form of the command is open to a caller whose
    // permissions f_can_perform answers for; false for unknown commands.
    // /commands and /help list through this so they mirror the dispatcher.
    bool canUse(const QString &f_command, const std::function<bool(const QString &)> &f_can_perform) const;

    // Applies command_extensions.json, which adds aliases and nothing else:
    // a command's behaviour and requirements belong to its definition, and
    // a plugin that wants different ones replaces it. These aliases belong
    // to the slot, so they survive a replace. An old "permissions"
    // override is no longer applied and is reported critically, because
    // it may have been tightening a command.
    void applyExtensions(const QString &f_path);

  private:
    struct Entry
    {
        CommandSpec spec; // the occupant's own definition and aliases
        QString owner_id;
        QString replaced_owner; // whose command this took over, if any
    };

    bool validSpec(const CommandSpec &f_spec) const;
    // True when an alias of the spec is a name or alias of another slot.
    bool aliasesCollide(const CommandSpec &f_spec, const QString &f_slot) const;
    void dropOccupant(const QString &f_slot);
    QString resolve(const QString &f_name) const;

    QHash<QString, Entry> m_entries;
    QHash<QString, QString> m_aliases;      // every alias, to its slot
    QHash<QString, QString> m_slot_aliases; // the operator's aliases, to their slot
    QHash<QString, QString> m_vacated;      // empty slot -> who left it so
    QThread *m_owner_thread;
};

} // namespace akashi
