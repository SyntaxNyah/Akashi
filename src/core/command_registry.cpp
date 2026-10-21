#include "core/command_registry.h"

#include "akashi/logging_categories.h"
#include "akashi/thread_assert.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace akashi {

CommandRegistry::CommandRegistry() :
    m_owner_thread(QThread::currentThread())
{}

QString CommandRegistry::serviceId() const
{
    return QStringLiteral("akashi.commands");
}

ServiceVersion CommandRegistry::serviceVersion() const
{
    return {1, 0, 0};
}

bool CommandRegistry::validSpec(const CommandSpec &f_spec) const
{
    if (f_spec.name.isEmpty()) {
        return false;
    }
    // A command is declared or not as a whole: the dispatcher picks forms
    // one way or the other, never both.
    const bool l_declared = !f_spec.variants.isEmpty() && f_spec.variants.first().isDeclared();
    for (const CommandVariant &l_variant : f_spec.variants) {
        if (l_variant.id.isEmpty() || l_variant.isDeclared() != l_declared) {
            return false;
        }
        if (l_declared ? !validParams(l_variant.params) : !l_variant.handler) {
            return false;
        }
    }
    return true;
}

// The definition as stored: declared forms get their usage generated from
// their parameters, and the command's usage lists them, unless given.
static CommandSpec prepared(CommandSpec f_spec)
{
    QStringList l_usages;
    for (CommandVariant &l_variant : f_spec.variants) {
        if (l_variant.isDeclared() && l_variant.usage.isEmpty()) {
            l_variant.usage = usageOf(f_spec.name.toLower(), l_variant.params);
        }
        l_usages << l_variant.usage;
    }
    if (f_spec.usage.isEmpty() && !f_spec.variants.isEmpty() && f_spec.variants.first().isDeclared()) {
        f_spec.usage = l_usages.join(QLatin1Char('\n'));
    }
    return f_spec;
}

bool CommandRegistry::aliasesCollide(const CommandSpec &f_spec, const QString &f_slot) const
{
    for (const QString &l_alias : f_spec.aliases) {
        const QString l_alias_key = l_alias.toLower();
        if (l_alias_key == f_slot) {
            continue;
        }
        if (m_entries.contains(l_alias_key) || m_vacated.contains(l_alias_key)) {
            return true;
        }
        const auto l_target = m_aliases.constFind(l_alias_key);
        if (l_target != m_aliases.constEnd() && *l_target != f_slot) {
            return true;
        }
    }
    return false;
}

bool CommandRegistry::registerCommand(const CommandSpec &f_spec, CommandHandler f_handler,
                                      const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = f_spec.name.toLower();
    // An empty slot only: a name held by a command, or by an alias of
    // one, is a take-over, and that is replace().
    if (!validSpec(f_spec) || m_entries.contains(l_key) || m_aliases.contains(l_key) || aliasesCollide(f_spec, l_key)) {
        return false;
    }
    m_vacated.remove(l_key);
    m_entries.insert(l_key, Entry{prepared(f_spec), std::move(f_handler), f_owner_id, {}});
    for (const QString &l_alias : f_spec.aliases) {
        m_aliases.insert(l_alias.toLower(), l_key);
    }
    return true;
}

bool CommandRegistry::registerCommand(const CommandSpec &f_spec, const QString &f_owner_id)
{
    if (f_spec.variants.isEmpty()) {
        return false;
    }
    return registerCommand(f_spec, CommandHandler(), f_owner_id);
}

