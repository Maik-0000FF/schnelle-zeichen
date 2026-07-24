// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileListModel.h"
#include "editor_paths.h"
#include "preset_meta.h"
#include "preset_paths.h"

#include "combo_parse.h"
#include "core/profile_paths.h"
#include "core/profiles_io.h"

#include <xkbcommon/xkbcommon.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSaveFile>
#include <QVariantMap>

#include <cstdio>

namespace {

using schnelle_zeichen::kMappingsFile;
using schnelle_zeichen::kProfilesConf;
using schnelle_zeichen::kProfilesSubdir;
using schnelle_zeichen::kStandardProfile;

// Longest slug accepted, so a pathological profile name can't produce a
// filename that hits the filesystem's NAME_MAX on save.
constexpr int kMaxSlugLength = 64;

QString configDir() { return schnelle_zeichen::configDirPath(); }

QString profilesConfPath() {
    return configDir() + QLatin1String(kProfilesConf);
}

// Read profiles.conf via the shared parser (the same format the engine
// reads); a missing file yields empty data.
schnelle_zeichen::ProfilesData readProfilesConf() {
    schnelle_zeichen::ProfilesData data;
    if (FILE *fp = std::fopen(profilesConfPath().toUtf8().constData(), "r")) {
        data = schnelle_zeichen::parseProfiles(fp);
        std::fclose(fp);
    }
    return data;
}

} // namespace

// Delegates to the shared predicate (core/profile_paths.h) so the editor and
// the engine apply the exact same "File must stay under profiles/" rule.
bool ProfileListModel::isSafeProfileFile(const QString &file) {
    return schnelle_zeichen::isSafeProfileFile(file.toStdString());
}

ProfileListModel::ProfileListModel(QObject *parent)
    : QAbstractListModel(parent), confWatcher_(new QFileSystemWatcher(this)) {
    load();
    // Watch profiles.conf so a runtime profile switch (the engine writes
    // Active= on a shortcut) updates the active marker live, not just on the
    // editor's own dropdown actions. load() above creates the file when it is
    // missing, so the path exists to watch by now.
    const QString conf = profilesConfPath();
    if (QFile::exists(conf))
        confWatcher_->addPath(conf);
    connect(confWatcher_, &QFileSystemWatcher::fileChanged, this,
            &ProfileListModel::onProfilesConfChanged);
}

void ProfileListModel::onProfilesConfChanged() {
    // Both the engine and the editor rewrite profiles.conf via a temp-file
    // rename, which replaces the inode and drops the watch after one
    // notification. Re-add the path so later switches keep firing.
    const QString conf = profilesConfPath();
    if (confWatcher_ && QFile::exists(conf) &&
        !confWatcher_->files().contains(conf))
        confWatcher_->addPath(conf);
    // Adopt the active profile from disk and refresh the marker. This reads
    // only the Active key, so the editor's own writes (same value) are a no-op.
    reloadActiveFromDisk();
}

int ProfileListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(entries_.size());
}

QVariant ProfileListModel::data(const QModelIndex &index, int role) const {
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return {};
    const auto &e = entries_[row];
    switch (role) {
    case NameRole:
        return e.name;
    case FileRole:
        return e.file;
    case SelectKeyRole:
        return e.selectKey;
    case IsActiveRole:
        return e.name == active_;
    case IsProtectedRole:
        return isProtected(row);
    case FavoriteRole:
        return e.favorite;
    default:
        return {};
    }
}

QHash<int, QByteArray> ProfileListModel::roleNames() const {
    return {
        {NameRole, "name"},
        {FileRole, "file"},
        {SelectKeyRole, "selectKey"},
        {IsActiveRole, "isActive"},
        {IsProtectedRole, "isProtected"},
        {FavoriteRole, "favorite"},
    };
}

QString ProfileListModel::normalizedName(const QString &name) {
    return name.trimmed().toLower();
}

QString ProfileListModel::slugify(const QString &name) {
    QString slug;
    bool lastDash = false;
    for (QChar c : name.toLower()) {
        if ((c >= QChar('a') && c <= QChar('z')) ||
            (c >= QChar('0') && c <= QChar('9'))) {
            slug += c;
            lastDash = false;
        } else if (!slug.isEmpty() && !lastDash) {
            slug += QChar('-');
            lastDash = true;
        }
    }
    if (slug.size() > kMaxSlugLength)
        slug.truncate(kMaxSlugLength);
    while (slug.endsWith(QChar('-')))
        slug.chop(1);
    if (slug.isEmpty())
        slug = QStringLiteral("profile");
    return slug;
}

