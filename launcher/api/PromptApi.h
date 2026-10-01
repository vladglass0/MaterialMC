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

#include "interaction/UserInteraction.h"

namespace api {

class ApiRouter;

/**
 * Shows backend prompts (interaction::UserInteraction) in the web UI.
 *
 * Events: `prompt.show` (a Prompt), `prompt.update` ({ id, payload }), `prompt.close` ({ id }).
 * Methods: `prompts.pending`, `prompts.answer`, `prompts.action`.
 */
class PromptApi : public QObject, public interaction::PromptPresenter {
    Q_OBJECT
   public:
    explicit PromptApi(ApiRouter* router, QObject* parent = nullptr);
    ~PromptApi() override;

    void show(int id, const interaction::Prompt& prompt) override;
    void update(int id, const QJsonObject& payload) override;
    void close(int id) override;

    static QJsonObject serialize(int id, const interaction::Prompt& prompt);

   private:
    ApiRouter* m_router;
};

}  // namespace api
