import { useMemo, useState, type DragEvent } from "react";
import { Link } from "react-router-dom";
import { materialmc } from "../api/client";
import { InstanceCard } from "../components/InstanceCard";
import { useInstanceActions } from "../components/InstanceActions";
import { Empty, ErrorBanner, Spinner } from "../components/common";
import { formatDuration } from "../components/format";
import { MenuButton, useContextMenu, type MenuItem } from "../components/Menu";
import { useToasts } from "../components/Toasts";
import { useInstances, useOverview, useSettings } from "../hooks/stores";
import { t } from "../i18n";
import type { Instance } from "../types/instances";
import { mdiChevronDown, mdiChevronRight, mdiPlus, mdiRestore } from "@mdi/js";
import { Icon } from "../components/Icon";

type Sort = "Name" | "LastLaunch" | "Playtime";

const SORTERS: Record<Sort, (a: Instance, b: Instance) => number> = {
  Name: (a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base", numeric: true }),
  LastLaunch: (a, b) => (b.lastLaunch ?? 0) - (a.lastLaunch ?? 0),
  Playtime: (a, b) => b.totalPlayTime - a.totalPlayTime,
};

const DRAG_TYPE = "application/x-materialmc-instance";

export function InstancesPage() {
  const instances = useInstances();
  const overview = useOverview();
  const settings = useSettings();
  const actions = useInstanceActions();
  const { showError, notify } = useToasts();
  const [query, setQuery] = useState("");
  const [dropTarget, setDropTarget] = useState<string | null>(null);
  const pageMenu = useContextMenu();

  const sortSetting = settings.data?.InstSortMode;
  const sort: Sort = sortSetting === "LastLaunch" || sortSetting === "Playtime" ? sortSetting : "Name";
  const setSort = (value: Sort) => materialmc.settings.set({ values: { InstSortMode: value } }).catch((e: unknown) => showError(e));
  const collapsed = new Set(overview.data.collapsedGroups);

  const groups = useMemo(() => {
    const q = query.trim().toLowerCase();
    const filtered = instances.data.filter(
      (i) =>
        !q ||
        i.name.toLowerCase().includes(q) ||
        (i.minecraftVersion ?? "").includes(q) ||
        (i.loader?.kind ?? "vanilla").includes(q) ||
        (i.group ?? "").toLowerCase().includes(q),
    );
    const byGroup = new Map<string, Instance[]>();
    for (const i of filtered) {
      const key = i.group ?? "";
      const list = byGroup.get(key) ?? [];
      list.push(i);
      byGroup.set(key, list);
    }
    return [...byGroup.entries()]
      .sort(([a], [b]) => (a === "" ? 1 : b === "" ? -1 : a.localeCompare(b)))
      .map(([group, list]) => ({ group, list: list.sort(SORTERS[sort]) }));
  }, [instances.data, query, sort]);

  const undo = () =>
    materialmc.instances.undoTrash().then(
      () => notify(t("Instance restored"), "success"),
      (e: unknown) => showError(e, t("Failed to undo trashing instance")),
    );

  const dropProps = (group: string) => ({
    onDragOver: (e: DragEvent) => {
      if (e.dataTransfer.types.includes(DRAG_TYPE)) {
        e.preventDefault();
        e.dataTransfer.dropEffect = "move";
        setDropTarget(group);
      }
    },
    onDragLeave: () => setDropTarget((g) => (g === group ? null : g)),
    onDrop: (e: DragEvent) => {
      const id = e.dataTransfer.getData(DRAG_TYPE);
      setDropTarget(null);
      if (!id) return;
      e.preventDefault();
      const instance = instances.data.find((i) => i.id === id);
      if (instance && (instance.group ?? "") !== group) {
        materialmc.instances.setGroup(id, group || null).catch((err: unknown) => showError(err));
      }
    },
  });

  const groupMenu = (group: string): MenuItem[] => [
    { label: t("Create instance"), onSelect: () => (window.location.hash = `#/instances/new?group=${encodeURIComponent(group)}`) },
    { label: t("Rename group"), disabled: !group, onSelect: () => actions.renameGroup(group) },
    { label: t("Delete group"), danger: true, disabled: !group, onSelect: () => actions.deleteGroup(group) },
  ];

  return (
    <div className="page" onContextMenu={pageMenu.open([{ label: t("Create instance"), onSelect: () => (window.location.hash = "#/instances/new") }])}>
      <div className="page-header">
        <h1>{t("Instances")}</h1>
        <div className="actions">
          <input className="input" placeholder={t("Search instances…")} value={query} onChange={(e) => setQuery(e.target.value)} />
          <select className="select" value={sort} onChange={(e) => void setSort(e.target.value as Sort)} aria-label={t("Sort by")}>
            <option value="Name">{t("By name")}</option>
            <option value="LastLaunch">{t("By last launched")}</option>
            <option value="Playtime">{t("By playtime")}</option>
          </select>
          {overview.data.canUndoTrash && (
            <button className="btn" onClick={() => void undo()} title={t("Undo Trash Instance")}>
              <Icon path={mdiRestore} /> {t("Undo Trash Instance")}
            </button>
          )}
          <button className="btn" onClick={() => materialmc.system.openFolder({ target: "instances" }).catch((e: unknown) => showError(e))}>
            {t("Folder")}
          </button>
          <Link className="btn fab" to="/instances/new">
            <Icon path={mdiPlus} /> {t("Add Instance")}
          </Link>
        </div>
      </div>

      {overview.data.showGlobalGameTime && overview.data.totalPlayTime > 0 && (
        <div className="small muted">{t("Total playtime: %1", formatDuration(overview.data.totalPlayTime))}</div>
      )}

      {instances.error ? (
        <ErrorBanner error={instances.error} />
      ) : !instances.loaded ? (
        <Spinner label={t("Loading instances…")} />
      ) : instances.data.length === 0 ? (
        <Empty>
          {t("No instances yet.")} <Link to="/instances/new">{t("Create your first instance")}</Link>.
        </Empty>
      ) : groups.length === 0 ? (
        <Empty>{t("No instance matches “%1”.", query)}</Empty>
      ) : (
        groups.map(({ group, list }) => {
          const isCollapsed = !!group && collapsed.has(group) && !query;
          return (
            <section key={group} className={`stack instance-group${dropTarget === group ? " drop-target" : ""}`} {...dropProps(group)}>
              <div className="row group-header" onContextMenu={pageMenu.open(groupMenu(group))}>
                <button
                  className="btn ghost small"
                  disabled={!group}
                  onClick={() => materialmc.instances.setGroupCollapsed(group, !isCollapsed).catch((e: unknown) => showError(e))}
                >
                  {group && <Icon path={isCollapsed ? mdiChevronRight : mdiChevronDown} size={20} />}
                  <span className="group-title">{group || t("Ungrouped")}</span>
                  <span className="small muted">({list.length})</span>
                </button>
                <span className="grow" />
                {group && <MenuButton items={groupMenu(group)} />}
              </div>
              {!isCollapsed && (
                <div className="instance-grid">
                  {list.map((i) => (
                    <InstanceCard key={i.id} instance={i} draggable />
                  ))}
                </div>
              )}
            </section>
          );
        })
      )}
      {pageMenu.element}
    </div>
  );
}