bool ProfileListModel::nameExists(const QString &name, int excludeRow) const {
    QString norm = normalizedName(name);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (i == excludeRow)
            continue;
        if (normalizedName(entries_[i].name) == norm)
            return true;
    }
    return false;
}

bool ProfileListModel::fileExists(const QString &file, int excludeRow) const {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (i == excludeRow)
            continue;
        if (entries_[i].file == file)
            return true;
    }
    return false;
}

// Build a unique "profiles/<slug>.txt" for a display name, suffixing -2, -3 ...
// if the base slug (or an existing file on disk) would collide.
QString ProfileListModel::uniqueSlugFile(const QString &name) const {
    return uniqueFileForBase(slugify(name));
}

QString ProfileListModel::uniqueFileForBase(const QString &base) const {
    QDir dir(configDir() + QLatin1String(kProfilesSubdir));
    const QString prefix = QLatin1String(kProfilesSubdir) + QStringLiteral("/");
    int n = 1;
    for (;;) {
        QString slug =
            (n == 1) ? base : base + QStringLiteral("-") + QString::number(n);
        QString rel = prefix + slug + QStringLiteral(".txt");
        if (!fileExists(rel, -1) && !dir.exists(slug + QStringLiteral(".txt")))
            return rel;
        ++n;
    }
}

QString ProfileListModel::uniqueName(const QString &base) const {
    if (!nameExists(base, -1))
        return base;
    for (int n = 2;; ++n) {
        QString cand = base + QChar(' ') + QString::number(n);
        if (!nameExists(cand, -1))
            return cand;
    }
}

QString ProfileListModel::nameErrorFor(const QString &name,
                                       int excludeRow) const {
    QString t = name.trimmed();
    if (t.isEmpty())
        return tr("Name must not be empty");
    if (t.contains(QChar('/')) || t.contains(QChar('\\')) ||
        t.contains(QChar('\n')) || t.contains(QChar('\r')))
        return tr("Name must not contain slashes or line breaks");
    if (nameExists(t, excludeRow))
        return tr("A profile with this name already exists");
    return {};
}

bool ProfileListModel::isProtected(int row) const {
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return true;
    // The Standard profile (its file holds the user's original mappings.txt)
    // and the last remaining profile cannot be deleted.
    if (entries_[row].file == QLatin1String(kMappingsFile))
        return true;
    return entries_.size() <= 1;
}

QStringList ProfileListModel::profileNames() const {
    QStringList names;
    names.reserve(static_cast<int>(entries_.size()));
    for (const auto &e : entries_)
        names << e.name;
    return names;
}

QString ProfileListModel::fileForRow(int row) const {
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return QLatin1String(kMappingsFile);
    return entries_[row].file;
}

int ProfileListModel::activeRow() const {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        if (entries_[i].name == active_)
            return i;
    return entries_.empty() ? -1 : 0;
}

bool ProfileListModel::createProfile(const QString &name) {
    reloadActiveFromDisk();
    QString err = nameErrorFor(name, -1);
    if (!err.isEmpty()) {
        Q_EMIT errorOccurred(err);
        return false;
    }
    Entry e;
    e.name = name.trimmed();
    e.file = uniqueSlugFile(e.name);
    int row = static_cast<int>(entries_.size());
    beginInsertRows(QModelIndex(), row, row);
    entries_.push_back(e);
    endInsertRows();
    Q_EMIT countChanged();
    // No mapping file is written here: a new profile starts empty (the German
    // defaults are seeded only for the Standard profile, in both the editor
    // model and the engine loader). The file is created on the first save once
    // the user adds mappings.
    save();
    return true;
}

QVariantList ProfileListModel::availablePresets() const {
    QVariantList out;
    const QString dirPath = schnelle_zeichen::presetsDir();
    if (dirPath.isEmpty())
        return out;
    QDir dir(dirPath);
    const QStringList files =
        dir.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString full = dir.filePath(fileName);
        const QString base = QFileInfo(fileName).completeBaseName();
        const schnelle_zeichen::PresetMeta meta =
            schnelle_zeichen::readPresetMeta(full, base);
        QVariantMap m;
        m.insert(QStringLiteral("file"), full);
        m.insert(QStringLiteral("name"), meta.name);
        m.insert(QStringLiteral("description"), meta.description);
        m.insert(QStringLiteral("category"), meta.category);
        m.insert(QStringLiteral("count"), meta.mappingCount);
        out.append(m);
    }
    return out;
}

