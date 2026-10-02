import { useEffect, useMemo, useState } from "react";
import { materialmc } from "../api/client";
import { useEvent, useQuery } from "../hooks/useApi";
import { waitForTask } from "../hooks/useTaskCompletion";
import { t } from "../i18n";
import type { ComponentFileKind, ComponentInfo } from "../types/components";
import { ErrorBanner, Spinner } from "./common";
import { Dialog } from "./Dialog";
import { Icon } from "./Icon";
import { MenuButton, useContextMenu, type MenuItem } from "./Menu";
import { useToasts } from "./Toasts";
import { VersionSelect } from "./VersionSelect";
import { mdiAlert, mdiAlertCircle, mdiArrowDown, mdiArrowUp, mdiRefresh } from "@mdi/js";

/** Mod loaders offered by the Qt InstallLoaderDialog, in the same order. */
export const LOADERS = [
  { uid: "net.neoforged", name: "NeoForge", minMinecraft: null },
  { uid: "net.minecraftforge", name: "Forge", minMinecraft: null },
  { uid: "net.fabricmc.fabric-loader", name: "Fabric", minMinecraft: "1.14" },
  { uid: "org.quiltmc.quilt-loader", name: "Quilt", minMinecraft: "1.14" },
  { uid: "com.mumfrey.liteloader", name: "LiteLoader", minMinecraft: null },
] as const;

/** Rough "a < b" for dotted release versions; non-release ids compare as "not older". */
function olderThan(version: string, minimum: string): boolean {
  const parse = (v: string) => (/^\d+(\.\d+)*$/.test(v) ? v.split(".").map(Number) : null);
  const a = parse(version);
  const b = parse(minimum);
  if (!a || !b) return false;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const x = a[i] ?? 0;
    const y = b[i] ?? 0;
    if (x !== y) return x < y;
  }
  return false;
}

