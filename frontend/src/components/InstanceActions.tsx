import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from "react";
import { useNavigate } from "react-router-dom";
import { materialmc } from "../api/client";
import { useEvent, useQuery } from "../hooks/useApi";
import { instancesStore, useAccounts } from "../hooks/stores";
import { waitForTask } from "../hooks/useTaskCompletion";
import { t, tn } from "../i18n";
import type { ApiErrorCode } from "../types/common";
import type { CopyInstanceOptions, Instance, LaunchParams, ShortcutTarget } from "../types/instances";
import { ConfirmDialog, Dialog } from "./Dialog";
import { IconPicker } from "./IconPicker";
import type { MenuItem } from "./Menu";
import { useToasts } from "./Toasts";
import { ErrorBanner, Spinner } from "./common";

type InstanceRef = Pick<Instance, "id" | "name">;
type LaunchOptions = Omit<LaunchParams, "id">;

interface InstanceActions {
  launch(instance: InstanceRef, options?: LaunchOptions): Promise<void>;
  kill(instance: InstanceRef): void;
  remove(instance: Pick<Instance, "id" | "name" | "shortcutCount">): void;
  duplicate(instance: Pick<Instance, "id" | "name" | "group" | "iconKey">): void;
  rename(instance: InstanceRef): void;
  changeGroup(instance: Pick<Instance, "id" | "name" | "group">): void;
  changeIcon(instance: Pick<Instance, "id" | "iconKey">): void;
  createShortcut(instance: Pick<Instance, "id" | "name" | "iconKey">): void;
  chooseProfiler(instance: InstanceRef): void;
  askOffline(instance: InstanceRef): void;
  renameGroup(group: string): void;
  deleteGroup(group: string): void;
  /** Launch entries of the Qt launch menu (Launch, Offline, Demo, Launch As…, Profiler). */
  launchMenu(instance: Instance): MenuItem[];
  /** The full instance context menu of the Qt main window. */
  instanceMenu(instance: Instance): MenuItem[];
}

const Ctx = createContext<InstanceActions | null>(null);

type DialogState =
  | { type: "kill"; instance: InstanceRef }
  | { type: "remove"; instance: Pick<Instance, "id" | "name" | "shortcutCount"> }
  | { type: "duplicate"; instance: Pick<Instance, "id" | "name" | "group" | "iconKey"> }
  | { type: "rename"; instance: InstanceRef }
  | { type: "group"; instance: Pick<Instance, "id" | "name" | "group"> }
  | { type: "icon"; instance: Pick<Instance, "id" | "iconKey"> }
  | { type: "shortcut"; instance: Pick<Instance, "id" | "name" | "iconKey"> }
  | { type: "profiler"; instance: InstanceRef }
  | { type: "offline"; instance: InstanceRef; reason: string }
  | { type: "renameGroup"; group: string }
  | { type: "deleteGroup"; group: string }
  | null;

/** Launch failures whose fix is "play offline instead". */
const OFFLINE_FALLBACK: ApiErrorCode[] = ["NO_ACCOUNT", "ACCOUNT_NEEDS_REAUTH", "ACCOUNT_NEEDS_PROFILE"];

const enc = encodeURIComponent;

