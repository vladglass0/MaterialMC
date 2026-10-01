import { Fragment, useState } from "react";
import { NavLink, Outlet, useLocation } from "react-router-dom";
import { materialmc } from "../api/client";
import {
  mdiAccountCircle,
  mdiAccountCircleOutline,
  mdiConsole,
  mdiCog,
  mdiCogOutline,
  mdiCube,
  mdiEarth,
  mdiHome,
  mdiHomeOutline,
  mdiInformation,
  mdiInformationOutline,
  mdiMenu,
  mdiPuzzle,
  mdiPuzzleOutline,
  mdiTrayArrowDown,
  mdiViewGrid,
  mdiViewGridOutline,
} from "@mdi/js";
import { useAccounts, useTasks } from "../hooks/stores";
import { Icon } from "./Icon";

// Material navigation shows the filled icon variant for the active destination.
const NAV = [
  { to: "/", label: "Home", icon: mdiHomeOutline, activeIcon: mdiHome, end: true },
  { to: "/instances", label: "Instances", icon: mdiViewGridOutline, activeIcon: mdiViewGrid },
  { to: "/mods", label: "Mods", icon: mdiPuzzleOutline, activeIcon: mdiPuzzle },
  { to: "/worlds", label: "Worlds", icon: mdiEarth, activeIcon: mdiEarth },
  { to: "/downloads", label: "Downloads", icon: mdiTrayArrowDown, activeIcon: mdiTrayArrowDown },
  { to: "/accounts", label: "Accounts", icon: mdiAccountCircleOutline, activeIcon: mdiAccountCircle, divider: true },
  { to: "/console", label: "Console", icon: mdiConsole, activeIcon: mdiConsole },
  { to: "/settings", label: "Settings", icon: mdiCogOutline, activeIcon: mdiCog },
  { to: "/about", label: "About", icon: mdiInformationOutline, activeIcon: mdiInformation },
] as const;

export function Layout() {
  const [open, setOpen] = useState(false);
  const location = useLocation();
  const tasks = useTasks();
  const accounts = useAccounts();
  const running = tasks.data.filter((t) => t.state === "running").length;
  const defaultAccount = accounts.data.find((a) => a.isDefault);

  return (
    <div className="app">
      <header className="topbar">
        <button className="btn icon-only ghost menu-toggle" aria-label="Toggle navigation" onClick={() => setOpen((o) => !o)}>
          <Icon path={mdiMenu} />
        </button>
        <div className="brand">
          <div className="brand-mark" aria-hidden>
            <Icon path={mdiCube} size={20} />
          </div>
          MaterialMC
        </div>
        <div className="spacer" />
        {running > 0 && (
          <NavLink to="/downloads" className="chip info task-chip">
            <span className="spinner" style={{ width: 12, height: 12, borderWidth: 2 }} /> {running} task{running > 1 ? "s" : ""}
          </NavLink>
        )}
        <NavLink to="/accounts" className="account">
          {defaultAccount ? (
            <>
              <img src={defaultAccount.faceUrl} alt="" />
              <span className="ellipsis" style={{ maxWidth: 160 }}>
                {defaultAccount.profileName || "Account"}
              </span>
            </>
          ) : (
            <span className="chip warn">No default account</span>
          )}
        </NavLink>
      </header>
      {open && <div className="sidebar-scrim" onClick={() => setOpen(false)} />}
      <nav className={`sidebar${open ? " open" : ""}`} onClick={() => setOpen(false)}>
        {NAV.map((item) => (
          <Fragment key={item.to}>
            {"divider" in item && <div className="divider" role="separator" />}
            <NavLink to={item.to} end={"end" in item ? item.end : false}>
              {({ isActive }) => (
                <>
                  <span className="icon">
                    <Icon path={isActive ? item.activeIcon : item.icon} />
                    {item.to === "/downloads" && running > 0 && <span className="chip info badge">{running}</span>}
                  </span>
                  <span className="label">{item.label}</span>
                </>
              )}
            </NavLink>
          </Fragment>
        ))}
      </nav>
      <main className="main" key={location.pathname.split("/")[1]}>
        {!materialmc.connected && (
          <div className="banner error" style={{ marginBottom: 16 }}>
            <strong>Backend not connected.</strong> This UI only works inside the MaterialMC application (or a dev
            session started with <code>MATERIALMC_DEV_URL</code>). No data is shown without it.
          </div>
        )}
        <Outlet />
      </main>
    </div>
  );
}
