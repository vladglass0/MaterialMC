import { useMemo, useState } from "react";
import { materialmc } from "../api/client";
import { useEvent, useQuery } from "../hooks/useApi";
import { t } from "../i18n";
import type { IconInfo } from "../types/system";
import { ErrorBanner, Spinner } from "./common";
import { Dialog } from "./Dialog";
import { useToasts } from "./Toasts";

type Category = "all" | IconInfo["category"];

/** Port of IconPickerDialog: categories, search, add / remove custom icons, open the icons folder. */
export function IconPicker({ selected, onSelect, onClose }: { selected?: string; onSelect(key: string): void; onClose(): void }) {
  const icons = useQuery(() => materialmc.system.icons(), []);
  useEvent("icons.changed", icons.reload);
  const { showError } = useToasts();
  const [category, setCategory] = useState<Category>("all");
  const [query, setQuery] = useState("");
  const [current, setCurrent] = useState(selected ?? "");

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    return (icons.data ?? []).filter(
      (i) => (category === "all" || i.category === category) && (!q || i.name.toLowerCase().includes(q) || i.key.toLowerCase().includes(q)),
    );
  }, [icons.data, category, query]);
  const currentIcon = icons.data?.find((i) => i.key === current);

  const categories: Array<[Category, string]> = [
    ["all", t("All")],
    ["modern", t("Modern")],
    ["legacy", t("Legacy")],
    ["modpack", t("Modpacks")],
    ["custom", t("Custom")],
  ];

  return (
    <Dialog
      title={t("Pick icon")}
      wide
      onClose={onClose}
      footer={
        <>
          <span className="grow row">
            <button className="btn" onClick={() => materialmc.icons.add().catch((e: unknown) => showError(e))}>
              {t("Add Icon")}
            </button>
            <button
              className="btn"
              disabled={!currentIcon?.removable}
              onClick={() => materialmc.icons.remove(current).catch((e: unknown) => showError(e))}
            >
              {t("Remove Icon")}
            </button>
            <button className="btn" onClick={() => materialmc.system.openFolder({ target: "icons" }).catch((e: unknown) => showError(e))}>
              {t("Open Folder")}
            </button>
          </span>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" disabled={!current} onClick={() => onSelect(current)}>
            {t("OK")}
          </button>
        </>
      }
    >
      <div className="row">
        <div className="segmented">
          {categories.map(([id, label]) => (
            <button key={id} className={category === id ? "active" : ""} onClick={() => setCategory(id)}>
              {label}
            </button>
          ))}
        </div>
        <input className="input grow" placeholder={t("Search...")} value={query} onChange={(e) => setQuery(e.target.value)} />
      </div>
      {icons.error ? (
        <ErrorBanner error={icons.error} />
      ) : !icons.data ? (
        <Spinner />
      ) : (
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(76px, 1fr))", gap: 8 }}>
          {visible.map((icon) => (
            <button
              key={icon.key}
              className={`btn ${icon.key === current ? "tonal" : "ghost"}`}
              style={{ height: 76, borderRadius: 12, flexDirection: "column", gap: 2, padding: 4 }}
              title={icon.name}
              onClick={() => setCurrent(icon.key)}
              onDoubleClick={() => onSelect(icon.key)}
            >
              <img src={icon.url} alt="" width={48} height={48} loading="lazy" style={{ imageRendering: "pixelated" }} />
            </button>
          ))}
        </div>
      )}
    </Dialog>
  );
}