bool ProfileListModel::addProfileFromPreset(const QString &presetFile) {
    reloadActiveFromDisk();
    // Defense in depth: only ever copy from inside the resolved presets dir,
    // never an arbitrary path handed across the QML boundary.
    const QString dirPath = schnelle_zeichen::presetsDir();
    const QString canonDir = QFileInfo(dirPath).canonicalFilePath();
    const QString canonSrc = QFileInfo(presetFile).canonicalFilePath();
    if (canonDir.isEmpty() || canonSrc.isEmpty() ||
        !canonSrc.startsWith(canonDir + QChar('/'))) {
        Q_EMIT errorOccurred(tr("Unknown preset"));
        return false;
    }

    const QString base = QFileInfo(canonSrc).completeBaseName();
    const schnelle_zeichen::PresetMeta meta =
        schnelle_zeichen::readPresetMeta(canonSrc, base);

    Entry e;
    e.name = uniqueName(meta.name);
    e.file = uniqueFileForBase(slugify(base));

    // Copy the preset's mappings into the user's writable config (verbatim, so
    // a later re-read keeps the header). The unique name guarantees the
    // destination does not yet exist; mkpath creates profiles/ on first use.
    const QString dst = configDir() + e.file;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!QFile::copy(canonSrc, dst)) {
        Q_EMIT errorOccurred(tr("Could not copy the preset"));
        return false;
    }
    // QFile::copy preserves the source's mode bits; a preset on a read-only
    // install (e.g. the Nix store, where files are 0444) would otherwise leave
    // the user's own copy read-only. Make it user-writable so editing it later
    // behaves like any hand-created profile file.
    QFile::setPermissions(dst,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ReadGroup | QFileDevice::ReadOther);

    int row = static_cast<int>(entries_.size());
    beginInsertRows(QModelIndex(), row, row);
    entries_.push_back(e);
    endInsertRows();
    Q_EMIT countChanged();
    save();
    return true;
}

bool ProfileListModel::renameProfile(int row, const QString &name) {
    reloadActiveFromDisk();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return false;
    QString err = nameErrorFor(name, row);
    if (!err.isEmpty()) {
        Q_EMIT errorOccurred(err);
        return false;
    }
    QString old = entries_[row].name;
    entries_[row].name = name.trimmed();
    // The File stays stable (Standard keeps mappings.txt); only the display
    // name changes. Keep the active pointer in sync if we renamed the active.
    if (active_ == old)
        active_ = entries_[row].name;
    auto idx = index(row);
    Q_EMIT dataChanged(idx, idx, {NameRole, IsActiveRole});
    Q_EMIT activeChanged();
    save();
    return true;
}

bool ProfileListModel::removeProfile(int row) {
    reloadActiveFromDisk();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return false;
    if (isProtected(row)) {
        Q_EMIT errorOccurred(tr("This profile cannot be deleted"));
        return false;
    }
    QString file = entries_[row].file;
    bool wasActive = (entries_[row].name == active_);
    beginRemoveRows(QModelIndex(), row, row);
    entries_.erase(entries_.begin() + row);
    endRemoveRows();
    Q_EMIT countChanged();
    // Delete the profile's mappings file (never mappings.txt: Standard is
    // protected above, and isSafeProfileFile keeps file under profiles/, so a
    // malformed entry can't aim QFile::remove outside the config dir).
    if (file != QLatin1String(kMappingsFile) && isSafeProfileFile(file)) {
        QFile::remove(configDir() + file);
    }
    // If the active profile was removed, fall back to the first profile and
    // refresh that row's active marker (the delegate's isActive only updates
    // on dataChanged).
    if (wasActive) {
        active_ = entries_.empty() ? QLatin1String(kStandardProfile)
                                   : entries_.front().name;
        Q_EMIT activeChanged();
        if (!entries_.empty()) {
            auto idx = index(0);
            Q_EMIT dataChanged(idx, idx, {IsActiveRole});
        }
    }
    // Let the editor reset its edit target if it was pointing at this file.
    // The merge manifest's own lifecycle (dissolve/prune) is handled by
    // MergeManifestModel, driven off this same profileRemoved signal.
    Q_EMIT profileRemoved(file);
    save();
    return true;
}