export function InstanceActionsProvider({ children }: { children: ReactNode }) {
  const [dialog, setDialog] = useState<DialogState>(null);
  const { showError, notify } = useToasts();
  const navigate = useNavigate();
  const accounts = useAccounts();
  const close = useCallback(() => setDialog(null), []);

  const launch = useCallback<InstanceActions["launch"]>(
    async (instance, options = {}) => {
      try {
        await materialmc.instances.launch({ id: instance.id, ...options });
      } catch (e) {
        showError(e, t("Could not launch %1", instance.name));
      }
    },
    [showError],
  );

  // The launch pipeline runs asynchronously in C++; failures arrive as events.
  useEvent("instance.launchFailed", (e) => {
    const name = instancesStore.getSnapshot().data.find((i) => i.id === e.instanceId)?.name ?? e.instanceId;
    if (OFFLINE_FALLBACK.includes(e.error.code)) {
      setDialog({ type: "offline", instance: { id: e.instanceId, name }, reason: e.error.message });
      return;
    }
    showError(e.error, t("Launch of %1 failed", name));
  });
  useEvent("minecraft.crashed", (e) => showError(e.reason || t("Minecraft crashed"), t("Game crashed")));

  const actions = useMemo<InstanceActions>(() => {
    const base = {
      launch,
      kill: (instance: InstanceRef) => setDialog({ type: "kill", instance }),
      remove: (instance: Pick<Instance, "id" | "name" | "shortcutCount">) => setDialog({ type: "remove", instance }),
      duplicate: (instance: Pick<Instance, "id" | "name" | "group" | "iconKey">) => setDialog({ type: "duplicate", instance }),
      rename: (instance: InstanceRef) => setDialog({ type: "rename", instance }),
      changeGroup: (instance: Pick<Instance, "id" | "name" | "group">) => setDialog({ type: "group", instance }),
      changeIcon: (instance: Pick<Instance, "id" | "iconKey">) => setDialog({ type: "icon", instance }),
      createShortcut: (instance: Pick<Instance, "id" | "name" | "iconKey">) => setDialog({ type: "shortcut", instance }),
      chooseProfiler: (instance: InstanceRef) => setDialog({ type: "profiler", instance }),
      askOffline: (instance: InstanceRef) => setDialog({ type: "offline", instance, reason: "" }),
      renameGroup: (group: string) => setDialog({ type: "renameGroup", group }),
      deleteGroup: (group: string) => setDialog({ type: "deleteGroup", group }),
    };
    const launchMenu = (instance: Instance): MenuItem[] => {
      const stopped = instance.state === "stopped";
      const items: MenuItem[] = [
        { label: t("Launch"), disabled: !stopped || !instance.canLaunch, onSelect: () => void launch(instance) },
        { label: t("Launch Offline"), disabled: !stopped || !instance.canLaunch, onSelect: () => void launch(instance, { mode: "offline" }) },
        {
          label: t("Launch Demo"),
          disabled: !stopped || !instance.canLaunch || !instance.supportsDemo,
          onSelect: () => void launch(instance, { mode: "demo" }),
        },
      ];
      if (accounts.data.length > 1) {
        accounts.data.forEach((account, index) =>
          items.push({
            label: account.profileName || account.id,
            image: account.faceUrl || undefined,
            section: index === 0 ? t("Launch As") : undefined,
            separatorBefore: index === 0,
            disabled: !stopped || !instance.canLaunch,
            onSelect: () => void launch(instance, { accountId: account.id }),
          }),
        );
      }
      items.push({ label: t("Profilers") + "…", separatorBefore: true, onSelect: () => base.chooseProfiler(instance) });
      return items;
    };
    const instanceMenu = (instance: Instance): MenuItem[] => {
      const stopped = instance.state === "stopped";
      return [
        { label: t("Open details"), onSelect: () => navigate(`/instances/${enc(instance.id)}`) },
        ...launchMenu(instance).map((item, i) => (i === 0 ? { ...item, separatorBefore: true } : item)),
        { label: t("Kill"), disabled: stopped, danger: true, onSelect: () => base.kill(instance) },
        { label: t("Edit mods"), separatorBefore: true, onSelect: () => navigate(`/instances/${enc(instance.id)}/mods`) },
        { label: t("Console"), onSelect: () => navigate(`/console/${enc(instance.id)}`) },
        { label: t("Rename"), separatorBefore: true, onSelect: () => base.rename(instance) },
        { label: t("Change Icon"), onSelect: () => base.changeIcon(instance) },
        { label: t("Change Group..."), onSelect: () => base.changeGroup(instance) },
        {
          label: t("Folder"),
          onSelect: () => materialmc.system.openFolder({ target: "instance.game", instanceId: instance.id }).catch((e: unknown) => showError(e)),
        },
        { label: t("Export..."), onSelect: () => navigate(`/instances/${enc(instance.id)}/export`) },
        { label: t("Copy"), onSelect: () => base.duplicate(instance) },
        { label: t("Create Shortcut"), onSelect: () => base.createShortcut(instance) },
        { label: t("Delete"), danger: true, separatorBefore: true, disabled: !stopped, onSelect: () => base.remove(instance) },
      ];
    };
    return { ...base, launchMenu, instanceMenu };
  }, [launch, accounts.data, navigate, showError]);

  const undo = () =>
    materialmc.instances.undoTrash().then(
      () => notify(t("Instance restored"), "success"),
      (e: unknown) => showError(e, t("Failed to undo trashing instance")),
    );

  return (
    <Ctx.Provider value={actions}>
      {children}
      {dialog?.type === "kill" && (
        <ConfirmDialog
          title={t("Kill Minecraft?")}
          danger
          confirmLabel={t("Kill")}
          message={t(
            "This force-stops %1. Unsaved progress may be lost and the world can get corrupted; only use it when the game is frozen.",
            dialog.instance.name,
          )}
          onConfirm={() => materialmc.instances.kill(dialog.instance.id).then(() => undefined, (e) => showError(e))}
          onClose={close}
        />
      )}
      {dialog?.type === "remove" && (
        <ConfirmDialog
          title={t("Confirm Deletion")}
          danger
          confirmLabel={t("Delete")}
          message={
            <div style={{ whiteSpace: "pre-line" }}>
              {t(
                'You are about to delete "%1"%2.\nThis may be permanent and will completely delete the instance.\n\nAre you sure?',
                dialog.instance.name,
                dialog.instance.shortcutCount > 0 ? tn(" and its %n registered shortcut(s)", dialog.instance.shortcutCount) : "",
              )}
            </div>
          }
          onConfirm={async () => {
            try {
              await materialmc.instances.remove(dialog.instance.id);
              navigate("/instances");
              const overview = await materialmc.instances.overview();
              if (overview.canUndoTrash) {
                notify(t("%1 was moved to the trash", dialog.instance.name), "info", { label: t("Undo"), onClick: () => void undo() });
              }
            } catch (e) {
              showError(e, t("Could not delete the instance"));
            }
          }}
          onClose={close}
        />
      )}
      {dialog?.type === "duplicate" && (
        <CopyDialog
          instance={dialog.instance}
          onClose={close}
          onStarted={(taskId) => {
            close();
            notify(t("Copying %1…", dialog.instance.name));
            waitForTask(taskId).then(
              () => notify(t("Instance copied"), "success"),
              (e: unknown) => showError(e, t("Copy failed")),
            );
          }}
        />
      )}
      {dialog?.type === "rename" && <RenameDialog instance={dialog.instance} onClose={close} />}
      {dialog?.type === "group" && <GroupDialog instance={dialog.instance} onClose={close} />}
      {dialog?.type === "icon" && (
        <IconPicker
          selected={dialog.instance.iconKey}
          onClose={close}
          onSelect={(key) => materialmc.instances.setIcon(dialog.instance.id, key).then(close, (e: unknown) => showError(e))}
        />
      )}
      {dialog?.type === "shortcut" && <ShortcutDialog instance={dialog.instance} onClose={close} />}
      {dialog?.type === "profiler" && <ProfilerDialog instance={dialog.instance} onClose={close} />}
      {dialog?.type === "renameGroup" && <RenameGroupDialog group={dialog.group} onClose={close} />}
      {dialog?.type === "deleteGroup" && (
        <ConfirmDialog
          title={t("Delete group")}
          danger
          confirmLabel={t("Delete")}
          message={t("Are you sure you want to delete the group '%1'?", dialog.group)}
          onConfirm={() => materialmc.instances.deleteGroup(dialog.group).then(() => undefined, (e: unknown) => showError(e))}
          onClose={close}
        />
      )}
      {dialog?.type === "offline" && (
        <OfflineDialog
          instanceName={dialog.instance.name}
          reason={dialog.reason}
          onClose={close}
          onLaunch={(name, mode) => {
            close();
            void launch(dialog.instance, { mode, offlineName: name });
          }}
        />
      )}
    </Ctx.Provider>
  );
}

