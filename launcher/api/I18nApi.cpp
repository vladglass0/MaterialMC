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

#include "I18nApi.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>

#include "Application.h"
#include "translations/TranslationsModel.h"

#include "ApiParams.h"
#include "ApiRouter.h"
#include "ApiUtils.h"

namespace api {

namespace {

// Context used for strings that only exist in the web UI.
constexpr auto WebUiContext = "WebUi";

// Representative numbers for plural forms: the frontend picks the one in the same CLDR category as the real number.
constexpr int PluralSamples[] = { 0, 1, 2, 3, 5, 11, 21, 22, 25, 101, 102, 111 };

QString stripMnemonic(QString text)
{
    // "Cancel(&C)" (CJK convention) and "&Cancel"
    static const QRegularExpression s_cjk(QStringLiteral("\\(&[^)]\\)"));
    text.remove(s_cjk);
    static const QRegularExpression s_amp(QStringLiteral("&(?=[^&\\s])"));
    text.remove(s_amp);
    return text;
}

/** Looks the string up in the web UI context, then in every Qt context it had; empty if untranslated. */
QString lookup(const QString& source, const QJsonArray& contexts, int n)
{
    auto* model = APPLICATION->translations();
    if (!model) {
        return {};
    }
    if (auto tr = model->rawTranslation(WebUiContext, source, n); !tr.isEmpty()) {
        return tr;
    }
    for (const auto& c : contexts) {
        const auto pair = c.toArray();
        const auto context = pair.at(0).toString().toUtf8();
        const auto original = pair.size() > 1 ? pair.at(1).toString() : source;
        if (context.isEmpty() || original.isEmpty()) {
            continue;
        }
        if (auto tr = model->rawTranslation(context.constData(), original, n); !tr.isEmpty()) {
            return original == source ? tr : stripMnemonic(tr);
        }
    }
    return {};
}

}  // namespace

I18nApi::I18nApi(ApiRouter* router, QObject* parent) : QObject(parent), m_router(router)
{
    router->addSync("i18n.info", [](const QJsonObject&) {
        auto* model = APPLICATION->translations();
        const auto language = model ? model->selectedLanguage() : QStringLiteral("en_US");
        return QJsonObject{ { "language", language }, { "locale", QLocale().bcp47Name() } };
    });

    router->addSync("i18n.catalog", [](const QJsonObject& p) {
        const auto keys = p.value("keys");
        if (!keys.isArray() || keys.toArray().size() > 20000) {
            throw ApiError::invalidParams("'keys' must be an array of at most 20000 items");
        }
        QJsonObject strings;
        QJsonObject plurals;
        for (const auto& value : keys.toArray()) {
            const auto key = value.toObject();
            const auto source = key.value("s").toString();
            if (source.isEmpty() || source.size() > 8192) {
                continue;
            }
            const auto contexts = key.value("c").toArray();
            if (key.value("n").toBool()) {
                QJsonObject forms;
                for (int sample : PluralSamples) {
                    if (auto tr = lookup(source, contexts, sample); !tr.isEmpty()) {
                        forms.insert(QString::number(sample), tr);
                    }
                }
                if (!forms.isEmpty()) {
                    plurals.insert(source, forms);
                }
            } else if (auto tr = lookup(source, contexts, -1); !tr.isEmpty() && tr != source) {
                strings.insert(source, tr);
            }
        }
        auto* model = APPLICATION->translations();
        return QJsonObject{ { "language", model ? model->selectedLanguage() : QStringLiteral("en_US") },
                            { "locale", QLocale().bcp47Name() },
                            { "strings", strings },
                            { "plurals", plurals } };
    });

    // Installing/removing a translator posts LanguageChange to the application object.
    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(200);
    connect(&m_changedTimer, &QTimer::timeout, this, [this] { m_router->emitEvent("i18n.changed"); });
    QCoreApplication::instance()->installEventFilter(this);
}

bool I18nApi::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        m_changedTimer.start();
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace api