bool ProfileListModel::setActiveRow(int row) {
    reloadActiveFromDisk();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return false;
    if (entries_[row].name == active_)
        return true;
    QString old = active_;
    active_ = entries_[row].name;
    Q_EMIT activeChanged();
    // Refresh isActive on the old and new rows.
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[i].name == active_ || entries_[i].name == old) {
            auto idx = index(i);
            Q_EMIT dataChanged(idx, idx, {IsActiveRole});
        }
    }
    save();
    return true;
}

QString ProfileListModel::canonicalCombo(const QString &combo) {
    // Canonicalise through the engine's own combo parser, so comparing these
    // strings is the engine's notion of "same shortcut" and the editor's
    // duplicate check can't disagree with the runtime matcher. (A hand-rolled
    // string form would be a third, independent equality that could drift from
    // both the capture writer and the engine.) An unparseable combo falls back
    // to its raw text.
    const schnelle_zeichen::ShortcutCombo parsed =
        schnelle_zeichen::parseShortcutCombo(combo.toStdString());
    if (!parsed.valid())
        return combo;
    return QStringLiteral("%1|%2")
        .arg(parsed.modifiers)
        .arg(xkb_keysym_to_lower(parsed.keysym));
}

bool ProfileListModel::isComboFree(const QString &combo, int excludeRow,
                                   CycleSlot excludeCycle) const {
    if (combo.isEmpty())
        return true;
    const QString c = canonicalCombo(combo);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (i == excludeRow)
            continue;
        if (!entries_[i].selectKey.isEmpty() &&
            canonicalCombo(entries_[i].selectKey) == c)
            return false;
    }
    if (excludeCycle != CycleSlot::Next && !cycleNext_.isEmpty() &&
        canonicalCombo(cycleNext_) == c)
        return false;
    if (excludeCycle != CycleSlot::Prev && !cyclePrev_.isEmpty() &&
        canonicalCombo(cyclePrev_) == c)
        return false;
    return true;
}

bool ProfileListModel::setSelectKey(int row, const QString &combo) {
    reloadActiveFromDisk();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return false;
    QString c = combo.trimmed();
    if (!isComboFree(c, row, CycleSlot::None)) {
        Q_EMIT errorOccurred(tr("Shortcut already in use"));
        return false;
    }
    entries_[row].selectKey = c;
    auto idx = index(row);
    Q_EMIT dataChanged(idx, idx, {SelectKeyRole});
    save();
    return true;
}

bool ProfileListModel::setFavorite(int row, bool favorite) {
    reloadActiveFromDisk();
    if (row < 0 || row >= static_cast<int>(entries_.size()))
        return false;
    if (entries_[row].favorite == favorite)
        return true;
    entries_[row].favorite = favorite;
    auto idx = index(row);
    Q_EMIT dataChanged(idx, idx, {FavoriteRole});
    save();
    return true;
}

void ProfileListModel::setCycleNext(const QString &combo) {
    reloadActiveFromDisk();
    QString c = combo.trimmed();
    if (c == cycleNext_)
        return;
    if (!isComboFree(c, -1, CycleSlot::Next)) {
        Q_EMIT errorOccurred(tr("Shortcut already in use"));
        return;
    }
    cycleNext_ = c;
    Q_EMIT cycleNextChanged();
    save();
}

void ProfileListModel::setCyclePrev(const QString &combo) {
    reloadActiveFromDisk();
    QString c = combo.trimmed();
    if (c == cyclePrev_)
        return;
    if (!isComboFree(c, -1, CycleSlot::Prev)) {
        Q_EMIT errorOccurred(tr("Shortcut already in use"));
        return;
    }
    cyclePrev_ = c;
    Q_EMIT cyclePrevChanged();
    save();
}

void ProfileListModel::seedStandardIfEmpty(bool persist) {
    if (!entries_.empty())
        return;
    Entry e;
    e.name = QLatin1String(kStandardProfile);
    e.file = QLatin1String(kMappingsFile);
    entries_.push_back(e);
    if (active_.isEmpty())
        active_ = e.name;
    if (persist)
        save();
}

