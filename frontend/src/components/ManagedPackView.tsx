import { useEffect, useState } from "react";
import { materialmc } from "../api/client";
import { ErrorBanner, Spinner } from "./common";
import { useQuery } from "../hooks/useApi";
import { useToasts } from "./Toasts";
import { t } from "../i18n";
import type { Instance } from "../types/instances";
import { waitForTask } from "../hooks/useTaskCompletion";

export function ManagedPackView({ instance }: { instance: Instance }) {
  const versions = useQuery(() => materialmc.modpacks.managedVersions(instance.id), [instance.id]);
  const [selected, setSelected] = useState("");
  const { showError, notify } = useToasts();
  useEffect(() => {
    if (!selected && versions.data?.length) setSelected(versions.data[0]!.id);
  }, [selected, versions.data]);
  if (versions.error) return <ErrorBanner error={versions.error} onRetry={versions.reload} />;
  if (!versions.data) return <Spinner />;
  const version = versions.data.find((item) => item.id === selected);
  return (
    <section className="card stack">
      <div className="row"><h2 className="grow">{t("Managed Pack")}</h2><span className="chip info">{instance.managedPack?.type}</span></div>
      <select className="select" value={selected} onChange={(event) => setSelected(event.target.value)}>
        {versions.data.map((item) => <option key={item.id} value={item.id}>{item.name || item.versionNumber}</option>)}
      </select>
      {version?.changelog && <pre className="small muted" style={{ whiteSpace: "pre-wrap" }}>{version.changelog}</pre>}
      <button className="btn" disabled={!version} onClick={() => {
        if (!version) return;
        materialmc.modpacks.updateManagedPack({ id: instance.id, versionId: version.id, url: version.downloadUrl ?? "", version: version.name })
          .then((task) => waitForTask(task.taskId)).then(() => notify(t("Pack updated"), "success"), showError);
      }}>{t("Update Pack")}</button>
    </section>
  );
}
