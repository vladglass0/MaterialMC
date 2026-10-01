import { useMemo, useState } from "react";
import { t } from "../i18n";
import { formatDate } from "./format";
import { VirtualList } from "./VirtualList";

export interface SelectableVersion {
  version: string;
  type?: string;
  recommended?: boolean;
  releaseTime?: number | null;
  /** Small secondary label (e.g. the Minecraft version a loader needs). */
  detail?: string | null;
  /** Highlighted like "recommended" (e.g. the LWJGL version Minecraft asks for). */
  suggested?: boolean;
}

const TYPE_LABELS: Record<string, string> = {
  release: "Releases",
  snapshot: "Snapshots",
  old_beta: "Betas",
  old_alpha: "Alphas",
  experiment: "Experiments",
};

/**
 * Searchable version list (port of VersionSelectWidget): filter box, optional type filter,
 * recommended / current markers, double click to confirm.
 */
export function VersionSelect({
  versions,
  selected,
  current,
  onSelect,
  onActivate,
  height = 360,
  typeFilter = false,
  emptyText,
}: {
  versions: readonly SelectableVersion[];
  selected: string | null;
  current?: string | null;
  onSelect(version: string): void;
  onActivate?(version: string): void;
  height?: number;
  typeFilter?: boolean;
  emptyText?: string;
}) {
  const [query, setQuery] = useState("");
  const types = useMemo(() => [...new Set(versions.map((v) => v.type).filter((x): x is string => !!x))], [versions]);
  const [enabledTypes, setEnabledTypes] = useState<Set<string>>(() => new Set(["release"]));
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return versions.filter(
      (v) => (!q || v.version.toLowerCase().includes(q)) && (!typeFilter || !v.type || types.length <= 1 || enabledTypes.has(v.type)),
    );
  }, [versions, query, typeFilter, enabledTypes, types.length]);

  return (
    <div className="stack" style={{ gap: 8 }}>
      <div className="row" style={{ flexWrap: "wrap" }}>
        <input className="input grow" placeholder={t("Search")} value={query} onChange={(e) => setQuery(e.target.value)} autoFocus />
        {typeFilter &&
          types.length > 1 &&
          types.map((type) => (
            <label key={type} className="check small">
              <input
                type="checkbox"
                checked={enabledTypes.has(type)}
                onChange={(e) =>
                  setEnabledTypes((prev) => {
                    const next = new Set(prev);
                    if (e.target.checked) next.add(type);
                    else next.delete(type);
                    return next;
                  })
                }
              />
              {t(TYPE_LABELS[type] ?? type)}
            </label>
          ))}
      </div>
      {filtered.length === 0 ? (
        <div className="empty" style={{ height }}>
          {emptyText ?? t("No versions are currently available.")}
        </div>
      ) : (
        <VirtualList
          items={filtered}
          rowHeight={40}
          style={{ height, border: "1px solid var(--border)", borderRadius: 12 }}
          getKey={(v) => v.version}
          renderRow={(v) => (
            <div
              className={`list-row clickable${selected === v.version ? " selected" : ""}`}
              style={{ height: 40 }}
              onClick={() => onSelect(v.version)}
              onDoubleClick={() => onActivate?.(v.version)}
            >
              <strong className="grow ellipsis">{v.version}</strong>
              {v.detail && <span className="small muted">{v.detail}</span>}
              {current === v.version && <span className="chip info">{t("Current")}</span>}
              {(v.recommended || v.suggested) && <span className="chip ok">{t("Recommended")}</span>}
              {v.type && v.type !== "release" && <span className="chip">{v.type}</span>}
              {v.releaseTime ? <span className="small muted">{formatDate(v.releaseTime).split(",")[0]}</span> : null}
            </div>
          )}
        />
      )}
    </div>
  );
}
