#pragma once

#include "akashi/helpers/checks.h"
#include "akashi_core_export.h"

#include <QString>
#include <QVariant>

#include <functional>

namespace akashi {

// One declared setting: key, typed default, description and an optional value check.
class AKASHI_CORE_EXPORT ConfigEntry
{
  public:
    using Check = helpers::Check;

    ConfigEntry(const QString &f_key, const QVariant &f_default, const QString &f_description, Check f_check = {});

    QString key() const;
    QVariant defaultValue() const;
    // The type of a setting is the type of its default value.
    int typeId() const;
    QString description() const;
    bool checkValue(const QVariant &f_value) const;

  private:
    QString m_key;
    QVariant m_default;
    QString m_description;
    Check m_check;
};

// The value checks live in akashi.helpers, so a range or a word list means
// the same thing in a config file as anywhere else.
using helpers::allOf;
using helpers::atLeast;
using helpers::atMost;
using helpers::emptyOr;
using helpers::emptyOrTime;
using helpers::inRange;
using helpers::oneOf;
using helpers::url;

} // namespace akashi
