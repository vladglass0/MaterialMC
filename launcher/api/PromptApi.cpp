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

#include "PromptApi.h"

#include <QJsonArray>

#include "ApiParams.h"
#include "ApiRouter.h"
#include "ApiUtils.h"

namespace api {

namespace {
QString roleName(interaction::ButtonRole role)
{
    switch (role) {
        case interaction::ButtonRole::Accept:
            return QStringLiteral("accept");
        case interaction::ButtonRole::Reject:
            return QStringLiteral("reject");
        case interaction::ButtonRole::Destructive:
            return QStringLiteral("destructive");
        case interaction::ButtonRole::Neutral:
            break;
    }
    return QStringLiteral("neutral");
}
}  // namespace

PromptApi::PromptApi(ApiRouter* router, QObject* parent) : QObject(parent), m_router(router)
{
    router->addSync("prompts.pending", [](const QJsonObject&) {
        QJsonArray out;
        for (const auto& [id, prompt] : interaction::UserInteraction::instance()->pending()) {
            out.append(serialize(id, prompt));
        }
        return out;
    });

    router->addSync("prompts.answer", [](const QJsonObject& p) {
        const auto id = static_cast<int>(params::requireInt(p, "promptId", 1));
        interaction::Answer answer;
        answer.button = params::optionalString(p, "button", 64).value_or(QString());
        answer.checked = params::optionalBool(p, "checked", false);
        if (!params::isMissing(p.value("data"))) {
            answer.data = params::requireObject(p, "data");
        }
        if (!interaction::UserInteraction::instance()->answer(id, answer)) {
            throw ApiError::notFound(QStringLiteral("Prompt %1 is not open").arg(id));
        }
        return ok();
    });

    router->addSync("prompts.action", [](const QJsonObject& p) {
        const auto id = static_cast<int>(params::requireInt(p, "promptId", 1));
        const auto action = params::requireNonEmpty(p, "action", 64);
        QJsonObject data;
        if (!params::isMissing(p.value("data"))) {
            data = params::requireObject(p, "data");
        }
        if (!interaction::UserInteraction::instance()->action(id, action, data)) {
            throw ApiError::notFound(QStringLiteral("Prompt %1 is not open or has no actions").arg(id));
        }
        return ok();
    });

    interaction::UserInteraction::instance()->setPresenter(this);
}

PromptApi::~PromptApi()
{
    interaction::UserInteraction::instance()->setPresenter(nullptr);
}

QJsonObject PromptApi::serialize(int id, const interaction::Prompt& prompt)
{
    QJsonArray buttons;
    for (const auto& b : prompt.buttons) {
        buttons.append(QJsonObject{ { "id", b.id }, { "label", b.label }, { "role", roleName(b.role) } });
    }
    QJsonObject out{ { "id", id },           { "kind", prompt.kind },       { "title", prompt.title },
                     { "text", prompt.text }, { "icon", prompt.icon },       { "buttons", buttons },
                     { "payload", prompt.payload } };
    out.insert("defaultButton", prompt.defaultButton.isEmpty() ? QJsonValue() : QJsonValue(prompt.defaultButton));
    out.insert("checkbox", prompt.checkbox.isEmpty() ? QJsonValue() : QJsonValue(prompt.checkbox));
    return out;
}

void PromptApi::show(int id, const interaction::Prompt& prompt)
{
    m_router->emitEvent("prompt.show", serialize(id, prompt));
}

void PromptApi::update(int id, const QJsonObject& payload)
{
    m_router->emitEvent("prompt.update", QJsonObject{ { "id", id }, { "payload", payload } });
}

void PromptApi::close(int id)
{
    m_router->emitEvent("prompt.close", QJsonObject{ { "id", id } });
}

}  // namespace api