int ProfileListModel::registerLooseProfiles() {
    QDir dir(configDir() + QLatin1String(kProfilesSubdir));
    if (!dir.exists())
        return 0;
    const QString prefix = QLatin1String(kProfilesSubdir) + QStringLiteral("/");
    const QStringList files =
        dir.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
    int added = 0;
    for (const QString &fileName : files) {
        const QString rel = prefix + fileName;
        // Skip files already registered in profiles.conf, and any name the
        // shared rule rejects (keeps the editor and engine in agreement).
        if (fileExists(rel, -1) || !isSafeProfileFile(rel))
            continue;
        const QString base = QFileInfo(fileName).completeBaseName();
        const schnelle_zeichen::PresetMeta meta =
            schnelle_zeichen::readPresetMeta(dir.filePath(fileName), base);
        // A file with no derivable name (e.g. a literal ".txt": empty base and
        // no "# Name:" header) would be adopted with an empty Name=, which
        // the shared parser then rejects on the next load, so it gets
        // re-adopted and re-saved on every startup. Skip it instead of
        // churning.
        if (meta.name.trimmed().isEmpty())
            continue;
        Entry e;
        e.name = uniqueName(meta.name);
        e.file = rel;
        entries_.push_back(e);
        ++added;
    }
    return added;
}

void ProfileListModel::load() {
    entries_.clear();
    active_.clear();
    cycleNext_.clear();
    cyclePrev_.clear();

    // The shared parser already applies entry validation (non-empty name and
    // file, safe file, no duplicates), exactly what the engine accepts.
    const schnelle_zeichen::ProfilesData data = readProfilesConf();
    for (const auto &e : data.entries) {
        entries_.push_back({QString::fromStdString(e.name),
                            QString::fromStdString(e.file),
                            QString::fromStdString(e.selectKey), e.favorite});
    }
    active_ = QString::fromStdString(data.active);
    cycleNext_ = QString::fromStdString(data.cycleNext);
    cyclePrev_ = QString::fromStdString(data.cyclePrev);

    // Fresh install / corrupt file: materialize the Standard profile
    // (pointing at the existing mappings.txt, which is never rewritten here).
    bool dirty = false;
    if (entries_.empty()) {
        // Drop any dangling Active read from an otherwise-empty/corrupt file so
        // the seeded Standard becomes active.
        active_.clear();
        seedStandardIfEmpty(/*persist=*/false);
        dirty = true;
    }
    // Pick up loose profiles/*.txt the user dropped in (drop-in profiles) that
    // profiles.conf does not list yet, and register them so they become usable.
    if (registerLooseProfiles() > 0)
        dirty = true;
    // Unknown/absent active falls back to the first profile. activeRow() yields
    // 0 (not -1) for any non-empty list, so it can't flag an active_ that names
    // no entry (a dangling Active= or a hand-edited typo); test membership
    // directly so the stale name is actually corrected.
    bool activeKnown = false;
    for (const auto &e : entries_)
        if (e.name == active_) {
            activeKnown = true;
            break;
        }
    if (!activeKnown)
        active_ = entries_.front().name;
    // Persist once if the Standard was seeded or loose profiles were adopted;
    // the engine picks the new entries up via its config watcher.
    if (dirty)
        save();
}

void ProfileListModel::reloadActiveFromDisk() {
    const schnelle_zeichen::ProfilesData data = readProfilesConf();
    const QString diskActive = QString::fromStdString(data.active);

    if (diskActive.isEmpty() || diskActive == active_)
        return;
    // Adopt only if it names a profile we know; otherwise keep the current one.
    bool known = false;
    for (const auto &e : entries_) {
        if (e.name == diskActive) {
            known = true;
            break;
        }
    }
    if (!known)
        return;
    const QString old = active_;
    active_ = diskActive;
    Q_EMIT activeChanged();
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[i].name == active_ || entries_[i].name == old) {
            auto idx = index(i);
            Q_EMIT dataChanged(idx, idx, {IsActiveRole});
        }
    }
}

bool ProfileListModel::save() {
    QString path = profilesConfPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT errorOccurred(file.errorString());
        return false;
    }
    // Serialize through the shared writer so engine writes (an Active switch)
    // and editor writes round-trip byte-identically.
    schnelle_zeichen::ProfilesData data;
    data.active = active_.toStdString();
    data.cycleNext = cycleNext_.toStdString();
    data.cyclePrev = cyclePrev_.toStdString();
    data.entries.reserve(entries_.size());
    for (const auto &e : entries_) {
        data.entries.push_back({e.name.toStdString(), e.file.toStdString(),
                                e.selectKey.toStdString(), e.favorite});
    }
    const QByteArray buf =
        QByteArray::fromStdString(schnelle_zeichen::serializeProfiles(data));
    if (file.write(buf) != buf.size() || !file.commit()) {
        Q_EMIT errorOccurred(file.errorString());
        return false;
    }
    ++revision_;
    Q_EMIT changed();
    // No engine-reload call: the engine watches the config dir and reloads
    // itself on the atomic replace.
    return true;
}
