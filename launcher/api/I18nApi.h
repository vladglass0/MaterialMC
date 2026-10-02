// SPDX-License-Identifier: GPL-3.0-only
/*
 *  MaterialMC - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QObject>
#include <QTimer>

namespace api {

class ApiRouter;

/**
 * Translations for the web UI, served from the launcher's own translation files (the Prism Launcher translations).
 *
 * The frontend marks strings with t()/tn(); a build step records, for each string, the Qt contexts that string had in the
 * Qt Widgets code (frontend/i18n/qt-contexts.json). `i18n.catalog` resolves them here with the loaded QTranslator, so
 * strings shared with the old UI are translated without new translation work. `i18n.changed` fires when the language changes.
 */
class I18nApi : public QObject {
    Q_OBJECT
   public:
    explicit I18nApi(ApiRouter* router, QObject* parent = nullptr);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    ApiRouter* m_router;
    QTimer m_changedTimer;
};

}  // namespace api
