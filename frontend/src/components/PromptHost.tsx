import { useCallback, useEffect, useState } from "react";
import { materialmc } from "../api/client";
import { useEvent } from "../hooks/useApi";
import { t, tn } from "../i18n";
import type { BlockedModInfo, Prompt, PromptButton } from "../types/prompts";
import { Dialog } from "./Dialog";
import { Icon } from "./Icon";
import { RichText } from "./RichText";
import { mdiAlertCircleOutline, mdiAlertOutline, mdiCheckCircle, mdiCloseCircle, mdiHelpCircleOutline, mdiInformationOutline } from "@mdi/js";

/**
 * Shows the questions the backend asks (`prompt.show`), one at a time in arrival order, and sends the answer back.
 * Prompts opened before the page loaded (or across a reload) are fetched with `prompts.pending`.
 */
export function PromptHost() {
  const [queue, setQueue] = useState<Prompt[]>([]);

  useEffect(() => {
    if (!materialmc.connected) return;
    materialmc.prompts.pending().then(
      (list) => setQueue((q) => [...q, ...list.filter((p) => !q.some((x) => x.id === p.id))]),
      () => undefined,
    );
  }, []);

  useEvent("prompt.show", (prompt) => setQueue((q) => (q.some((x) => x.id === prompt.id) ? q : [...q, prompt])));
  useEvent("prompt.update", ({ id, payload }) =>
    setQueue((q) => q.map((p) => (p.id === id ? { ...p, payload: { ...p.payload, ...payload } } : p))),
  );
  useEvent("prompt.close", ({ id }) => setQueue((q) => q.filter((p) => p.id !== id)));

  const current = queue[0];
  const answer = useCallback((prompt: Prompt, button: string, checked = false, data?: Record<string, unknown>) => {
    setQueue((q) => q.filter((p) => p.id !== prompt.id));
    void materialmc.prompts.answer({ promptId: prompt.id, button, checked, data }).catch(() => undefined);
  }, []);

  if (!current) return null;
  return <PromptDialog key={current.id} prompt={current} onAnswer={answer} />;
}

const ICONS: Record<string, { path: string; color: string }> = {
  info: { path: mdiInformationOutline, color: "var(--md-sys-color-primary)" },
  warning: { path: mdiAlertOutline, color: "var(--warning)" },
  error: { path: mdiAlertCircleOutline, color: "var(--danger)" },
  question: { path: mdiHelpCircleOutline, color: "var(--md-sys-color-primary)" },
};