export function useInstanceActions(): InstanceActions {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error("useInstanceActions must be used inside <InstanceActionsProvider>");
  return ctx;
}

function GroupInput({ value, onChange, autoFocus }: { value: string; onChange(v: string): void; autoFocus?: boolean }) {
  const groups = useQuery(() => materialmc.instances.groups(), []);
  return (
    <>
      <input
        className="input"
        list="instance-groups"
        value={value}
        placeholder={t("No group")}
        autoFocus={autoFocus}
        onChange={(e) => onChange(e.target.value)}
      />
      <datalist id="instance-groups">
        {(groups.data ?? []).map((g) => (
          <option key={g} value={g} />
        ))}
      </datalist>
    </>
  );
}

const COPY_DEFAULTS: CopyInstanceOptions = {
  copySaves: true,
  keepPlaytime: true,
  copyGameOptions: true,
  copyResourcePacks: true,
  copyShaderPacks: true,
  copyServers: true,
  copyMods: true,
  copyScreenshots: true,
  useSymLinks: false,
  linkRecursively: false,
  useHardLinks: false,
  dontLinkSaves: false,
  useClone: false,
};

/** Port of CopyInstanceDialog. */
function CopyDialog({
  instance,
  onClose,
  onStarted,
}: {
  instance: Pick<Instance, "id" | "name" | "group" | "iconKey">;
  onClose(): void;
  onStarted(taskId: string): void;
}) {
  const [name, setName] = useState(instance.name);
  const [group, setGroup] = useState(instance.group ?? "");
  const [iconKey, setIconKey] = useState(instance.iconKey);
  const [picker, setPicker] = useState(false);
  const [o, setO] = useState<CopyInstanceOptions>(COPY_DEFAULTS);
  const [busy, setBusy] = useState(false);
  const info = useQuery(() => materialmc.instances.copyInfo(instance.id), [instance.id]);
  const { showError } = useToasts();
  const set = (key: keyof CopyInstanceOptions) => (e: React.ChangeEvent<HTMLInputElement>) => setO((prev) => ({ ...prev, [key]: e.target.checked }));
  const linking = o.useSymLinks || o.useHardLinks;
  const contentKeys: Array<[keyof CopyInstanceOptions, string]> = [
    ["copySaves", t("Copy saves")],
    ["copyGameOptions", t("Copy game options")],
    ["copyResourcePacks", t("Copy resource packs")],
    ["copyShaderPacks", t("Copy shader packs")],
    ["copyServers", t("Copy servers")],
    ["copyMods", t("Copy mods")],
    ["copyScreenshots", t("Copy screenshots")],
  ];
  const allSelected = contentKeys.every(([k]) => o[k]);
  const icons = useQuery(() => materialmc.system.icons(), []);
  const iconUrl = icons.data?.find((i) => i.key === iconKey)?.url;

  return (
    <Dialog
      title={t("Copy Instance")}
      wide
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button
            className="btn primary"
            disabled={busy || !name.trim()}
            onClick={async () => {
              setBusy(true);
              try {
                const { taskId } = await materialmc.instances.copy({ id: instance.id, name: name.trim(), group: group.trim() || null, iconKey, ...o });
                onStarted(taskId);
              } catch (e) {
                showError(e, t("Could not copy the instance"));
                setBusy(false);
              }
            }}
          >
            {t("OK")}
          </button>
        </>
      }
    >
      <div className="row" style={{ alignItems: "flex-start" }}>
        <button className="btn ghost" style={{ width: 72, height: 72, borderRadius: 16 }} onClick={() => setPicker(true)} title={t("Change Icon")}>
          {iconUrl && <img src={iconUrl} alt="" width={48} height={48} />}
        </button>
        <div className="stack grow">
          <label className="field">
            <span>{t("Name")}</span>
            <input className="input" value={name} onChange={(e) => setName(e.target.value)} autoFocus />
          </label>
          <label className="field">
            <span>{t("Group")}</span>
            <GroupInput value={group} onChange={setGroup} />
          </label>
        </div>
      </div>
      <fieldset className="card stack">
        <legend>{t("Instance Copy Options")}</legend>
        <label className="check">
          <input type="checkbox" checked={o.keepPlaytime} onChange={set("keepPlaytime")} /> {t("Keep play time")}
        </label>
        <div className="grid-2" style={{ gap: 4 }}>
          {contentKeys.map(([key, label]) => (
            <label key={key} className="check">
              <input type="checkbox" checked={o[key]} onChange={set(key)} /> {label}
            </label>
          ))}
        </div>
        <div>
          <button
            className="btn small"
            onClick={() => setO((prev) => ({ ...prev, ...Object.fromEntries(contentKeys.map(([k]) => [k, !allSelected])) }))}
          >
            {t("Select all")}
          </button>
        </div>
      </fieldset>
      <details>
        <summary>{t("Advanced Copy Options")}</summary>
        <div className="stack" style={{ marginTop: 8 }}>
          <span className="small muted">{t("Links are supported on most filesystems except FAT")}</span>
          <label className="check">
            <input type="checkbox" checked={o.useSymLinks} disabled={info.data && !info.data.linkSupported} onChange={set("useSymLinks")} />{" "}
            {t("Use symbolic links")}
          </label>
          <label className="check">
            <input type="checkbox" checked={o.useHardLinks} disabled={info.data && !info.data.linkSupported} onChange={set("useHardLinks")} />{" "}
            {t("Use hard links")}
          </label>
          <label className="check">
            <input type="checkbox" checked={o.linkRecursively} disabled={!linking} onChange={set("linkRecursively")} /> {t("Link files recursively")}
          </label>
          <label className="check">
            <input type="checkbox" checked={o.dontLinkSaves} disabled={!linking} onChange={set("dontLinkSaves")} /> {t("Don't link saves")}
          </label>
          <label className="check">
            <input type="checkbox" checked={o.useClone} disabled={!info.data?.cloneSupported || linking} onChange={set("useClone")} />{" "}
            {t("Clone instead of copying")}
          </label>
          {info.data && (
            <span className="small muted">
              {info.data.cloneSupported
                ? t("Reflinks are supported on %1", info.data.filesystem)
                : t("Reflinks aren't supported on %1", info.data.filesystem)}
            </span>
          )}
        </div>
      </details>
      {picker && (
        <IconPicker
          selected={iconKey}
          onClose={() => setPicker(false)}
          onSelect={(key) => {
            setIconKey(key);
            setPicker(false);
          }}
        />
      )}
    </Dialog>
  );
}

