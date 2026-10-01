import { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { materialmc } from "../api/client";
import { useQuery } from "../hooks/useApi";
import { waitForTask } from "../hooks/useTaskCompletion";
import { t, useI18nVersion } from "../i18n";
import type { RemoteVersion } from "../types/mods";
import type { ModpackProject, ModpackProvider } from "../types/modpacks";
import { ErrorBanner, Spinner } from "./common";
import { formatDate } from "./format";
import { useToasts } from "./Toasts";
import { VirtualList } from "./VirtualList";

export function ModpackCreate({ source }: { source: ModpackProvider | "import" }) {
  useI18nVersion();
  const navigate = useNavigate();
  const { notify, showError } = useToasts();
  const [name, setName] = useState("");
  const [nameTouched, setNameTouched] = useState(false);
  const [group, setGroup] = useState("");
  const [busy, setBusy] = useState(false);
  const [url, setUrl] = useState("");
  const [query, setQuery] = useState("");
  const [search, setSearch] = useState("");
  const [offset, setOffset] = useState(0);
  const [sort, setSort] = useState("");
  const [project, setProject] = useState<ModpackProject>();
  const [version, setVersion] = useState<RemoteVersion>();
  const groups = useQuery(() => materialmc.instances.groups(), []);
  const results = useQuery(
    () => source === "import" ? Promise.resolve(null) : materialmc.modpacks.search({ provider: source, query: search, offset, sort: sort || undefined }),
    [source, search, offset, sort],
  );
  const versions = useQuery(
    () => project ? materialmc.modpacks.versions({ provider: project.provider, projectId: project.id }) : Promise.resolve(null),
    [project?.provider, project?.id],
  );

  useEffect(() => {
    setVersion(undefined);
  }, [project]);

  const start = async (fromFile = false) => {
    if (!name.trim() || busy || (source !== "import" && (!project || !version || versions.loading))) return;
    setBusy(true);
    try {
      const common = { name: name.trim(), group: group.trim() || null };
      const { taskId } = source === "import"
        ? await (fromFile ? materialmc.modpacks.importFile(common) : materialmc.modpacks.importUrl({ ...common, url: url.trim() }))
        : await materialmc.modpacks.install({ ...common, provider: source, projectId: project!.id, versionId: version!.id });
      notify(t("Creating %1…", name.trim()));
      navigate("/instances");
      void waitForTask(taskId).then(
        () => notify(t("%1 is ready", name.trim()), "success"),
        (error: unknown) => showError(error, t("Instance creation failed")),
      );
    } catch (error) {
      if (!(typeof error === "object" && error !== null && "code" in error && error.code === "CANCELLED")) {
        showError(error, t("Could not create the instance"));
      }
      setBusy(false);
    }
  };

  const validUrl = (() => {
    try { const parsed = new URL(url.trim()); return ["http:", "https:"].includes(parsed.protocol) && !!parsed.hostname; }
    catch { return false; }
  })();
  const canInstall = !!name.trim() && !!project && !!version && !versions.loading && !busy;

  return (
    <div className="page">
      <div className="page-header">
        <h1>{t("New instance")}</h1>
        <div className="actions">
          <button className="btn" disabled={busy} onClick={() => navigate(-1)}>{t("Cancel")}</button>
          {source !== "import" && <button className="btn primary" disabled={!canInstall} onClick={() => void start()}>{t("Install")}</button>}
        </div>
      </div>
      <div className="card grid-2">
        <label className="field"><span>{t("Name")}</span><input className="input" value={name} maxLength={256} onChange={(e) => { setName(e.target.value); setNameTouched(true); }} /></label>
        <label className="field"><span>{t("Group")}</span><input className="input" list="modpack-groups" value={group} onChange={(e) => setGroup(e.target.value)} placeholder={t("Ungrouped")} />
          <datalist id="modpack-groups">{(groups.data ?? []).map((g) => <option key={g} value={g} />)}</datalist>
        </label>
      </div>
      {source === "import" ? (
        <section className="card stack">
          <h2>{t("Import")}</h2>
          <p className="muted">{t("Import a modpack from a ZIP or .mrpack archive, or a download URL.")}</p>
          <button className="btn" disabled={!name.trim() || busy} onClick={() => void start(true)}>{t("Choose file…")}</button>
          <label className="field"><span>{t("URL")}</span><input className="input" type="url" value={url} onChange={(e) => setUrl(e.target.value)} placeholder="https://" /></label>
          <button className="btn primary" disabled={!name.trim() || !validUrl || busy} onClick={() => void start()}>{t("Import from URL")}</button>
        </section>
      ) : (
        <div className="grid-2" style={{ alignItems: "start" }}>
          <section className="card stack">
            <form className="row" onSubmit={(e) => { e.preventDefault(); setOffset(0); setSearch(query); setProject(undefined); results.reload(); }}>
              <input className="input grow" aria-label={t("Search modpacks")} placeholder={t("Search modpacks…")} value={query} onChange={(e) => setQuery(e.target.value)} />
              <button className="btn" type="submit" disabled={results.loading}>{t("Search")}</button>
            </form>
            <label className="field"><span>{t("Sort by")}</span><select className="input" value={sort} onChange={(e) => { setSort(e.target.value); setOffset(0); setProject(undefined); }}>
              <option value="">{t("Default")}</option>
              {(results.data?.sortingMethods ?? []).map((s) => <option key={s.id} value={s.id}>{s.name}</option>)}
            </select></label>
            {results.error ? <ErrorBanner error={results.error} onRetry={results.reload} /> : results.loading ? <Spinner label={t("Loading…")} /> : (
              <VirtualList items={results.data?.projects ?? []} rowHeight={80} style={{ height: 400 }} getKey={(p) => p.id} renderRow={(p) => (
                <button className={`list-row clickable${project?.id === p.id ? " selected" : ""}`} style={{ height: 80, width: "100%", textAlign: "left" }} onClick={() => { setProject(p); if (!nameTouched) setName(p.name); }}>
                  {p.iconUrl && <img src={p.iconUrl} alt="" width={48} height={48} loading="lazy" />}
                  <span className="grow" style={{ minWidth: 0 }}><strong className="ellipsis">{p.name}</strong><span className="small muted ellipsis" style={{ display: "block" }}>{p.description}</span></span>
                </button>
              )} />
            )}
            {!results.loading && !results.error && results.data?.projects.length === 0 && <p className="muted">{t("No results")}</p>}
            <div className="row">
              <button className="btn" disabled={offset === 0 || results.loading} onClick={() => { setOffset(Math.max(0, offset - 25)); setProject(undefined); }}>{t("Previous")}</button>
              <span className="grow small muted">{t("Page %1", Math.floor(offset / 25) + 1)}</span>
              <button className="btn" disabled={results.loading || !!results.error || (results.data?.projects.length ?? 0) < 25} onClick={() => { setOffset(offset + 25); setProject(undefined); }}>{t("Next")}</button>
            </div>
          </section>
          <section className="card stack">
            <h2>{project?.name ?? t("Select a modpack")}</h2>
            {project && <>
              <p>{project.description}</p>
              {project.websiteUrl && <button className="btn small" onClick={() => void materialmc.system.openUrl(project.websiteUrl).catch(showError)}>{t("Website")}</button>}
              {versions.error ? <ErrorBanner error={versions.error} onRetry={versions.reload} /> : versions.loading ? <Spinner label={t("Loading versions…")} /> : (
                <VirtualList items={versions.data ?? []} rowHeight={64} style={{ height: 360 }} getKey={(v) => v.id} renderRow={(v) => (
                  <button className={`list-row clickable${version?.id === v.id ? " selected" : ""}`} style={{ height: 64, width: "100%", textAlign: "left" }} onClick={() => setVersion(v)}>
                    <span className="grow" style={{ minWidth: 0 }}><strong className="ellipsis">{v.name}</strong><span className="small muted" style={{ display: "block" }}>{v.gameVersions.join(", ")} · {v.loaders.join(", ")}</span></span>
                    <span className="chip">{v.type}</span>
                    <span className="small muted">{v.date ? formatDate(Date.parse(v.date)) : ""}</span>
                  </button>
                )} />
              )}
              {!versions.loading && !versions.error && versions.data?.length === 0 && <p className="muted">{t("No versions available")}</p>}
            </>}
          </section>
        </div>
      )}
    </div>
  );
}