function PromptDialog({
  prompt,
  onAnswer,
}: {
  prompt: Prompt;
  onAnswer(prompt: Prompt, button: string, checked?: boolean, data?: Record<string, unknown>): void;
}) {
  const [checked, setChecked] = useState(false);
  const [data, setData] = useState<Record<string, unknown>>(() => initialData(prompt));
  const [checkboxEnabled, setCheckboxEnabled] = useState(() => !(Number(prompt.payload.confirmDelayMs) > 0));

  useEffect(() => {
    const delay = Number(prompt.payload.confirmDelayMs);
    if (delay > 0) {
      const timer = window.setTimeout(() => setCheckboxEnabled(true), delay);
      return () => window.clearTimeout(timer);
    }
  }, [prompt.payload.confirmDelayMs]);

  const rejectButton = prompt.buttons.find((b) => b.role === "reject");
  const dismiss = () => onAnswer(prompt, rejectButton?.id ?? "");
  const icon = ICONS[prompt.icon];
  // untrustedMods: continuing needs the confirmation checkbox
  const needsCheck = prompt.kind === "untrustedMods";

  const press = (b: PromptButton) => onAnswer(prompt, b.id, checked, data);

  return (
    <Dialog
      wide={prompt.kind !== "message" && prompt.kind !== "text"}
      title={
        <span className="row" style={{ gap: 12 }}>
          {icon && <Icon path={icon.path} size={26} style={{ color: icon.color, flex: "none" }} />}
          <span>{prompt.title}</span>
        </span>
      }
      onClose={dismiss}
      footer={
        <>
          {prompt.kind === "blockedMods" && (
            <span className="grow row">
              <button className="btn" onClick={() => void materialmc.prompts.action({ promptId: prompt.id, action: "addFolder" })}>
                {t("Add Download Folder")}
              </button>
              <button
                className="btn"
                onClick={() => {
                  for (const mod of (prompt.payload.mods as BlockedModInfo[] | undefined) ?? []) {
                    if (!mod.matched) void materialmc.system.openUrl(mod.url);
                  }
                }}
              >
                {t("Open Missing")}
              </button>
            </span>
          )}
          {prompt.buttons.map((b) => (
            <button
              key={b.id}
              className={`btn${b.role === "accept" ? " primary" : b.role === "destructive" ? " danger" : ""}`}
              autoFocus={b.id === prompt.defaultButton}
              disabled={b.role === "accept" && needsCheck && !checked}
              onClick={() => press(b)}
            >
              {b.label}
            </button>
          ))}
        </>
      }
    >
      {prompt.text && <RichText text={prompt.text} />}
      <PromptBody prompt={prompt} data={data} setData={setData} />
      {typeof prompt.payload.details === "string" && prompt.payload.details && (
        <details>
          <summary>{t("Show Details...")}</summary>
          <pre className="code-box small" style={{ whiteSpace: "pre-wrap" }}>
            {prompt.payload.details}
          </pre>
        </details>
      )}
      {prompt.checkbox && (
        <label className="check">
          <input type="checkbox" checked={checked} disabled={!checkboxEnabled} onChange={(e) => setChecked(e.target.checked)} />
          {prompt.checkbox}
        </label>
      )}
    </Dialog>
  );
}

function initialData(prompt: Prompt): Record<string, unknown> {
  switch (prompt.kind) {
    case "text":
      return { value: String(prompt.payload.value ?? "") };
    case "optionalMods":
      return { selected: [] as string[] };
    case "choice":
      return { selected: prompt.payload.selected ?? null };
    case "review":
      return {
        selected: ((prompt.payload.items as Array<{ id: string; checked?: boolean }> | undefined) ?? [])
          .filter((i) => i.checked !== false)
          .map((i) => i.id),
      };
    default:
      return {};
  }
}