function RenameDialog({ instance, onClose }: { instance: InstanceRef; onClose(): void }) {
  const [name, setName] = useState(instance.name);
  const [busy, setBusy] = useState(false);
  const { showError } = useToasts();
  const navigate = useNavigate();
  const submit = () => {
    setBusy(true);
    materialmc.instances.rename(instance.id, name.trim()).then(
      (result) => {
        onClose();
        // The folder (and with it the id) may have been renamed as well.
        if (result.id !== instance.id && window.location.hash.includes(enc(instance.id))) {
          navigate(window.location.hash.slice(1).replace(enc(instance.id), enc(result.id)), { replace: true });
        }
      },
      (e: unknown) => {
        setBusy(false);
        showError(e, t("Rename failed"));
      },
    );
  };
  return (
    <Dialog
      title={t("Rename instance")}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" disabled={busy || !name.trim()} onClick={submit}>
            {t("Rename")}
          </button>
        </>
      }
    >
      <input className="input" value={name} onChange={(e) => setName(e.target.value)} autoFocus onKeyDown={(e) => e.key === "Enter" && name.trim() && submit()} />
    </Dialog>
  );
}

function GroupDialog({ instance, onClose }: { instance: Pick<Instance, "id" | "name" | "group">; onClose(): void }) {
  const [group, setGroup] = useState(instance.group ?? "");
  const { showError } = useToasts();
  const submit = () => materialmc.instances.setGroup(instance.id, group.trim() || null).then(onClose, (e: unknown) => showError(e));
  return (
    <Dialog
      title={t("Group name")}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" onClick={submit}>
            {t("OK")}
          </button>
        </>
      }
    >
      <span>{t("Enter a new group name.")}</span>
      <GroupInput value={group} onChange={setGroup} autoFocus />
    </Dialog>
  );
}

