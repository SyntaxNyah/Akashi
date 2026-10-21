#include "core/command_params.h"

#include "akashi/scheduler.h"
#include "core/command_context.h"
#include "core/command_spec.h"

#include <QSet>

#include <limits>

namespace akashi {

using helpers::ParseError;

// Builds parameters of the kinds that carry data, and reads a form.
class FormReader
{
  public:
    static Param integer(const QString &f_name, qint64 f_min, qint64 f_max)
    {
        Param l_param(Param::Kind::Integer, f_name);
        l_param.m_min = f_min;
        l_param.m_max = [f_max](const ParamScope &) { return f_max; };
        return l_param;
    }

    static Param area(const QString &f_name)
    {
        Param l_param(Param::Kind::Integer, f_name);
        l_param.m_subject = QStringLiteral("area_id");
        l_param.m_max = [](const ParamScope &f_scope) { return qint64(f_scope.areaCount() - 1); };
        return l_param;
    }

    static Param word(const QString &f_name, const QStringList &f_words)
    {
        Param l_param(Param::Kind::Word, f_name);
        l_param.m_words = f_words;
        return l_param;
    }

    static Param role(const QString &f_name)
    {
        Param l_param(Param::Kind::Word, f_name);
        l_param.m_subject = QStringLiteral("role");
        l_param.m_word_source = [](const ParamScope &f_scope) { return f_scope.roleIds(); };
        return l_param;
    }

    static Param dice(const QString &f_name, Param::Limit f_max_count, Param::Limit f_max_sides)
    {
        Param l_param(Param::Kind::Dice, f_name);
        l_param.m_max_count = std::move(f_max_count);
        l_param.m_max_sides = std::move(f_max_sides);
        return l_param;
    }

    // Text and When read the rest of the line, so they come last.
    static bool takesRestOfLine(const Param &f_param) { return f_param.m_kind == Param::Kind::Text || f_param.m_kind == Param::Kind::When; }

    // How the parameter reads in a usage line: <target>, [reason...], all.
    static QString placeholder(const Param &f_param);

    static FormReading read(const QList<Param> &f_params, const QStringList &f_arguments, const ParamScope &f_scope);

  private:
    // A value refusal outranks a count refusal at the same point: having
    // read the right number of values, a wrong one says more.
    static int score(int f_read, bool f_value_error) { return f_read * 2 + (f_value_error ? 1 : 0); }

