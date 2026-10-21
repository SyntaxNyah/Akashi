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

bool CommandRegistry::registerCommand(const CommandSpec &f_spec, CommandHandler f_handler,
                                      const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = f_spec.name.toLower();
    if (m_entries.contains(l_key) || m_aliases.contains(l_key)) {
        return false;
    }

    for (const QString &l_alias : f_spec.aliases) {
        const QString l_alias_key = l_alias.toLower();
        if (m_entries.contains(l_alias_key) || m_aliases.contains(l_alias_key)) {
            return false;
        }
    }

    for (const CommandVariant &l_variant : f_spec.variants) {
        if (l_variant.id.isEmpty() || !l_variant.handler) {
            return false;
        }
    }

    m_entries.insert(l_key, Entry{f_spec, std::move(f_handler), f_owner_id});
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

void CommandRegistry::unregisterAll(const QString &f_owner_id)
{
    AKASHI_ASSERT_OWNER_THREAD();
    m_entries.removeIf([this, &f_owner_id](std::pair<const QString &, Entry &> f_item) {
        const Entry &l_entry = f_item.second;
        if (l_entry.owner_id != f_owner_id) {
            return false;
        }
        for (const QString &l_alias : l_entry.spec.aliases) {
            m_aliases.remove(l_alias.toLower());
        }
        return true;
    });
}

QString CommandRegistry::resolve(const QString &f_name) const
{
    const QString l_key = f_name.toLower();
    if (m_entries.contains(l_key)) {
        return l_key;
    }
    return m_aliases.value(l_key);
}

std::optional<CommandSpec> CommandRegistry::spec(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = resolve(f_command_name);
    if (auto it = m_entries.constFind(l_key); it != m_entries.constEnd()) {
        return it->spec;
    }
    return std::nullopt;
}

CommandHandler CommandRegistry::handler(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = resolve(f_command_name);
    if (auto it = m_entries.constFind(l_key); it != m_entries.constEnd()) {
        return it->handler;
    }
    return {};
}

std::optional<CommandRegistry::Resolved> CommandRegistry::lookup(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    const QString l_key = resolve(f_command_name);
    if (auto it = m_entries.constFind(l_key); it != m_entries.constEnd()) {
        return Resolved{it->spec, it->handler};
    }
    return std::nullopt;
}

QStringList CommandRegistry::commandNames() const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return m_entries.keys();
}

bool CommandRegistry::contains(const QString &f_command_name) const
{
    AKASHI_ASSERT_OWNER_THREAD();
    return !resolve(f_command_name).isEmpty();
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

        auto l_entry = m_entries.find(l_name);
        if (l_entry == m_entries.end()) {
            qCWarning(akashiCommands) << "command_extensions: unknown command" << l_name;
            continue;
        }

        if (l_ext.contains(QStringLiteral("aliases"))) {
            const QStringList l_new_aliases = l_ext.value(QStringLiteral("aliases")).toString().split(QChar(' '), Qt::SkipEmptyParts);
            for (const QString &l_alias : l_new_aliases) {
                const QString l_alias_key = l_alias.toLower();
                if (resolve(l_alias_key) == l_name) {
                    continue;
                }
                if (m_entries.contains(l_alias_key) || m_aliases.contains(l_alias_key)) {
                    qCWarning(akashiCommands) << "command_extensions:" << l_name << "alias" << l_alias << "collides with an existing command or alias";
                    continue;
                }
                l_entry->spec.aliases.append(l_alias_key);
                m_aliases.insert(l_alias_key, l_name);
            }
        }
    }
}

} // namespace akashi