function PromptBody({
  prompt,
  data,
  setData,
}: {
  prompt: Prompt;
  data: Record<string, unknown>;
  setData(update: Record<string, unknown>): void;
}) {
  const items = (prompt.payload.items as unknown[] | undefined) ?? [];
  switch (prompt.kind) {
    case "text":
      return (
        <input
          className="input"
          autoFocus
          value={String(data.value ?? "")}
          placeholder={String(prompt.payload.placeholder ?? "")}
          maxLength={Number(prompt.payload.maxLength) || undefined}
          onChange={(e) => setData({ value: e.target.value })}
        />
      );
    case "optionalMods":
    case "review": {
      const selected = new Set((data.selected as string[]) ?? []);
      const rows = items.map((item) =>
        typeof item === "string" ? { id: item, name: item, details: "" } : (item as { id: string; name: string; details?: string }),
      );
      const toggle = (id: string, on: boolean) => {
        const next = new Set(selected);
        if (on) next.add(id);
        else next.delete(id);
        setData({ selected: [...next] });
      };
      return (
        <div className="stack" style={{ gap: 8 }}>
          <div className="row">
            <button className="btn small" onClick={() => setData({ selected: rows.map((r) => r.id) })}>
              {t("Select All")}
            </button>
            <button className="btn small" onClick={() => setData({ selected: [] })}>
              {t("Deselect All")}
            </button>
            <span className="grow" />
            <span className="small muted">{tn("%n selected", selected.size)}</span>
          </div>
          <div className="card flush" style={{ maxHeight: 360, overflow: "auto" }}>
            {rows.map((r) => (
              <label key={r.id} className="list-row check" style={{ alignItems: "flex-start" }}>
                <input type="checkbox" checked={selected.has(r.id)} onChange={(e) => toggle(r.id, e.target.checked)} />
                <span className="stack" style={{ gap: 2 }}>
                  <span className="mono small">{r.name}</span>
                  {r.details && <RichText className="small muted" text={r.details} />}
                </span>
              </label>
            ))}
          </div>
        </div>
      );
    }
    case "choice": {
      const options = items as Array<{ id: string; label: string; description?: string }>;
      return (
        <div className="card flush" style={{ maxHeight: 360, overflow: "auto" }}>
          {options.map((o) => (
            <label key={o.id} className="list-row check" style={{ alignItems: "flex-start" }}>
              <input type="radio" name={`prompt-${prompt.id}`} checked={data.selected === o.id} onChange={() => setData({ selected: o.id })} />
              <span className="stack" style={{ gap: 2 }}>
                <span>{o.label}</span>
                {o.description && <span className="small muted">{o.description}</span>}
              </span>
            </label>
          ))}
        </div>
      );
    }
    case "untrustedMods":
    case "list":
      return (
        <div className="card flush" style={{ maxHeight: 300, overflow: "auto" }}>
          {(items as string[]).map((item) => (
            <div key={item} className="list-row mono small">
              {item}
            </div>
          ))}
        </div>
      );
    case "networkFailed": {
      const failed = (prompt.payload.failed as Array<{ url: string; error: string }> | undefined) ?? [];
      return (
        <div className="stack" style={{ gap: 8 }}>
          <table className="table">
            <thead>
              <tr>
                <th>{t("URL")}</th>
                <th>{t("Error")}</th>
              </tr>
            </thead>
            <tbody>
              {failed.map((f) => (
                <tr key={f.url}>
                  <td className="mono small" style={{ wordBreak: "break-all" }}>
                    {f.url}
                  </td>
                  <td className="small">{f.error}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <div>
            <button className="btn small" onClick={() => void materialmc.system.copyText(failed.map((f) => f.url).join("\n"))}>
              {t("Copy URL")}
            </button>
          </div>
        </div>
      );
    }
    case "blockedMods": {
      const mods = (prompt.payload.mods as BlockedModInfo[] | undefined) ?? [];
      const watched = (prompt.payload.watched as string[] | undefined) ?? [];
      return (
        <div className="stack" style={{ gap: 12 }}>
          <RichText
            className="small"
            text={t(
              "<html><head/><body><p>Your configured global mods folder and default downloads folder are automatically checked for the downloaded mods and they will be copied to the instance if found.</p><p>Optionally, you may drag and drop the downloaded mods onto this dialog or add a folder to watch if you did not download the mods to a default location.</p><p><span style=\" font-weight:600;\">Click 'Open Missing' to open all the download links in the browser. </span></p></body></html>",
            )}
          />
          <div className="card flush" style={{ maxHeight: 320, overflow: "auto" }}>
            {mods.map((m) => (
              <div key={m.name + m.hash} className="list-row" style={{ alignItems: "flex-start" }}>
                <Icon
                  path={m.matched ? mdiCheckCircle : mdiCloseCircle}
                  style={{ color: m.matched ? "var(--md-sys-color-success, green)" : "var(--danger)", flex: "none" }}
                />
                <div className="stack grow" style={{ gap: 2, minWidth: 0 }}>
                  <a
                    href={m.url}
                    onClick={(e) => {
                      e.preventDefault();
                      void materialmc.system.openUrl(m.url);
                    }}
                  >
                    {m.name}
                  </a>
                  <span className="small muted ellipsis">
                    {m.matched ? t("Found at %1", m.localPath) : t("Not Found")} · {t("Hash: %1", m.hash)}
                  </span>
                </div>
              </div>
            ))}
          </div>
          <div className="small">
            <strong>{t("Watched Folders")}</strong>
            {watched.map((w) => (
              <div key={w} className="mono muted ellipsis">
                {w}
              </div>
            ))}
          </div>
        </div>
      );
    }
    default:
      return null;
  }
}