function RenameGroupDialog({ group, onClose }: { group: string; onClose(): void }) {
  const [name, setName] = useState(group);
  const { showError } = useToasts();
  return (
    <Dialog
      title={t("Rename group")}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button
            className="btn primary"
            disabled={!name.trim() || name.trim() === group}
            onClick={() => materialmc.instances.renameGroup(group, name.trim()).then(onClose, (e: unknown) => showError(e, t("Cannot rename group")))}
          >
            {t("Rename")}
          </button>
        </>
      }
    >
      <span>{t("Enter a new group name.")}</span>
      <input className="input" value={name} onChange={(e) => setName(e.target.value)} autoFocus />
    </Dialog>
  );
}

/** Port of CreateShortcutDialog. */
function ShortcutDialog({ instance, onClose }: { instance: Pick<Instance, "id" | "name" | "iconKey">; onClose(): void }) {
  const info = useQuery(() => materialmc.instances.shortcutTargets(instance.id), [instance.id]);
  const accounts = useAccounts();
  const icons = useQuery(() => materialmc.system.icons(), []);
  const { showError } = useToasts();
  const [target, setTarget] = useState<ShortcutTarget | "">("");
  const [name, setName] = useState("");
  const [iconKey, setIconKey] = useState(instance.iconKey);
  const [picker, setPicker] = useState(false);
  const [overrideAccount, setOverrideAccount] = useState(false);
  const [accountId, setAccountId] = useState("");
  const [joinTarget, setJoinTarget] = useState(false);
  const [joinKind, setJoinKind] = useState<"world" | "server">("server");
  const [world, setWorld] = useState("");
  const [server, setServer] = useState("");
  const [busy, setBusy] = useState(false);

  const effectiveTarget = target || info.data?.targets[0] || "other";
  const canWorld = !!info.data?.quickPlaySingleplayer && (info.data?.worlds.length ?? 0) > 0;
  const kind = canWorld ? joinKind : "server";
  const placeholder = joinTarget
    ? kind === "world" && world
      ? t("%1 - %2", instance.name, world)
      : kind === "server" && server
        ? t("%1 - Server %2", instance.name, server)
        : instance.name
    : instance.name;
  const valid = !joinTarget || (kind === "world" ? !!world : !!server.trim());
  const targetLabels: Record<ShortcutTarget, string> = { desktop: t("Desktop"), applications: t("Applications"), other: t("Other...") };
  const iconUrl = icons.data?.find((i) => i.key === iconKey)?.url;

  return (
    <Dialog
      title={t("Create Instance Shortcut")}
      wide
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button
            className="btn primary"
            disabled={busy || !valid || !info.data}
            onClick={async () => {
              setBusy(true);
              try {
                const result = await materialmc.instances.createShortcut({
                  id: instance.id,
                  target: effectiveTarget,
                  name: name.trim() || undefined,
                  iconKey,
                  accountId: overrideAccount && accountId ? accountId : undefined,
                  world: joinTarget && kind === "world" ? world : undefined,
                  server: joinTarget && kind === "server" ? server.trim() : undefined,
                });
                if (result.created) onClose();
                else setBusy(false);
              } catch (e) {
                showError(e);
                setBusy(false);
              }
            }}
          >
            {t("OK")}
          </button>
        </>
      }
    >
      {info.error ? (
        <ErrorBanner error={info.error} />
      ) : !info.data ? (
        <Spinner />
      ) : (
        <>
          <div className="row" style={{ alignItems: "flex-start" }}>
            <button className="btn ghost" style={{ width: 72, height: 72, borderRadius: 16 }} onClick={() => setPicker(true)}>
              {iconUrl && <img src={iconUrl} alt="" width={48} height={48} />}
            </button>
            <div className="stack grow">
              <label className="field">
                <span>{t("Name:")}</span>
                <input className="input" value={name} placeholder={placeholder} onChange={(e) => setName(e.target.value)} />
              </label>
              <label className="field">
                <span>{t("Save To:")}</span>
                <select className="select" value={effectiveTarget} onChange={(e) => setTarget(e.target.value as ShortcutTarget)}>
                  {info.data.targets.map((tg) => (
                    <option key={tg} value={tg}>
                      {targetLabels[tg]}
                    </option>
                  ))}
                </select>
              </label>
            </div>
          </div>
          <label className="check" title={t("Use a different account than the default specified.")}>
            <input type="checkbox" checked={overrideAccount} disabled={accounts.data.length === 0} onChange={(e) => setOverrideAccount(e.target.checked)} />
            {t("Override the default account")}
          </label>
          {overrideAccount && (
            <select className="select" value={accountId} onChange={(e) => setAccountId(e.target.value)}>
              <option value="">—</option>
              {accounts.data.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.profileName || a.id}
                </option>
              ))}
            </select>
          )}
          <label className="check" title={t("Specify a world or server to automatically join on launch.")}>
            <input type="checkbox" checked={joinTarget} onChange={(e) => setJoinTarget(e.target.checked)} />
            {t("Select a target to join on launch")}
          </label>
          {joinTarget && (
            <div className="stack" style={{ paddingLeft: 28 }}>
              {canWorld && (
                <label className="check">
                  <input type="radio" checked={kind === "world"} onChange={() => setJoinKind("world")} /> {t("World:")}
                  <select className="select grow" disabled={kind !== "world"} value={world} onChange={(e) => setWorld(e.target.value)}>
                    <option value="">—</option>
                    {info.data.worlds.map((w) => (
                      <option key={w.name} value={w.name}>
                        {w.name}
                      </option>
                    ))}
                  </select>
                </label>
              )}
              <label className="check">
                {canWorld && <input type="radio" checked={kind === "server"} onChange={() => setJoinKind("server")} />} {t("Server Address:")}
                <input className="input grow" disabled={kind !== "server"} value={server} placeholder={t("Server Address")} onChange={(e) => setServer(e.target.value)} />
              </label>
            </div>
          )}
          <p className="small muted" style={{ margin: 0 }}>
            {t("Note: If a shortcut is moved after creation, it won't be deleted when deleting the instance.")}{" "}
            {t("You'll need to delete them manually if that is the case.")}
          </p>
        </>
      )}
      {picker && (
        <IconPicker
          selected={iconKey}
          onClose={() => setPicker(false)}
          onSelect={(key) => {
            setIconKey(key);
            setPicker(false);
          }}
        />
      )}
    </Dialog>
  );
}

