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

#include "UserInteraction.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QInputDialog>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

namespace interaction {

UserInteraction::UserInteraction(QObject* parent) : QObject(parent) {}

UserInteraction* UserInteraction::instance()
{
    static QPointer<UserInteraction> s_instance;
    if (!s_instance) {
        s_instance = new UserInteraction(QCoreApplication::instance());
    }
    return s_instance;
}

void UserInteraction::setPresenter(PromptPresenter* presenter)
{
    m_presenter = presenter;
    if (m_presenter) {
        // prompts asked before the UI was ready
        for (auto it = m_open.cbegin(); it != m_open.cend(); ++it) {
            m_presenter->show(it.key(), it.value().prompt);
        }
    }
}

int UserInteraction::ask(const Prompt& prompt, Callback callback, ActionHandler onAction)
{
    const int id = m_nextId++;
    m_open.insert(id, Entry{ prompt, std::move(callback), std::move(onAction) });
    qDebug() << "Prompt" << id << prompt.kind << prompt.title;
    if (m_presenter) {
        m_presenter->show(id, prompt);
    } else {
        // defer, so callers always get the id before the answer
        QTimer::singleShot(0, this, [this, id] { showNative(id); });
    }
    return id;
}

Answer UserInteraction::askBlocking(const Prompt& prompt, ActionHandler onAction)
{
    Answer result;
    QEventLoop loop;
    bool done = false;
    ask(
        prompt,
        [&](const Answer& a) {
            result = a;
            done = true;
            loop.quit();
        },
        std::move(onAction));
    if (!done) {
        loop.exec(QEventLoop::DialogExec);
    }
    return result;
}

void UserInteraction::update(int id, const QJsonObject& payload)
{
    auto it = m_open.find(id);
    if (it == m_open.end()) {
        return;
    }
    for (auto p = payload.begin(); p != payload.end(); ++p) {
        it->prompt.payload.insert(p.key(), p.value());
    }
    if (m_presenter) {
        m_presenter->update(id, payload);
    }
}

void UserInteraction::finish(int id, const Answer& answer)
{
    if (m_open.contains(id) && m_presenter) {
        m_presenter->close(id);
    }
    this->answer(id, answer);
}

bool UserInteraction::answer(int id, const Answer& answer)
{
    auto it = m_open.find(id);
    if (it == m_open.end()) {
        return false;
    }
    auto callback = std::move(it->callback);
    m_open.erase(it);
    qDebug() << "Prompt" << id << "answered" << (answer.button.isEmpty() ? QStringLiteral("<dismissed>") : answer.button);
    if (callback) {
        callback(answer);
    }
    return true;
}

bool UserInteraction::action(int id, const QString& action, const QJsonObject& data)
{
    auto it = m_open.find(id);
    if (it == m_open.end() || !it->onAction) {
        return false;
    }
    auto handler = it->onAction;  // the handler may finish the prompt
    handler(action, data);
    return true;
}

QList<std::pair<int, Prompt>> UserInteraction::pending() const
{
    QList<std::pair<int, Prompt>> out;
    for (auto it = m_open.cbegin(); it != m_open.cend(); ++it) {
        out.append({ it.key(), it.value().prompt });
    }
    return out;
}

void UserInteraction::showNative(int id)
{
    auto it = m_open.find(id);
    if (it == m_open.end()) {
        return;
    }
    if (m_presenter) {  // a UI appeared in the meantime
        m_presenter->show(id, it->prompt);
        return;
    }
    const Prompt prompt = it->prompt;
    Answer answer;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        qWarning() << "No UI to ask" << prompt.title << "- using the default answer";
        answer.button = prompt.defaultButton;
        this->answer(id, answer);
        return;
    }

    if (prompt.kind == QLatin1String("message")) {
        QMessageBox box;
        box.setWindowTitle(prompt.title);
        box.setText(prompt.text);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        if (prompt.icon == QLatin1String("warning")) {
            box.setIcon(QMessageBox::Warning);
        } else if (prompt.icon == QLatin1String("error")) {
            box.setIcon(QMessageBox::Critical);
        } else if (prompt.icon == QLatin1String("question")) {
            box.setIcon(QMessageBox::Question);
        } else {
            box.setIcon(QMessageBox::Information);
        }
        QHash<QAbstractButton*, QString> ids;
        for (const auto& b : prompt.buttons) {
            QMessageBox::ButtonRole role = QMessageBox::ActionRole;
            switch (b.role) {
                case ButtonRole::Accept:
                    role = QMessageBox::AcceptRole;
                    break;
                case ButtonRole::Reject:
                    role = QMessageBox::RejectRole;
                    break;
                case ButtonRole::Destructive:
                    role = QMessageBox::DestructiveRole;
                    break;
                case ButtonRole::Neutral:
                    break;
            }
            auto* button = box.addButton(b.label, role);
            ids.insert(button, b.id);
            if (b.id == prompt.defaultButton) {
                box.setDefaultButton(button);
            }
        }
        QCheckBox* check = nullptr;
        if (!prompt.checkbox.isEmpty()) {
            check = new QCheckBox(prompt.checkbox);
            box.setCheckBox(check);
        }
        box.exec();
        answer.button = ids.value(box.clickedButton());
        answer.checked = check && check->isChecked();
    } else if (prompt.kind == QLatin1String("text")) {
        bool ok = false;
        const auto value = QInputDialog::getText(nullptr, prompt.title, prompt.text, QLineEdit::Normal,
                                                 prompt.payload.value("value").toString(), &ok);
        if (ok) {
            answer.button = QStringLiteral("ok");
            answer.data.insert("value", value);
        }
    } else {
        // Rich prompts need the web UI; without it, behave like the user cancelled.
        qWarning() << "Prompt kind" << prompt.kind << "needs the web UI; cancelling" << prompt.title;
    }
    this->answer(id, answer);
}

