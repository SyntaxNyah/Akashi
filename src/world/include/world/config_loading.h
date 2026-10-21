#pragma once

#include "akashi/area_rule.h"
#include "akashi_core_export.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <utility>

namespace akashi {
namespace config {

// Song name to its category and duration, as loaded from music.json.
using MusicList = QHash<QString, std::pair<QString, int>>;

struct MusicCatalog
{
    MusicList songs;
    QStringList ordered;
};

// One rule as declared in areas.json; the action is looked up in the
// RuleRegistry when the declaration is applied, not when it is parsed.
struct RuleDeclaration
{
    QString event;
    RulePhase phase = RulePhase::Before;
    QString action;
    QVariantMap args;
};

struct AreaRulesConfig
{
    QHash<QString, QVector<RuleDeclaration>> floor_rules;
    QHash<int, QVector<RuleDeclaration>> area_rules;
};

// One standing offer as declared in a "grants" object: the key names a
// permission or an @group reference (expanded when applied), the value
// an audience - "everyone", "participants" or "role:<id>".
struct GrantDeclaration
{
    QString permission;
    QString audience;
};

struct AreaGrantsConfig
{
    QHash<QString, QVector<GrantDeclaration>> floor_grants;
    QHash<int, QVector<GrantDeclaration>> area_grants;
};

// The area sections of an areas file - keys with a numeric prefix, like
// "2:Courtroom" - in area order: by prefix, then by key. An area's id is
// its position in this list, not its prefix, so gaps in the numbering
// ("0:", "2:", "5:") still give ids 0, 1 and 2.
AKASHI_CORE_EXPORT QStringList areaSections(const QStringList &f_keys);

AKASHI_CORE_EXPORT QStringList loadTextFile(const QString &f_path);
AKASHI_CORE_EXPORT MusicCatalog loadMusicList(const QString &f_path);
AKASHI_CORE_EXPORT AreaRulesConfig loadAreaRules(const QString &f_path);
AKASHI_CORE_EXPORT AreaGrantsConfig loadAreaGrants(const QString &f_path);
AKASHI_CORE_EXPORT QStringList loadIpRangeBans(const QString &f_path);
AKASHI_CORE_EXPORT QList<quint32> loadBannedAsns(const QString &f_path);

} // namespace config
} // namespace akashi
