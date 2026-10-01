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

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace interaction {

/** How a button behaves; the UI uses it for styling and for Escape / closing the dialog. */
enum class ButtonRole { Accept, Reject, Destructive, Neutral };

struct Button {
    QString id;
    QString label;
    ButtonRole role = ButtonRole::Neutral;
};

/**
 * A question the backend asks the user. Rendered by the active UI (the React UI, or a native fallback).
 *
 * `kind` selects the dialog type:
 *  - "message"        text + buttons (+ optional checkbox)
 *  - "text"           text + single line input (payload: { value, placeholder, maxLength })
 *  - "optionalMods"   checklist (payload: { items: [string] }) -> answer data { selected: [string] }
 *  - "untrustedMods"  list + confirmation checkbox (payload: { items: [string] })
 *  - "blockedMods"    live list of blocked downloads (payload: { mods: [...], watched: [...] }); see BlockedModsWatcher
 *  - "networkFailed"  failed requests (payload: { attempts, requests, failed: [{ url, error }] })
 *  - "list"           read-only list of items (payload: { items: [string] })
 *  - "choice"         radio list (payload: { items: [{ id, label, description? }], selected? }) -> data { selected }
 *  - "review"         checklist with columns (payload: { items: [{ id, name, details, checked, ... }] }) -> data { selected }
 *
 * `text` may contain the small HTML subset Qt dialogs used (b, i, br, p, a, ul/li); the UI sanitises it.
 */
struct Prompt {
    QString kind = QStringLiteral("message");
    QString title;
    QString text;
    QString icon;  // "info", "warning", "error", "question" or empty
    QList<Button> buttons;
    QString defaultButton;
    QString checkbox;  // optional checkbox label; its state is returned in Answer::checked
    QJsonObject payload;
};

struct Answer {
    QString button;  // id of the pressed button; empty when the prompt was dismissed or cancelled
    bool checked = false;
    QJsonObject data;

    bool is(const QString& id) const { return button == id; }
};

/** Something that can show prompts: the web UI, or the native fallback. */
class PromptPresenter {
   public:
    virtual ~PromptPresenter() = default;
    virtual void show(int id, const Prompt& prompt) = 0;
    virtual void update(int id, const QJsonObject& payload) = 0;
    virtual void close(int id) = 0;
};

/**
 * Central place where backend code asks the user something, without depending on a UI toolkit.
 *
 * Asynchronous code uses ask(); legacy synchronous code (tasks that used QDialog::exec()) uses askBlocking(), which spins a
 * nested event loop exactly like exec() did. Without a presenter, prompts fall back to native message boxes
 * (or to their default answer when that is not possible).
 */
class UserInteraction : public QObject {
    Q_OBJECT
   public:
    using Callback = std::function<void(const Answer&)>;
    /** Called for actions that do not close the prompt (e.g. "Add download folder"). */
    using ActionHandler = std::function<void(const QString& action, const QJsonObject& data)>;

    static UserInteraction* instance();

    void setPresenter(PromptPresenter* presenter);
    bool hasPresenter() const { return m_presenter != nullptr; }

    int ask(const Prompt& prompt, Callback callback, ActionHandler onAction = {});
    Answer askBlocking(const Prompt& prompt, ActionHandler onAction = {});

    /** Pushes new payload to an open prompt (live lists). */
    void update(int id, const QJsonObject& payload);
    /** Closes an open prompt programmatically with the given answer. */
    void finish(int id, const Answer& answer);

    /** Called by the presenter when the user answered. Returns false if the prompt is unknown. */
    bool answer(int id, const Answer& answer);
    /** Called by the presenter for a non-closing action. */
    bool action(int id, const QString& action, const QJsonObject& data);

    /** Prompts that are currently open, for a UI that (re)connects. */
    QList<std::pair<int, Prompt>> pending() const;

   private:
    explicit UserInteraction(QObject* parent = nullptr);
    void showNative(int id);

    struct Entry {
        Prompt prompt;
        Callback callback;
        ActionHandler onAction;
    };
    PromptPresenter* m_presenter = nullptr;
    QHash<int, Entry> m_open;
    int m_nextId = 1;
};

// Convenience helpers ---------------------------------------------------------------------------------------------

Button accept(const QString& label, const QString& id = QStringLiteral("ok"));
Button reject(const QString& label, const QString& id = QStringLiteral("cancel"));
Button destructive(const QString& label, const QString& id);
Button neutral(const QString& label, const QString& id);

/** Shows an informational/warning/error message with a single OK button (does not block). */
void notify(const QString& title, const QString& text, const QString& icon = QStringLiteral("info"));

/** Blocking yes/no question; returns true for yes. */
bool confirm(const QString& title,
             const QString& text,
             const QString& icon = QStringLiteral("question"),
             const QString& yes = {},
             const QString& no = {},
             bool defaultYes = false);

/** Blocking message with arbitrary buttons; returns the answer. */
Answer message(const QString& title,
               const QString& text,
               const QString& icon,
               const QList<Button>& buttons,
               const QString& defaultButton = {},
               const QString& checkbox = {});

/** Blocking checklist of optional files; returns the selected ones, or std::nullopt if cancelled. */
std::optional<QStringList> chooseOptionalMods(const QStringList& items);

/** Blocking warning about files that come from an untrusted source; returns true if the user accepts them. */
bool confirmUntrustedMods(const QStringList& items);

}  // namespace interaction
