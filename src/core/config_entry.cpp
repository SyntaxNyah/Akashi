#include "akashi/config_entry.h"

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

} // namespace akashi