/** Port of the Qt VersionPage: the instance's component list and everything that edits it. */
export function VersionView({ instanceId, running }: { instanceId: string; running: boolean }) {
  const list = useQuery(() => materialmc.components.list(instanceId), [instanceId]);
  useEvent("components.changed", (e) => e.instanceId === instanceId && list.reload());
  const { showError, notify } = useToasts();
  const [selectedUid, setSelectedUid] = useState<string | null>(null);
  const [filter, setFilter] = useState("");
  const [dialog, setDialog] = useState<
    { type: "version"; component: ComponentInfo } | { type: "loader" } | { type: "empty" } | null
  >(null);
  const menu = useContextMenu();

  const components = list.data?.components ?? [];
  const selected = components.find((c) => c.uid === selectedUid) ?? null;
  const visible = useMemo(() => {
    const q = filter.trim().toLowerCase();
    return components.filter((c) => !q || c.name.toLowerCase().includes(q) || c.uid.toLowerCase().includes(q) || c.version.toLowerCase().includes(q));
  }, [components, filter]);
  useEffect(() => {
    if (!selectedUid && components[0]) setSelectedUid(components[0].uid);
  }, [components, selectedUid]);

  const run = (promise: Promise<unknown>, context?: string) => promise.then(() => list.reload(), (e: unknown) => showError(e, context));
  const ref = (c: ComponentInfo) => ({ id: instanceId, uid: c.uid });
  const locked = running;

  const addFiles = (kind: ComponentFileKind) =>
    materialmc.components.addFiles({ id: instanceId, kind }).then(
      (r) => r.added > 0 && list.reload(),
      (e: unknown) => showError(e),
    );

  const componentMenu = (c: ComponentInfo): MenuItem[] => [
    { label: t("Change Version"), disabled: locked || !c.versionChangeable, onSelect: () => setDialog({ type: "version", component: c }) },
    {
      label: c.enabled ? t("Disable") : t("Enable"),
      disabled: locked || !c.canBeDisabled,
      onSelect: () => void run(materialmc.components.setEnabled({ ...ref(c), enabled: !c.enabled })),
    },
    { label: t("Move Up"), disabled: locked || !c.moveable, onSelect: () => void run(materialmc.components.move({ ...ref(c), direction: "up" })) },
    { label: t("Move Down"), disabled: locked || !c.moveable, onSelect: () => void run(materialmc.components.move({ ...ref(c), direction: "down" })) },
    { label: t("Customize"), separatorBefore: true, disabled: locked || !c.customizable, onSelect: () => void run(materialmc.components.customize(ref(c))) },
    { label: t("Edit"), disabled: locked || !c.custom, onSelect: () => void run(materialmc.components.edit(ref(c))) },
    { label: t("Revert"), disabled: locked || !c.revertible, onSelect: () => void run(materialmc.components.revert(ref(c))) },
    { label: t("Remove"), danger: true, separatorBefore: true, disabled: locked || !c.removable, onSelect: () => void run(materialmc.components.remove(ref(c))) },
  ];

  const addMenu: MenuItem[] = [
    { label: t("Install Loader"), disabled: locked, onSelect: () => setDialog({ type: "loader" }) },
    { label: t("Add to Minecraft.jar"), disabled: locked, onSelect: () => void addFiles("jarMods") },
    { label: t("Replace Minecraft.jar"), disabled: locked, onSelect: () => void addFiles("customJar") },
    { label: t("Add Agents"), disabled: locked, onSelect: () => void addFiles("agents") },
    { label: t("Add Empty"), disabled: locked, onSelect: () => setDialog({ type: "empty" }) },
    { label: t("Import Components"), disabled: locked, onSelect: () => void addFiles("components") },
  ];
  const moreMenu: MenuItem[] = [
    {
      label: t("Download All"),
      disabled: locked,
      onSelect: () =>
        materialmc.components.downloadAll(instanceId).then(
          ({ taskId }) => {
            if (!taskId) return;
            void waitForTask(taskId).then(
              () => notify(t("All files downloaded"), "success"),
              (e: unknown) => showError(e, t("Error updating instance")),
            );
          },
          (e: unknown) => showError(e),
        ),
    },
    { label: t("Reload"), onSelect: () => void run(materialmc.components.reload(instanceId)) },
    {
      label: t("Open Minecraft folder"),
      separatorBefore: true,
      onSelect: () => materialmc.system.openFolder({ target: "instance.game", instanceId }).catch((e: unknown) => showError(e)),
    },
    {
      label: t("Open libraries folder"),
      onSelect: () => materialmc.system.openFolder({ target: "instance.libraries", instanceId }).catch((e: unknown) => showError(e)),
    },
  ];

  if (list.error) return <ErrorBanner error={list.error} onRetry={list.reload} />;
  if (!list.data) return <Spinner />;

  return (
    <div className="stack">
      {running && <div className="banner warn">{t("The instance is running. Stop it to change its components.")}</div>}
      <div className="row" style={{ flexWrap: "wrap" }}>
        <input className="input" placeholder={t("Filter")} value={filter} onChange={(e) => setFilter(e.target.value)} />
        <span className="grow" />
        {list.data.resolving && <Spinner label={t("Updating components…")} />}
        <button className="btn" disabled={locked || !selected?.versionChangeable} onClick={() => selected && setDialog({ type: "version", component: selected })}>
          {t("Change Version")}
        </button>
        <button className="btn tonal" disabled={locked} onClick={() => setDialog({ type: "loader" })}>
          {t("Install Loader")}
        </button>
        <MenuButton className="btn" label={t("Add")} items={addMenu}>
          {t("Add")}…
        </MenuButton>
        <MenuButton items={moreMenu} />
      </div>

      <div className="card flush">
        <table className="table selectable">
          <thead>
            <tr>
              <th>{t("Name")}</th>
              <th>{t("Version")}</th>
              <th style={{ width: 150 }} />
            </tr>
          </thead>
          <tbody>
            {visible.map((c) => (
              <tr
                key={c.uid}
                className={`${c.enabled ? "" : "disabled"}${c.uid === selectedUid ? " selected" : ""}`}
                onClick={() => setSelectedUid(c.uid)}
                onDoubleClick={() => !locked && c.versionChangeable && setDialog({ type: "version", component: c })}
                onContextMenu={(e) => {
                  setSelectedUid(c.uid);
                  menu.open(componentMenu(c))(e);
                }}
              >
                <td>
                  <span className="row" style={{ gap: 8 }}>
                    {c.severity === "error" && <Icon path={mdiAlertCircle} size={18} style={{ color: "var(--danger)" }} />}
                    {c.severity === "warning" && <Icon path={mdiAlert} size={18} style={{ color: "var(--warning)" }} />}
                    <span>
                      {c.name}
                      {c.custom && <span className="chip small" style={{ marginLeft: 8 }}>{t("Custom")}</span>}
                    </span>
                  </span>
                  <div className="small muted mono">{c.uid}</div>
                </td>
                <td className="mono small">{c.version}</td>
                <td>
                  <span className="row" style={{ gap: 0, justifyContent: "flex-end", flexWrap: "nowrap" }}>
                    <button
                      className="btn ghost small icon-only"
                      title={t("Move Up")}
                      disabled={locked || !c.moveable}
                      onClick={(e) => {
                        e.stopPropagation();
                        void run(materialmc.components.move({ ...ref(c), direction: "up" }));
                      }}
                    >
                      <Icon path={mdiArrowUp} />
                    </button>
                    <button
                      className="btn ghost small icon-only"
                      title={t("Move Down")}
                      disabled={locked || !c.moveable}
                      onClick={(e) => {
                        e.stopPropagation();
                        void run(materialmc.components.move({ ...ref(c), direction: "down" }));
                      }}
                    >
                      <Icon path={mdiArrowDown} />
                    </button>
                    <MenuButton items={componentMenu(c)} />
                  </span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {selected && selected.problems.length > 0 && (
        <div className={`banner ${selected.severity === "error" ? "error" : "warn"}`}>
          <strong>
            {selected.severity === "error" ? t("%1 has issues!", selected.name) : t("%1 possibly has issues.", selected.name)}
          </strong>
          {selected.problems.map((p, i) => (
            <div key={i}>
              {p.severity === "error" ? t("Error: ") : p.severity === "warning" ? t("Warning: ") : ""}
              {p.description}
            </div>
          ))}
        </div>
      )}

      {dialog?.type === "version" && (
        <ChangeVersionDialog instanceId={instanceId} component={dialog.component} onClose={() => setDialog(null)} onDone={list.reload} />
      )}
      {dialog?.type === "loader" && (
        <InstallLoaderDialog
          instanceId={instanceId}
          minecraftVersion={components.find((c) => c.uid === "net.minecraft")?.version ?? ""}
          onClose={() => setDialog(null)}
          onDone={list.reload}
        />
      )}
      {dialog?.type === "empty" && (
        <NewComponentDialog instanceId={instanceId} existing={components.map((c) => c.uid)} onClose={() => setDialog(null)} onDone={list.reload} />
      )}
      {menu.element}
    </div>
  );
}

function ChangeVersionDialog({
  instanceId,
  component,
  onClose,
  onDone,
}: {
  instanceId: string;
  component: ComponentInfo;
  onClose(): void;
  onDone(): void;
}) {
  const [reload, setReload] = useState(0);
  const versions = useQuery(
    () => materialmc.components.versions({ id: instanceId, uid: component.uid, forceReload: reload > 0 }),
    [instanceId, component.uid, reload],
  );
  const [selected, setSelected] = useState<string | null>(component.version || null);
  const { showError } = useToasts();
  const isMappings = component.uid === "net.fabricmc.intermediary" || component.uid === "org.quiltmc.hashed";
  const items = (versions.data?.versions ?? [])
    .filter((v) => v.matchesMinecraft)
    .map((v) => ({ ...v, detail: v.minecraft && v.minecraft !== versions.data?.minecraftVersion ? v.minecraft : null }));
  const apply = (version: string) =>
    materialmc.components.setVersion({ id: instanceId, uid: component.uid, version }).then(
      () => {
        onDone();
        onClose();
      },
      (e: unknown) => showError(e),
    );

  return (
    <Dialog
      title={t("Change %1 version", component.name)}
      wide
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={() => setReload((r) => r + 1)}>
            <Icon path={mdiRefresh} /> {t("Refresh")}
          </button>
          <span className="grow" />
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" disabled={!selected || selected === component.version} onClick={() => selected && void apply(selected)}>
            {t("OK")}
          </button>
        </>
      }
    >
      {versions.error ? (
        <ErrorBanner
          error={isMappings ? t("Couldn't load or download the intermediary mappings version lists!") : versions.error}
          onRetry={versions.reload}
        />
      ) : !versions.data ? (
        <Spinner />
      ) : (
        <VersionSelect
          versions={items}
          selected={selected}
          current={versions.data.current}
          onSelect={setSelected}
          onActivate={(v) => void apply(v)}
          typeFilter={component.uid === "net.minecraft"}
          emptyText={isMappings ? t("No intermediary mappings versions are currently available.") : undefined}
        />
      )}
    </Dialog>
  );
}

/** Port of InstallLoaderDialog: one tab per loader, conflicts are resolved by the backend (prompts). */
export function InstallLoaderDialog({
  instanceId,
  minecraftVersion,
  initialUid,
  onClose,
  onDone,
}: {
  instanceId: string;
  minecraftVersion: string;
  initialUid?: string;
  onClose(): void;
  onDone(): void;
}) {
  const [uid, setUid] = useState<string>(initialUid ?? LOADERS[0].uid);
  const loader = LOADERS.find((l) => l.uid === uid) ?? LOADERS[0];
  const [reload, setReload] = useState(0);
  const versions = useQuery(() => materialmc.components.versions({ id: instanceId, uid, forceReload: reload > 0 }), [instanceId, uid, reload]);
  const [selected, setSelected] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const { showError } = useToasts();
  useEffect(() => setSelected(versions.data?.current ?? null), [versions.data]);

  const tooOld = loader.minMinecraft !== null && olderThan(minecraftVersion, loader.minMinecraft);
  const items = tooOld ? [] : (versions.data?.versions ?? []).filter((v) => v.matchesMinecraft);
  const install = (version: string) => {
    setBusy(true);
    materialmc.components.installLoader({ id: instanceId, uid, version }).then(
      () => {
        onDone();
        onClose();
      },
      (e: unknown) => {
        setBusy(false);
        showError(e);
      },
    );
  };

  return (
    <Dialog
      title={t("Install Loader")}
      wide
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={() => setReload((r) => r + 1)}>
            <Icon path={mdiRefresh} /> {t("Refresh")}
          </button>
          <span className="grow" />
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" disabled={busy || !selected} onClick={() => selected && install(selected)}>
            {t("OK")}
          </button>
        </>
      }
    >
      <div className="segmented">
        {LOADERS.map((l) => (
          <button
            key={l.uid}
            className={l.uid === uid ? "active" : ""}
            onClick={() => {
              setUid(l.uid);
              setSelected(null);
            }}
          >
            {l.name}
          </button>
        ))}
      </div>
      {versions.error ? (
        <ErrorBanner error={versions.error} onRetry={versions.reload} />
      ) : !versions.data ? (
        <Spinner />
      ) : (
        <VersionSelect
          versions={items}
          selected={selected}
          current={versions.data.current}
          onSelect={setSelected}
          onActivate={install}
          emptyText={t("No versions are currently available for Minecraft %1", minecraftVersion)}
        />
      )}
    </Dialog>
  );
}

function NewComponentDialog({
  instanceId,
  existing,
  onClose,
  onDone,
}: {
  instanceId: string;
  existing: string[];
  onClose(): void;
  onDone(): void;
}) {
  const [name, setName] = useState("");
  const [uid, setUid] = useState("");
  const { showError } = useToasts();
  const validUid = /^[a-zA-Z0-9-]+(\.[a-zA-Z0-9-]+)+$/.test(uid) && !existing.includes(uid);
  return (
    <Dialog
      title={t("Add Empty")}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button
            className="btn primary"
            disabled={!name.trim() || !validUid}
            onClick={() =>
              materialmc.components.addEmpty({ id: instanceId, uid, name: name.trim() }).then(
                () => {
                  onDone();
                  onClose();
                },
                (e: unknown) => showError(e),
              )
            }
          >
            {t("OK")}
          </button>
        </>
      }
    >
      <label className="field">
        <span>{t("Name")}</span>
        <input className="input" value={name} placeholder={t("Name")} autoFocus onChange={(e) => setName(e.target.value)} />
      </label>
      <label className="field">
        <span>{t("uid")}</span>
        <input className="input mono" value={uid} placeholder="org.example.component" onChange={(e) => setUid(e.target.value)} />
        {uid && !validUid && <span className="hint">{t("The uid must look like org.example.component and be unique.")}</span>}
      </label>
    </Dialog>
  );
}