bool CommandRegistry::replace(const CommandSpec &f_spec, CommandHandler f_handler, const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = f_spec.name.toLower();
    // The name may belong to anyone; an alias may not reach into another
    // slot, or one replace would take two commands.
    if (!validSpec(f_spec) || aliasesCollide(f_spec, l_key)) {
        qCWarning(akashiCommands).noquote() << QStringLiteral("%1 could not replace /%2: the definition is malformed or an alias names another command").arg(f_owner_id, l_key);
        return false;
    }
    const auto l_alias_owner = m_aliases.constFind(l_key);
    if (l_alias_owner != m_aliases.constEnd() && *l_alias_owner != l_key) {
        qCWarning(akashiCommands).noquote() << QStringLiteral("%1 could not replace /%2: it is an alias of /%3").arg(f_owner_id, l_key, *l_alias_owner);
        return false;
    }

    QString l_previous;
    if (const auto it = m_entries.constFind(l_key); it != m_entries.constEnd()) {
        l_previous = it->owner_id;
        dropOccupant(l_key);
    }
    m_vacated.remove(l_key);
    m_entries.insert(l_key, Entry{prepared(f_spec), std::move(f_handler), f_owner_id, l_previous == f_owner_id ? QString() : l_previous});
    for (const QString &l_alias : f_spec.aliases) {
        m_aliases.insert(l_alias.toLower(), l_key);
    }

    if (l_previous == QStringLiteral("core") && f_owner_id != l_previous) {
        qCWarning(akashiCommands).noquote() << QStringLiteral("%1 replaced the core command /%2").arg(f_owner_id, l_key);
    }
    else if (!l_previous.isEmpty() && f_owner_id != l_previous) {
        qCInfo(akashiCommands).noquote() << QStringLiteral("%1 replaced /%2 from %3").arg(f_owner_id, l_key, l_previous);
    }
    return true;
}

bool CommandRegistry::replace(const CommandSpec &f_spec, const QString &f_owner_id)
{
    if (f_spec.variants.isEmpty()) {
        return false;
    }
    return replace(f_spec, CommandHandler(), f_owner_id);
}

void CommandRegistry::dropOccupant(const QString &f_slot)
{
    const auto it = m_entries.constFind(f_slot);
    if (it == m_entries.constEnd()) {
        return;
    }
    // The occupant's own aliases leave with it; the operator's stay with
    // the slot for whoever fills it next.
    for (const QString &l_alias : it->spec.aliases) {
        const QString l_alias_key = l_alias.toLower();
        if (!m_slot_aliases.contains(l_alias_key)) {
            m_aliases.remove(l_alias_key);
        }
    }
    m_entries.remove(f_slot);
}

bool CommandRegistry::vacate(const QString &f_command_name, const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = resolve(f_command_name);
    if (!m_entries.contains(l_key)) {
        return false;
    }
    const QString l_previous = m_entries.value(l_key).owner_id;
    dropOccupant(l_key);
    m_vacated.insert(l_key, f_owner_id);
    qCWarning(akashiCommands).noquote() << QStringLiteral("%1 vacated /%2 (held by %3); it stays empty until something registers it").arg(f_owner_id, l_key, l_previous);
    return true;
}

void CommandRegistry::unregisterAll(const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    QStringList l_owned;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->owner_id == f_owner_id) {
            l_owned << it.key();
        }
    }
    for (const QString &l_slot : std::as_const(l_owned)) {
        const QString l_replaced = m_entries.value(l_slot).replaced_owner;
        dropOccupant(l_slot);
        // A command the owner took from someone else leaves a hole where
        // a command used to be: that is worth a warning. Its own command
        // leaving with it is ordinary.
        if (!l_replaced.isEmpty()) {
            m_vacated.insert(l_slot, f_owner_id);
            qCWarning(akashiCommands).noquote() << QStringLiteral("%1 left /%2 empty; it had replaced the command from %3, which is not restored").arg(f_owner_id, l_slot, l_replaced);
        }
        else {
            qCInfo(akashiCommands).noquote() << QStringLiteral("/%1 left with %2").arg(l_slot, f_owner_id);
        }
    }
}

QString CommandRegistry::resolve(const QString &f_name) const
{
    const QString l_key = f_name.toLower();
    if (m_entries.contains(l_key) || m_vacated.contains(l_key)) {
        return l_key;
    }
    return m_aliases.value(l_key);
}