Button accept(const QString& label, const QString& id)
{
    return { id, label, ButtonRole::Accept };
}

Button reject(const QString& label, const QString& id)
{
    return { id, label, ButtonRole::Reject };
}

Button destructive(const QString& label, const QString& id)
{
    return { id, label, ButtonRole::Destructive };
}

Button neutral(const QString& label, const QString& id)
{
    return { id, label, ButtonRole::Neutral };
}

void notify(const QString& title, const QString& text, const QString& icon)
{
    Prompt prompt;
    prompt.title = title;
    prompt.text = text;
    prompt.icon = icon;
    prompt.buttons = { accept(QObject::tr("OK")) };
    prompt.defaultButton = "ok";
    UserInteraction::instance()->ask(prompt, {});
}

bool confirm(const QString& title, const QString& text, const QString& icon, const QString& yes, const QString& no, bool defaultYes)
{
    const auto answer = message(title, text, icon,
                                { reject(no.isEmpty() ? QObject::tr("No") : no, "no"), accept(yes.isEmpty() ? QObject::tr("Yes") : yes, "yes") },
                                defaultYes ? "yes" : "no");
    return answer.is("yes");
}

Answer message(const QString& title,
               const QString& text,
               const QString& icon,
               const QList<Button>& buttons,
               const QString& defaultButton,
               const QString& checkbox)
{
    Prompt prompt;
    prompt.title = title;
    prompt.text = text;
    prompt.icon = icon;
    prompt.buttons = buttons;
    prompt.defaultButton = defaultButton;
    prompt.checkbox = checkbox;
    return UserInteraction::instance()->askBlocking(prompt);
}

std::optional<QStringList> chooseOptionalMods(const QStringList& items)
{
    Prompt prompt;
    prompt.kind = "optionalMods";
    prompt.title = QCoreApplication::translate("OptionalModDialog", "Select Optional Mods");
    prompt.text = QCoreApplication::translate("OptionalModDialog", "Unchecked mods will be disabled.");
    prompt.buttons = { reject(QObject::tr("Cancel")), accept(QObject::tr("OK")) };
    prompt.defaultButton = "ok";
    prompt.payload = { { "items", QJsonArray::fromStringList(items) } };
    const auto answer = UserInteraction::instance()->askBlocking(prompt);
    if (!answer.is("ok")) {
        return std::nullopt;
    }
    QStringList selected;
    for (const auto& v : answer.data.value("selected").toArray()) {
        if (items.contains(v.toString())) {
            selected << v.toString();
        }
    }
    return selected;
}

bool confirmUntrustedMods(const QStringList& items)
{
    // Same strings as the former UntrustedModsDialog, so existing translations apply.
    Prompt prompt;
    prompt.kind = "untrustedMods";
    prompt.title = QCoreApplication::translate("UntrustedModsDialog", "Easy There!");
    prompt.text = QCoreApplication::translate(
                      "UntrustedModsDialog",
                      "<html><head/><body><p>The modpack you are installing includes mods which are not hosted on Modrinth or "
                      "CurseForge:</p></body></html>") +
                  QCoreApplication::translate("UntrustedModsDialog",
                                              "<html><head/><body><p><b>Malicious mods are often distributed through links sent on "
                                              "platforms such as Discord.</b></p><p>We strongly recommend only importing modpacks from "
                                              "trusted sources.</p></body></html>");
    prompt.icon = "warning";
    prompt.checkbox = QCoreApplication::translate("UntrustedModsDialog", "I trust this modpack and wish to proceed regardless");
    prompt.buttons = { reject(QObject::tr("Cancel")), accept(QObject::tr("OK")) };
    prompt.defaultButton = "cancel";
    prompt.payload = { { "items", QJsonArray::fromStringList(items) }, { "confirmDelayMs", 3000 } };
    const auto answer = UserInteraction::instance()->askBlocking(prompt);
    return answer.is("ok") && answer.checked;
}

}  // namespace interaction
