#pragma once

#include "akashi_core_export.h"
#include "core/command_params.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace akashi {

class CommandContext;

using CommandHandler = std::function<void(CommandContext &)>;
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

// One gated form of a command. Variants let one command carry different
// permission requirements per argument shape, resolved at the dispatch gate
// before the handler runs - no permission check hides inside a handler body,
// so the command list sees every gate.
struct CommandVariant
{
    QString id; // names the form, for /help and /why
    int min_args = 0;
    int max_args = -1; // -1 accepts any count at or above min_args
    Gate gate;         // checked at the dispatch gate
    QString usage;
    QString description;
    CommandHandler handler;

    // A declared form names its parameters and runs with checked values.
    // The dispatcher tries declared forms in order and runs the first
    // whose parameters all read; usage is generated when left empty.
    QList<Param> params;
    ArgsHandler run;

    bool isDeclared() const { return bool(run); }

    bool matches(int f_argc) const
    {
        return f_argc >= min_args && (max_args < 0 || f_argc <= max_args);
    }
};

struct CommandSpec
{
    QString name;
    QStringList aliases;
    Gate gate;
    int min_args = 0;
    QString usage;
    QString description;
    int sensitive_args_from = -1;

    // When non-empty, dispatch picks the first variant whose argument count
    // matches and gates on ITS gate; the spec-level gate, min_args and
    // registered handler are not consulted.
    QList<CommandVariant> variants;

    // A value-dependent escalation the body triggers but the spec names,
    // so /help shows it: timer 0 needs this, the SUPER role as a /setperms
    // argument needs this.
    QString escalates_to;
    QString escalates_when; // the human words for /help

    // A target holding this permission is immune to the command - "you
    // cannot kick another CM" - declared here instead of hidden inline,
    // enforced through CommandContext::targetIsImmune.
    QString target_immune_if;

    // The first variant taking this many arguments, or nullptr when no form
    // does. Declaration order decides ties, so narrower forms come first.
    // The result points into this spec's own storage, so bind the spec to a
    // named variable first - never call match() on a CommandRegistry lookup
    // returned by value (e.g. registry.spec(name)->match(argc)), or the
    // pointer dangles once the temporary spec dies.
    // The first argument position the command log must hide, -1 for none:
    // the declared sensitive_args_from, or the earliest sensitive()
    // parameter of any declared form - the log runs before a form is
    // picked, so the strictest position wins.
    int sensitiveFrom() const
    {
        int l_from = sensitive_args_from;
        for (const CommandVariant &l_variant : variants) {
            for (int i = 0; i < l_variant.params.size(); ++i) {
                if (l_variant.params.at(i).isSensitive() && (l_from < 0 || i < l_from)) {
                    l_from = i;
                }
            }
        }
        return l_from;
    }

    const CommandVariant *match(int f_argc) const
    {
        for (const CommandVariant &l_variant : variants) {
            if (l_variant.matches(f_argc)) {
                return &l_variant;
            }
        }
        return nullptr;
    }
};

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
