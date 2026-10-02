import { useEffect, useMemo, useState, type ReactNode } from "react";
import { NavLink, useNavigate, useParams } from "react-router-dom";
import { materialmc } from "../api/client";
import { ConsoleView, instanceLogSource } from "../components/ConsoleView";
import { ErrorBanner, Spinner } from "../components/common";
import { formatDate, formatDuration, formatRelative, LOADER_NAMES } from "../components/format";
import { GameLogsView } from "../components/GameLogsView";
import { ManagedPackView } from "../components/ManagedPackView";
import { useInstanceActions } from "../components/InstanceActions";
import { InstanceStateChip, PlayButton } from "../components/InstanceCard";
import { InstanceSettingsView } from "../components/InstanceSettingsView";
import { MenuButton } from "../components/Menu";
import { ResourceList } from "../components/ResourceList";
import { RichText } from "../components/RichText";
import { ScreenshotsView } from "../components/ScreenshotsView";
import { useToasts } from "../components/Toasts";
import { VersionView } from "../components/VersionView";
import { WorldsView } from "../components/WorldsView";
import { useInstances } from "../hooks/stores";
import { useEvent, useQuery } from "../hooks/useApi";
import { t, tn } from "../i18n";
import type { Instance } from "../types/instances";

interface TabDef {
  id: string;
  label: () => string;
  /** Only shown when it applies to the instance (like Qt's BasePage::shouldDisplay). */
  visible?: (instance: Instance) => boolean;
}

const TABS: TabDef[] = [
  { id: "overview", label: () => t("Overview") },
  { id: "managed-pack", label: () => t("Managed Pack"), visible: (instance) => instance.managedPack !== null },
  { id: "version", label: () => t("Version") },
  { id: "mods", label: () => t("Mods") },
  { id: "resourcepacks", label: () => t("Resource packs") },
  { id: "shaderpacks", label: () => t("Shader packs") },
  { id: "texturepacks", label: () => t("Texture packs") },
  { id: "worlds", label: () => t("Worlds") },
  { id: "screenshots", label: () => t("Screenshots") },
  { id: "console", label: () => t("Minecraft Log") },
  { id: "logs", label: () => t("Other logs") },
  { id: "settings", label: () => t("Settings") },
];

export function InstanceDetailsPage() {
  const params = useParams();
  const id = params.id ?? "";
  const instances = useInstances();
  const instance = instances.data.find((i) => i.id === id);
  const actions = useInstanceActions();
  const navigate = useNavigate();

  if (!instance) {
    if (!instances.loaded) return <Spinner label={t("Loading instance…")} />;
    if (instances.error) return <ErrorBanner error={instances.error} />;
    return (
      <div className="page">
        <ErrorBanner error={t("Instance “%1” does not exist (it may have been deleted).", id)} />
      </div>
    );
  }

  const tabs = TABS.filter((tab) => !tab.visible || tab.visible(instance));
  const tab = tabs.some((x) => x.id === params.tab) ? (params.tab as string) : "overview";
  const base = `/instances/${encodeURIComponent(instance.id)}`;
  const running = instance.state !== "stopped";

  return (
    <div className="page">
      <div className="page-header">
        <img className="instance-icon" src={instance.iconUrl} alt="" onClick={() => actions.changeIcon(instance)} style={{ cursor: "pointer" }} />
        <div className="grow" style={{ minWidth: 0 }}>
          <h1 className="ellipsis">{instance.name}</h1>
          <div className="row small muted">
            {t("Minecraft %1", instance.minecraftVersion ?? "?")} ·{" "}
            {instance.loader ? `${LOADER_NAMES[instance.loader.kind]} ${instance.loader.version}` : t("Vanilla")}
            <InstanceStateChip instance={instance} />
          </div>
        </div>
        <div className="actions">
          <PlayButton instance={instance} />
          <MenuButton items={actions.instanceMenu(instance).slice(1)} />
        </div>
      </div>

      <nav className="tabs">
        {tabs.map((x) => (
          <NavLink key={x.id} to={x.id === "overview" ? base : `${base}/${x.id}`} end className={tab === x.id ? "active" : ""}>
            {x.label()}
          </NavLink>
        ))}
      </nav>

      {tab === "overview" && <Overview instance={instance} onNavigate={(x) => navigate(`${base}/${x}`)} />}
      {tab === "managed-pack" && <ManagedPackView instance={instance} />}
      {tab === "version" && <VersionView instanceId={instance.id} running={running} />}
      {(tab === "mods" || tab === "resourcepacks" || tab === "shaderpacks" || tab === "texturepacks") && (
        <ResourceList instanceId={instance.id} kind={tab} />
      )}
      {tab === "worlds" && <WorldsView instanceId={instance.id} instanceName={instance.name} />}
      {tab === "screenshots" && <ScreenshotsView instanceId={instance.id} />}
      {tab === "console" && <InstanceConsole instanceId={instance.id} />}
      {tab === "logs" && <GameLogsView instanceId={instance.id} />}
      {tab === "settings" && <InstanceSettingsView instanceId={instance.id} />}
    </div>
  );
}