function ProfilerDialog({ instance, onClose }: { instance: InstanceRef; onClose(): void }) {
  const profilers = useQuery(() => materialmc.instances.profilers(instance.id), [instance.id]);
  const { showError } = useToasts();
  const select = (key: string) => materialmc.instances.setProfiler(instance.id, key).then(onClose, (e: unknown) => showError(e));
  return (
    <Dialog
      title={t("Profilers")}
      onClose={onClose}
      footer={
        <button className="btn" onClick={onClose}>
          {t("Close")}
        </button>
      }
    >
      {profilers.error ? (
        <ErrorBanner error={profilers.error} />
      ) : !profilers.data ? (
        <Spinner />
      ) : (
        <div className="card flush">
          <label className="list-row check">
            <input type="radio" checked={!profilers.data.selected} onChange={() => select("")} /> {t("No Profiler")}
          </label>
          {profilers.data.profilers.map((p) => (
            <label key={p.key} className="list-row check" title={p.error}>
              <input type="radio" disabled={!p.available} checked={profilers.data?.selected === p.key} onChange={() => select(p.key)} />
              <span className="stack" style={{ gap: 0 }}>
                <span>{p.name}</span>
                {!p.available && p.error && <span className="small muted">{p.error}</span>}
              </span>
            </label>
          ))}
        </div>
      )}
    </Dialog>
  );
}

