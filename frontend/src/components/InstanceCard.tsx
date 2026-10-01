import { memo } from "react";
import { useNavigate } from "react-router-dom";
import { useSettings } from "../hooks/stores";
import { t, tn } from "../i18n";
import type { Instance } from "../types/instances";
import { formatDuration, formatRelative, LOADER_NAMES } from "./format";
import { useInstanceActions } from "./InstanceActions";
import { MenuButton, useContextMenu } from "./Menu";
import { mdiChevronDown, mdiPlay, mdiStop } from "@mdi/js";
import { Icon } from "./Icon";

export function InstanceStateChip({ instance }: { instance: Instance }) {
  if (instance.state === "running") return <span className="chip ok">{t("Running")}</span>;
  if (instance.state === "launching") return <span className="chip info">{t("Launching…")}</span>;
  if (instance.hasVersionBroken) return <span className="chip err">{t("Broken version")}</span>;
  if (instance.hasCrashed) return <span className="chip warn">{t("Crashed")}</span>;
  return null;
}

/** Play / Stop / Cancel button, with the Qt launch menu (offline, demo, launch as, profilers) next to it. */
export function PlayButton({ instance, small }: { instance: Instance; small?: boolean }) {
  const actions = useInstanceActions();
  const cls = `btn${small ? " small" : ""}`;
  if (instance.state === "running") {
    return (
      <button className={`${cls} danger`} onClick={() => actions.kill(instance)}>
        <Icon path={mdiStop} /> {t("Stop")}
      </button>
    );
  }
  if (instance.state === "launching") {
    return (
      <button className={cls} onClick={() => actions.kill(instance)} title={t("Abort the launch")}>
        <span className="spinner" style={{ width: 16, height: 16, borderWidth: 2 }} /> {t("Cancel")}
      </button>
    );
  }
  return (
    <div className="split-button">
      <button className={`${cls} primary`} disabled={!instance.canLaunch} onClick={() => void actions.launch(instance)}>
        <Icon path={mdiPlay} /> {t("Play")}
      </button>
      <MenuButton className={`${cls} primary icon-only`} label={t("Launch options")} items={actions.launchMenu(instance)}>
        <Icon path={mdiChevronDown} size={20} />
      </MenuButton>
    </div>
  );
}

export const InstanceCard = memo(function InstanceCard({ instance, draggable }: { instance: Instance; draggable?: boolean }) {
  const navigate = useNavigate();
  const actions = useInstanceActions();
  const settings = useSettings();
  const menu = useContextMenu();
  const open = () => navigate(`/instances/${encodeURIComponent(instance.id)}`);
  // Qt: double-click launches, or edits when "EditInstanceOnDoubleClick" is set.
  const activate = () => {
    if (settings.data?.EditInstanceOnDoubleClick || instance.state !== "stopped" || !instance.canLaunch) open();
    else void actions.launch(instance);
  };

  return (
    <article
      className="instance-card"
      onContextMenu={menu.open(actions.instanceMenu(instance))}
      draggable={draggable}
      onDragStart={(e) => {
        e.dataTransfer.setData("application/x-materialmc-instance", instance.id);
        e.dataTransfer.effectAllowed = "move";
      }}
    >
      <div
        className="head"
        onClick={open}
        onDoubleClick={(e) => {
          e.preventDefault();
          activate();
        }}
        role="link"
        tabIndex={0}
        onKeyDown={(e) => e.key === "Enter" && open()}
      >
        <img className="icon" src={instance.iconUrl} alt="" loading="lazy" />
        <div className="grow">
          <div className="title ellipsis" title={instance.name}>
            {instance.name}
          </div>
          <div className="muted small">{t("Minecraft %1", instance.minecraftVersion ?? "?")}</div>
        </div>
      </div>
      <div className="meta">
        <span className="chip">{instance.loader ? LOADER_NAMES[instance.loader.kind] : t("Vanilla")}</span>
        {instance.loader && <span className="chip">{tn("%n mod(s)", instance.modCount)}</span>}
        {instance.managedPack && <span className="chip info">{instance.managedPack.name}</span>}
        <InstanceStateChip instance={instance} />
      </div>
      <div className="small muted">
        {t("Played %1", formatRelative(instance.lastLaunch))} · {formatDuration(instance.totalPlayTime)}
      </div>
      <div className="foot">
        <PlayButton instance={instance} />
        <div className="grow" />
        <MenuButton items={actions.instanceMenu(instance)} />
      </div>
      {menu.element}
    </article>
  );
});
