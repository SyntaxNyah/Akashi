#pragma once

#include "akashi_core_export.h"
#include "core/command_params.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace akashi {

class CommandContext;

// The handler of a declared form: every parameter is already read and
// checked, and arrives in Args.
using ArgsHandler = std::function<void(CommandContext &, const Args &)>;

// A command form's whole gate: any-of over all-of. One permission is a
// group of one, so the ordinary case still reads as a plain name -
// {"area.cm","kick"} in one group needs both, two groups of one needs
// either. An empty gate is an open command.
struct Gate
{
    Gate() = default;
    Gate(const QString &f_permission) :
        groups{{f_permission}}
    {}
    Gate(std::initializer_list<QString> f_any_of)
    {
        for (const QString &l_permission : f_any_of) {
            groups.append({l_permission});
        }
    }
    Gate(const QStringList &f_any_of)
    {
        for (const QString &l_permission : f_any_of) {
            groups.append({l_permission});
        }
    }

    // Every named permission must resolve for this to pass.
    static Gate allOf(const QStringList &f_permissions)
    {
        Gate l_gate;
        if (!f_permissions.isEmpty()) {
            l_gate.groups.append(f_permissions);
        }
        return l_gate;
    }

    QList<QStringList> groups;

    bool isEmpty() const { return groups.isEmpty(); }

    // The gate itself, asked the one way. The dispatcher, /commands and
    // /help all call this, so a listing can never disagree with what
    // dispatch actually does.
    bool passes(const std::function<bool(const QString &)> &f_can_perform) const
    {
        if (groups.isEmpty()) {
            return true;
        }
        for (const QStringList &l_group : groups) {
            bool l_all = true;
            for (const QString &l_permission : l_group) {
                if (!f_can_perform(l_permission)) {
                    l_all = false;
                    break;
                }
            }
            if (l_all) {
                return true;
            }
        }
        return false;
    }

    // Every permission the gate can ask about, in declaration order and
    // without repeats - what /why walks to explain a command.
    QStringList permissions() const
    {
        QStringList l_names;
        for (const QStringList &l_group : groups) {
            for (const QString &l_permission : l_group) {
                if (!l_names.contains(l_permission)) {
                    l_names.append(l_permission);
                }
            }
        }
        return l_names;
    }

    // The words /help and /why print: "a, b+c" reads as a OR (b AND c).
    QString describe() const
    {
        if (groups.isEmpty()) {
            return QStringLiteral("nothing");
        }
        QStringList l_terms;
        for (const QStringList &l_group : groups) {
            l_terms << l_group.join(QLatin1Char('+'));
        }
        return l_terms.join(QStringLiteral(", "));
    }

    bool operator==(const Gate &) const = default;
};

// One gated form of a command: the parameters it reads and the gate it
// needs. The dispatcher tries a command's forms in order and runs the
// first whose parameters all read among those the caller passes - no
// permission check hides inside a handler body, so the command list sees
// every gate.
struct CommandVariant
{
    QString id;    // names the form, for /help and /why
    Gate gate;     // checked at the dispatch gate
    QString usage; // generated from the parameters when left empty
    QString description;
    QList<Param> params;
    ArgsHandler run;
};

struct CommandSpec
{
    QString name;
    QStringList aliases;
    QString usage; // every form's usage, one per line; generated
    QString description;
    QList<CommandVariant> variants;

    // The first argument position the command log must hide, -1 for none:
    // the earliest sensitive() parameter of any form - the log runs before
    // a form is picked, so the strictest position wins.
    int sensitiveFrom() const
    {
        int l_from = -1;
        for (const CommandVariant &l_variant : variants) {
            for (int i = 0; i < l_variant.params.size(); ++i) {
                if (l_variant.params.at(i).isSensitive() && (l_from < 0 || i < l_from)) {
                    l_from = i;
                }
            }
        }
        return l_from;
    }
};

// A command with one declared form - most commands have one shape.
inline CommandSpec singleForm(const QString &f_name, const Gate &f_gate, const QList<Param> &f_params,
                              const QString &f_description, ArgsHandler f_run, const QStringList &f_aliases = {})
{
    CommandSpec l_spec;
    l_spec.name = f_name;
    l_spec.aliases = f_aliases;
    l_spec.description = f_description;
    l_spec.variants = {{.id = QStringLiteral("run"), .gate = f_gate, .params = f_params, .run = std::move(f_run)}};
    return l_spec;
}

// Which declared form runs for the typed arguments, or why none does.
struct FormChoice
{
    const CommandVariant *form = nullptr; // the form to run, when one fits
    std::optional<Args> args;             // its checked values
    helpers::ParseError error;            // otherwise: the refusal to show
    bool denied = false;                  // otherwise: only closed forms fit
    QStringList usages;                   // the forms the caller may type
};

// Tries a declared command's forms in order and picks the first whose
// parameters all read, among the forms whose gate the caller passes.
// When none fits it answers the refusal of the form that read furthest,
// or denied when the caller lacks no syntax but a permission: no form
// open to them fits, or only a closed one would have.
AKASHI_CORE_EXPORT FormChoice chooseForm(const CommandSpec &f_spec, const QStringList &f_arguments, const ParamScope &f_scope,
                                         const std::function<bool(const QString &)> &f_can_perform);

} // namespace akashi