function InstanceConsole({ instanceId }: { instanceId: string }) {
  const source = useMemo(() => instanceLogSource(instanceId), [instanceId]);
  return <ConsoleView source={source} />;
}

function Overview({ instance, onNavigate }: { instance: Instance; onNavigate(tab: string): void }) {
  const details = useQuery(() => materialmc.instances.get(instance.id), [instance.id]);
  const { showError, notify } = useToasts();
  const actions = useInstanceActions();
  const [notes, setNotes] = useState("");
  useEffect(() => setNotes(details.data?.notes ?? ""), [details.data?.notes]);
  useEvent("instances.changed", details.reload);
  useEvent("components.changed", (e) => e.instanceId === instance.id && details.reload());

  return (
    <div className="grid-2" style={{ alignItems: "start" }}>
      <section className="card stack">
        <h2>{t("Summary")}</h2>
        <dl className="stack" style={{ margin: 0, gap: 6 }}>
          <Info label={t("Last played")} value={`${formatRelative(instance.lastLaunch)} (${formatDate(instance.lastLaunch)})`} />
          <Info label={t("Total play time")} value={formatDuration(instance.totalPlayTime)} />
          <Info label={t("Last session")} value={formatDuration(instance.lastPlayTime)} />
          <Info label={t("Group")} value={instance.group ?? t("Ungrouped")} />
          {instance.managedPack && (
            <Info label={t("Modpack")} value={`${instance.managedPack.name} ${instance.managedPack.version} (${instance.managedPack.type})`} />
          )}
          {details.data && <Info label={t("Game folder")} value={<code className="small">{details.data.gameRoot}</code>} />}
          {details.data && details.data.shortcuts.length > 0 && (
            <Info label={t("Shortcuts")} value={details.data.shortcuts.map((s) => s.name).join(", ")} />
          )}
        </dl>
        {details.data?.statusDescription && <RichText className="small muted" text={details.data.statusDescription} />}
        <div className="row" style={{ flexWrap: "wrap" }}>
          <button className="btn small tonal" onClick={() => onNavigate("mods")}>
            {tn("%n mod(s)", instance.modCount)}
          </button>
          <button className="btn small tonal" onClick={() => onNavigate("worlds")}>
            {t("Worlds")}
          </button>
          <button className="btn small tonal" onClick={() => actions.changeIcon(instance)}>
            {t("Change Icon")}
          </button>
          <button className="btn small tonal" onClick={() => actions.changeGroup(instance)}>
            {t("Change Group...")}
          </button>
        </div>
      </section>

      <section className="card stack">
        <div className="row">
          <h2 className="grow">{t("Components")}</h2>
          <button className="btn small" onClick={() => onNavigate("version")}>
            {t("Edit")}
          </button>
        </div>
        {details.error ? (
          <ErrorBanner error={details.error} onRetry={details.reload} />
        ) : !details.data ? (
          <Spinner />
        ) : (
          <table className="table">
            <tbody>
              {details.data.components.map((c) => (
                <tr key={c.uid} className={c.enabled ? "" : "disabled"}>
                  <td>{c.name}</td>
                  <td className="mono small">{c.version}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section className="card stack" style={{ gridColumn: "1 / -1" }}>
        <h2>{t("Notes")}</h2>
        <textarea className="textarea" rows={5} value={notes} onChange={(e) => setNotes(e.target.value)} placeholder={t("Notes about this instance…")} />
        <div className="row">
          <div className="grow" />
          <button
            className="btn"
            disabled={notes === (details.data?.notes ?? "")}
            onClick={() =>
              materialmc.instances.setNotes(instance.id, notes).then(() => notify(t("Notes saved"), "success"), (e: unknown) => showError(e))
            }
          >
            {t("Save notes")}
          </button>
        </div>
      </section>
    </div>
  );
}

function Info({ label, value }: { label: string; value: ReactNode }) {
  return (
    <div className="row" style={{ alignItems: "baseline" }}>
      <dt className="muted small" style={{ width: 130 }}>
        {label}
      </dt>
      <dd style={{ margin: 0 }} className="grow">
        {value}
      </dd>
    </div>
  );
}