function OfflineDialog({
  instanceName,
  reason,
  onClose,
  onLaunch,
}: {
  instanceName: string;
  reason: string;
  onClose(): void;
  onLaunch(name: string, mode: "offline" | "demo"): void;
}) {
  const accounts = useAccounts();
  const navigate = useNavigate();
  const fallbackName = accounts.data.find((a) => a.isDefault)?.profileName ?? "Player";
  const [name, setName] = useState(fallbackName);
  const ownsGame = accounts.data.some((a) => a.ownsMinecraft);
  const valid = /^[A-Za-z0-9_]{3,16}$/.test(name);

  return (
    <Dialog
      title={t("Play %1 offline?", instanceName)}
      onClose={onClose}
      footer={
        <>
          <button
            className="btn"
            onClick={() => {
              onClose();
              navigate("/accounts");
            }}
          >
            {t("Manage Accounts...")}
          </button>
          <button className="btn" onClick={onClose}>
            {t("Cancel")}
          </button>
          <button className="btn primary" disabled={!valid} onClick={() => onLaunch(name, ownsGame ? "offline" : "demo")}>
            {ownsGame ? t("Launch Offline") : t("Play Demo")}
          </button>
        </>
      }
    >
      {reason && <div className="banner warn">{reason}</div>}
      <p className="muted" style={{ margin: 0 }}>
        {ownsGame
          ? t("Launch without contacting the Minecraft authentication servers.")
          : t("No account that owns Minecraft is signed in. You can play the demo, or add a Microsoft account.")}
      </p>
      <label className="field">
        <span>{t("Player name")}</span>
        <input className="input" value={name} maxLength={16} onChange={(e) => setName(e.target.value)} autoFocus />
        {!valid && (
          <span className="hint">
            {t("Username must be between 3 and 16 characters long and can only contain letters, numbers and underscores.")}
          </span>
        )}
      </label>
    </Dialog>
  );
}
