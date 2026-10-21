#include "akashi/config_entry.h"

#include "akashi/helpers/checks.h"

#include <utility>

namespace akashi {

ConfigEntry::ConfigEntry(const QString &f_key, const QVariant &f_default, const QString &f_description, Check f_check) :
    m_key(f_key),
    m_default(f_default),
    m_description(f_description),
    m_check(f_check)
{}

QString ConfigEntry::key() const
{
    return m_key;
}

QVariant ConfigEntry::defaultValue() const
{
    return m_default;
}

int ConfigEntry::typeId() const
{
    return m_default.typeId();
}

QString ConfigEntry::description() const
{
    return m_description;
}

bool ConfigEntry::checkValue(const QVariant &f_value) const
{
    return m_check ? m_check(f_value) : true;
}

// The checks themselves live in akashi.helpers, so a range or a word list
// means the same thing in a config file as anywhere else.
ConfigEntry::Check inRange(double f_min, double f_max)
{
    return helpers::inRange(f_min, f_max);
}

ConfigEntry::Check atLeast(double f_min)
{
    return helpers::atLeast(f_min);
}

ConfigEntry::Check atMost(double f_max)
{
    return helpers::atMost(f_max);
}

ConfigEntry::Check allOf(const QList<ConfigEntry::Check> &f_checks)
{
    return helpers::allOf(f_checks);
}

ConfigEntry::Check oneOf(const QStringList &f_words)
{
    return helpers::oneOf(f_words);
}

ConfigEntry::Check emptyOrTime()
{
    return helpers::emptyOrTime();
}

ConfigEntry::Check url()
{
    return helpers::url();
}

ConfigEntry::Check emptyOr(ConfigEntry::Check f_check)
{
    return helpers::emptyOr(std::move(f_check));
}

} // namespace akashi
