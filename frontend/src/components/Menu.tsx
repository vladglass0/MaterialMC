import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties, type MouseEvent as ReactMouseEvent, type ReactNode } from "react";
import { createPortal } from "react-dom";
import { mdiCheck, mdiDotsVertical } from "@mdi/js";
import { t } from "../i18n";
import { Icon } from "./Icon";

export interface MenuItem {
  label: string;
  onSelect(): void;
  danger?: boolean;
  disabled?: boolean;
  separatorBefore?: boolean;
  /** Shows a check mark (radio / toggle entries such as the profiler choice). */
  checked?: boolean;
  /** Small caption shown above this entry (starts a section, like the Qt "Profilers" separator). */
  section?: string;
  /** Optional leading image (e.g. an account face). */
  image?: string;
}

function MenuList({ items, style, onDone }: { items: MenuItem[]; style: CSSProperties; onDone(): void }) {
  return (
    <div className="menu" role="menu" style={style} onContextMenu={(e) => e.preventDefault()}>
      {items.map((item, index) => (
        <div key={`${index}-${item.label}`} style={{ display: "contents" }}>
          {item.separatorBefore && <hr />}
          {item.section && <div className="menu-section">{item.section}</div>}
          <button
            role={item.checked === undefined ? "menuitem" : "menuitemradio"}
            aria-checked={item.checked}
            className={item.danger ? "danger" : undefined}
            disabled={item.disabled}
            onClick={() => {
              onDone();
              item.onSelect();
            }}
          >
            {item.checked !== undefined && (
              <span className="menu-check">{item.checked && <Icon path={mdiCheck} size={18} />}</span>
            )}
            {item.image && <img src={item.image} alt="" width={20} height={20} style={{ imageRendering: "pixelated" }} />}
            {item.label}
          </button>
        </div>
      ))}
    </div>
  );
}

function useDismiss(open: boolean, close: () => void, inside: () => HTMLElement | null) {
  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (!inside()?.contains(e.target as Node)) close();
    };
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && close();
    window.addEventListener("mousedown", onDown);
    window.addEventListener("keydown", onKey);
    window.addEventListener("blur", close);
    return () => {
      window.removeEventListener("mousedown", onDown);
      window.removeEventListener("keydown", onKey);
      window.removeEventListener("blur", close);
    };
  }, [open, close, inside]);
}

/** A "more" (⋮) icon button that opens a small popup menu. */
export function MenuButton({
  items,
  label,
  children,
  className = "btn icon-only ghost",
}: {
  items: MenuItem[];
  label?: string;
  children?: ReactNode;
  className?: string;
}) {
  const [open, setOpen] = useState(false);
  const [up, setUp] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useDismiss(open, () => setOpen(false), () => ref.current);

  return (
    <div ref={ref} style={{ position: "relative" }}>
      <button
        className={className}
        aria-label={label ?? t("More actions")}
        aria-haspopup="menu"
        onClick={(e) => {
          e.stopPropagation();
          // Open upwards when the menu would not fit below the button.
          const rect = ref.current?.getBoundingClientRect();
          const below = rect ? window.innerHeight - rect.bottom : Infinity;
          setUp(!!rect && below < Math.min(items.length * 48 + 24, 420) && rect.top > below);
          setOpen((o) => !o);
        }}
      >
        {children ?? <Icon path={mdiDotsVertical} />}
      </button>
      {open && (
        <MenuList
          items={items}
          onDone={() => setOpen(false)}
          style={up ? { right: 0, bottom: "calc(100% + 4px)", transformOrigin: "bottom right" } : { right: 0, top: "calc(100% + 4px)" }}
        />
      )}
    </div>
  );
}

/**
 * Right-click menu: `const menu = useContextMenu(); <div onContextMenu={menu.open(items)}>…</div>{menu.element}`.
 */
export function useContextMenu() {
  const [state, setState] = useState<{ x: number; y: number; items: MenuItem[] } | null>(null);
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState<{ left: number; top: number } | null>(null);
  useDismiss(!!state, () => setState(null), () => ref.current);

  useLayoutEffect(() => {
    if (!state) return;
    const el = ref.current?.firstElementChild as HTMLElement | null;
    const w = el?.offsetWidth ?? 220;
    const h = el?.offsetHeight ?? 200;
    setPos({ left: Math.min(state.x, window.innerWidth - w - 8), top: Math.min(state.y, window.innerHeight - h - 8) });
  }, [state]);

  const open = (items: MenuItem[]) => (e: ReactMouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setPos(null);
    setState({ x: e.clientX, y: e.clientY, items });
  };

  const element = state
    ? createPortal(
        <div ref={ref} style={{ position: "fixed", inset: 0, zIndex: 60, pointerEvents: "none" }}>
          <MenuList
            items={state.items}
            onDone={() => setState(null)}
            style={{
              position: "fixed",
              pointerEvents: "auto",
              left: pos?.left ?? state.x,
              top: pos?.top ?? state.y,
              visibility: pos ? "visible" : "hidden",
              transformOrigin: "top left",
            }}
          />
        </div>,
        document.body,
      )
    : null;

  return { open, element };
}