    static std::optional<ParseError> readOne(const Param &f_param, const QString &f_text, const ParamScope &f_scope, Args &f_args);
};

Param::Param(Kind f_kind, const QString &f_name) :
    m_kind(f_kind),
    m_name(f_name),
    m_subject(f_name)
{}

Param Param::optional() const
{
    Param l_copy = *this;
    l_copy.m_optional = true;
    return l_copy;
}

Param Param::sensitive() const
{
    Param l_copy = *this;
    l_copy.m_sensitive = true;
    return l_copy;
}

Param Param::inArea() const
{
    Param l_copy = *this;
    l_copy.m_in_area = true;
    return l_copy;
}

Param Param::immuneIf(const QString &f_permission) const
{
    Param l_copy = *this;
    l_copy.m_immune_permission = f_permission;
    return l_copy;
}

Param Param::immuneIfAreaOwner() const
{
    Param l_copy = *this;
    l_copy.m_immune_area_owner = true;
    return l_copy;
}

Param Param::max(qint64 f_max) const
{
    return max([f_max](const ParamScope &) { return f_max; });
}

Param Param::max(Limit f_max) const
{
    Param l_copy = *this;
    l_copy.m_max = std::move(f_max);
    return l_copy;
}

Param Param::without(const QStringList &f_words) const
{
    Param l_copy = *this;
    l_copy.m_excluded = f_words;
    return l_copy;
}

QString FormReader::placeholder(const Param &f_param)
{
    // Words known only at reading time are shown by the parameter's name.
    const bool l_fixed_words = f_param.m_kind == Param::Kind::Word && !f_param.m_word_source;
    QString l_inner;
    if (l_fixed_words && f_param.m_words.size() == 1) {
        // A single fixed word is typed as it is: "/area_kick all".
        l_inner = f_param.m_words.first();
        return f_param.m_optional ? "[" + l_inner + "]" : l_inner;
    }
    l_inner = l_fixed_words ? f_param.m_words.join(QLatin1Char('|')) : f_param.m_name;
    if (takesRestOfLine(f_param)) {
        l_inner += QStringLiteral("...");
    }
    return f_param.m_optional ? "[" + l_inner + "]" : "<" + l_inner + ">";
}

namespace param {
Param client(const QString &f_name) { return Param(Param::Kind::Client, f_name); }
Param integer(const QString &f_name, qint64 f_min, qint64 f_max) { return FormReader::integer(f_name, f_min, f_max); }
Param area(const QString &f_name) { return FormReader::area(f_name); }
Param word(const QString &f_name, const QStringList &f_words) { return FormReader::word(f_name, f_words); }
Param duration(const QString &f_name) { return Param(Param::Kind::Duration, f_name); }
Param dice(const QString &f_name, Param::Limit f_max_count, Param::Limit f_max_sides) { return FormReader::dice(f_name, std::move(f_max_count), std::move(f_max_sides)); }
Param text(const QString &f_name) { return Param(Param::Kind::Text, f_name); }
Param token(const QString &f_name) { return Param(Param::Kind::Token, f_name); }
Param role(const QString &f_name) { return FormReader::role(f_name); }
Param when(const QString &f_name) { return Param(Param::Kind::When, f_name); }
} // namespace param

bool Args::has(const QString &f_name) const
{
    return m_clients.contains(f_name) || m_numbers.contains(f_name) || m_texts.contains(f_name) || m_dice.contains(f_name) || m_times.contains(f_name);
}

ClientSession *Args::client(const QString &f_name) const { return m_clients.value(f_name); }
TargetPlayer Args::target(const QString &f_name) const { return TargetPlayer(m_clients.value(f_name)); }
qint64 Args::number(const QString &f_name) const { return m_numbers.value(f_name); }
QString Args::text(const QString &f_name) const { return m_texts.value(f_name); }
helpers::Dice Args::dice(const QString &f_name) const { return m_dice.value(f_name); }
QDateTime Args::when(const QString &f_name) const { return m_times.value(f_name); }

std::optional<ParseError> FormReader::readOne(const Param &f_param, const QString &f_text, const ParamScope &f_scope, Args &f_args)
{
    switch (f_param.m_kind) {
    case Param::Kind::Client:
    {
        PlayerFilter l_filter;
        if (f_param.m_in_area)
            l_filter.in_area = f_scope.invokerArea();
        const auto l_found = f_scope.findClient(f_text, l_filter);
        if (!l_found)
            return l_found.error();
        ClientSession *l_client = *l_found;
        const bool l_immune_by_permission = !f_param.m_immune_permission.isEmpty() && f_scope.clientCan(l_client, f_param.m_immune_permission) && !f_scope.invokerCan(QStringLiteral("super"));
        if (l_immune_by_permission || (f_param.m_immune_area_owner && f_scope.ownsInvokerArea(l_client)))
            return ParseError(ParseError::Immune, f_text, QStringLiteral("client_id"));
        f_args.m_clients.insert(f_param.m_name, l_client);
        return std::nullopt;
    }
    case Param::Kind::Integer:
    {
        const qint64 l_max = f_param.m_max ? f_param.m_max(f_scope) : std::numeric_limits<int>::max();
        const auto l_number = helpers::parseInt(f_text, f_param.m_subject, int(f_param.m_min), int(l_max));
        if (!l_number)
            return l_number.error();
        f_args.m_numbers.insert(f_param.m_name, *l_number);
        return std::nullopt;
    }
    case Param::Kind::Word:
    {
        QStringList l_words = f_param.m_word_source ? f_param.m_word_source(f_scope) : f_param.m_words;
        for (const QString &l_excluded : f_param.m_excluded)
            l_words.removeAll(l_excluded);
        const auto l_word = helpers::parseWord(f_text, l_words, f_param.m_subject);
        if (!l_word)
            return l_word.error();
        f_args.m_texts.insert(f_param.m_name, *l_word);
        return std::nullopt;
    }
    case Param::Kind::Duration:
    {
        const qint64 l_max = f_param.m_max ? f_param.m_max(f_scope) : helpers::c_max_duration_seconds;
        const auto l_duration = helpers::parseDuration(f_text, l_max);
        if (!l_duration)
            return l_duration.error();
        f_args.m_numbers.insert(f_param.m_name, *l_duration);
        return std::nullopt;
    }
    case Param::Kind::Dice:
    {
        const auto l_dice = helpers::parseDice(f_text, int(f_param.m_max_count(f_scope)), int(f_param.m_max_sides(f_scope)));
        if (!l_dice)
            return l_dice.error();
        f_args.m_dice.insert(f_param.m_name, *l_dice);
        return std::nullopt;
    }
    case Param::Kind::Token:
    {
        if (f_param.m_max) {
            const auto l_token = helpers::limitLength(f_text, int(f_param.m_max(f_scope)), f_param.m_name);
            if (!l_token)
                return l_token.error();
        }
        f_args.m_texts.insert(f_param.m_name, f_text);
        return std::nullopt;
    }
    case Param::Kind::When:
    {
        const auto l_when = parseWhen(f_text, QDateTime::currentDateTime());
        if (!l_when)
            return ParseError(ParseError::NotATime, f_text, f_param.m_name);
        f_args.m_times.insert(f_param.m_name, *l_when);
        return std::nullopt;
    }
    case Param::Kind::Text:
    {
        if (f_param.m_max) {
            const auto l_text = helpers::limitLength(f_text, int(f_param.m_max(f_scope)), f_param.m_name);
            if (!l_text)
                return l_text.error();
        }
        f_args.m_texts.insert(f_param.m_name, f_text);
        return std::nullopt;
    }
    }
    return std::nullopt;
}

FormReading FormReader::read(const QList<Param> &f_params, const QStringList &f_arguments, const ParamScope &f_scope)
{
    FormReading l_reading;
    Args l_args;
    int l_next = 0;
    for (int i = 0; i < f_params.size(); ++i) {
        const Param &l_param = f_params.at(i);
        if (l_next >= f_arguments.size()) {
            if (l_param.m_optional)
                continue;
            l_reading.error = ParseError(ParseError::MissingValue, {}, l_param.m_name);
            l_reading.score = score(i, false);
            return l_reading;
        }
        const bool l_rest = takesRestOfLine(l_param);
        const QString l_text = l_rest ? helpers::joinFrom(f_arguments, l_next) : f_arguments.at(l_next);
        if (const auto l_error = readOne(l_param, l_text, f_scope, l_args)) {
            l_reading.error = *l_error;
            l_reading.score = score(i, true);
            return l_reading;
        }
        l_next = l_rest ? f_arguments.size() : l_next + 1;
    }
    if (l_next < f_arguments.size()) {
        l_reading.error = ParseError(ParseError::ExtraValue, f_arguments.at(l_next), {});
        l_reading.score = score(f_params.size(), false);
        return l_reading;
    }
    l_reading.args = l_args;
    l_reading.score = score(f_params.size(), false);
    return l_reading;
}

FormReading readForm(const QList<Param> &f_params, const QStringList &f_arguments, const ParamScope &f_scope)
{
    return FormReader::read(f_params, f_arguments, f_scope);
}

QString usageOf(const QString &f_command, const QList<Param> &f_params)
{
    QString l_usage = "/" + f_command;
    for (const Param &l_param : f_params) {
        l_usage += " " + FormReader::placeholder(l_param);
    }
    return l_usage;
}

bool validParams(const QList<Param> &f_params)
{
    QSet<QString> l_names;
    bool l_seen_optional = false;
    for (int i = 0; i < f_params.size(); ++i) {
        const Param &l_param = f_params.at(i);
        if (l_param.name().isEmpty() || l_names.contains(l_param.name())) {
            return false;
        }
        l_names.insert(l_param.name());
        if (FormReader::takesRestOfLine(l_param) && i != f_params.size() - 1) {
            return false;
        }
        if (l_seen_optional && !l_param.isOptional()) {
            return false;
        }
        l_seen_optional = l_seen_optional || l_param.isOptional();
    }
    return true;
}

FormChoice chooseForm(const CommandSpec &f_spec, const QStringList &f_arguments, const ParamScope &f_scope,
                      const std::function<bool(const QString &)> &f_can_perform)
{
    FormChoice l_choice;
    std::optional<FormReading> l_best;
    bool l_closed_form_fits = false;
    for (const CommandVariant &l_variant : f_spec.variants) {
        const FormReading l_reading = readForm(l_variant.params, f_arguments, f_scope);
        if (!l_variant.gate.passes(f_can_perform)) {
            // Only whether it would have fitted is kept: the caller learns
            // nothing of what a closed form's lookups found.
            l_closed_form_fits = l_closed_form_fits || l_reading.args.has_value();
            continue;
        }
        l_choice.usages << l_variant.usage;
        if (l_reading.args && !l_choice.form) {
            l_choice.form = &l_variant;
            l_choice.args = l_reading.args;
        }
        if (l_choice.form) {
            continue;
        }
        if (!l_best || l_reading.score > l_best->score) {
            l_best = l_reading;
        }
        else if (l_reading.score == l_best->score && l_reading.error.error == helpers::ParseError::UnknownWord && l_best->error.error == helpers::ParseError::UnknownWord && l_reading.error.subject == l_best->error.subject) {
            // Forms told apart by a word at the same point: the refusal
            // lists every word that would have been accepted there.
            for (const QString &l_word : l_reading.error.choices) {
                if (!l_best->error.choices.contains(l_word))
                    l_best->error.choices << l_word;
            }
        }
    }
    if (l_choice.form) {
        return l_choice;
    }
    if (!l_best || l_closed_form_fits) {
        l_choice.denied = true;
        return l_choice;
    }
    l_choice.error = l_best->error;
    return l_choice;
}

} // namespace akashi