std::optional<CommandSpec> CommandRegistry::spec(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = resolve(f_command_name);
    const auto it = m_entries.constFind(l_key);
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    CommandSpec l_spec = it->spec;
    for (auto l_alias = m_slot_aliases.cbegin(); l_alias != m_slot_aliases.cend(); ++l_alias) {
        if (l_alias.value() == l_key && !l_spec.aliases.contains(l_alias.key(), Qt::CaseInsensitive)) {
            l_spec.aliases.append(l_alias.key());
        }
    }
    return l_spec;
}

QString CommandRegistry::ownerOf(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_entries.value(resolve(f_command_name)).owner_id;
}

QString CommandRegistry::replacedOwnerOf(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_entries.value(resolve(f_command_name)).replaced_owner;
}

QHash<QString, QString> CommandRegistry::vacatedSlots() const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_vacated;
}

std::optional<CommandRegistry::Resolved> CommandRegistry::lookup(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const auto it = m_entries.constFind(resolve(f_command_name));
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    return Resolved{it->spec, it->handler};
}

QStringList CommandRegistry::commandNames() const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_entries.keys();
}

bool CommandRegistry::contains(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_entries.contains(resolve(f_command_name));
}

bool CommandRegistry::canUse(const QString &f_command, const std::function<bool(const QString &)> &f_can_perform) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const auto l_spec = spec(f_command);
    if (!l_spec) {
        return false;
    }
    if (l_spec->variants.isEmpty()) {
        return l_spec->gate.passes(f_can_perform);
    }
    for (const CommandVariant &l_variant : l_spec->variants) {
        if (l_variant.gate.passes(f_can_perform)) {
            return true;
        }
    }
    return false;
}

void CommandRegistry::applyExtensions(const QString &f_path)
{
    AKASHI_ASSERT_OWNER_THREAD();
    QFile l_file(f_path);
    if (!l_file.open(QIODevice::ReadOnly)) {
        return;
    }
    QJsonDocument l_doc = QJsonDocument::fromJson(l_file.readAll());
    if (!l_doc.isObject()) {
        qCWarning(akashiCommands) << f_path << "is not a valid command extensions file";
        return;
    }

    const QJsonObject l_root = l_doc.object();
    for (auto it = l_root.begin(); it != l_root.end(); ++it) {
        const QString l_name = it.key().toLower();
        const QJsonObject l_ext = it.value().toObject();

        // Overrides of a command's requirements are gone: a command's
        // definition owns its gates. Ignoring one silently could loosen a
        // command the owner had tightened, so every one is reported.
        if (l_ext.contains(QStringLiteral("permissions")) || l_name.contains(QChar('.'))) {
            qCCritical(akashiCommands) << "command_extensions:" << it.key()
                                       << "overrides permissions, which is no longer supported - the override was NOT applied"
                                       << "and the command keeps its own requirements. Grant or revoke permissions in permissions.json instead.";
        }
        if (l_name.contains(QChar('.'))) {
            continue;
        }

        if (!m_entries.contains(l_name) && !m_vacated.contains(l_name)) {
            qCWarning(akashiCommands) << "command_extensions: unknown command" << l_name;
            continue;
        }

        if (l_ext.contains(QStringLiteral("aliases"))) {
            const QStringList l_new_aliases = l_ext.value(QStringLiteral("aliases")).toString().split(QChar(' '), Qt::SkipEmptyParts);
            for (const QString &l_alias : l_new_aliases) {
                const QString l_alias_key = l_alias.toLower();
                if (resolve(l_alias_key) == l_name) {
                    // Already this slot's - an operator alias the occupant
                    // also declares is still kept for the slot.
                    m_slot_aliases.insert(l_alias_key, l_name);
                    continue;
                }
                if (m_entries.contains(l_alias_key) || m_vacated.contains(l_alias_key) || m_aliases.contains(l_alias_key)) {
                    qCWarning(akashiCommands) << "command_extensions:" << l_name << "alias" << l_alias << "collides with an existing command or alias";
                    continue;
                }
                m_slot_aliases.insert(l_alias_key, l_name);
                m_aliases.insert(l_alias_key, l_name);
            }
        }
    }
}

} // namespace akashi
