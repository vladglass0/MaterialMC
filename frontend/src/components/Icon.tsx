import type { CSSProperties } from "react";

/** A Material Design icon from @mdi/js (pass one of its path constants, e.g. `mdiPlay`). */
export function Icon({ path, size = 24, className, style }: { path: string; size?: number; className?: string; style?: CSSProperties }) {
  return (
    <svg
      className={className ? `md-icon ${className}` : "md-icon"}
      width={size}
      height={size}
      viewBox="0 0 24 24"
      aria-hidden
      focusable="false"
      style={style}
    >
      <path d={path} fill="currentColor" />
    </svg>
  );
}
