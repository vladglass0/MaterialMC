/** Component editor (launcher/api/ComponentApi.cpp), formerly the Qt VersionPage. */
import type { InstanceId } from "./instances";

export type ProblemSeverity = "none" | "warning" | "error";

export interface ComponentInfo {
  uid: string;
  name: string;
  version: string;
  enabled: boolean;
  canBeDisabled: boolean;
  moveable: boolean;
  customizable: boolean;
  revertible: boolean;
  removable: boolean;
  custom: boolean;
  versionChangeable: boolean;
  knownModloader: boolean;
  severity: ProblemSeverity;
  problems: Array<{ severity: ProblemSeverity; description: string }>;
}

export interface ComponentList {
  components: ComponentInfo[];
  /** A resolve / update task is running. */
  resolving: boolean;
  running: boolean;
}

export interface ComponentVersion {
  version: string;
  type: string;
  releaseTime: number | null;
  recommended: boolean;
  /** Minecraft version this version requires, if any. */
  minecraft: string | null;
  /** Passes the Qt filter "exact Minecraft version if any version matches it". */
  matchesMinecraft: boolean;
  /** Suggested by the current Minecraft version (LWJGL). */
  suggested: boolean;
}

export interface ComponentVersions {
  uid: string;
  name: string;
  current: string | null;
  minecraftVersion: string;
  versions: ComponentVersion[];
}

export type ComponentRef = { id: InstanceId; uid: string };
export type ComponentFileKind = "jarMods" | "customJar" | "agents" | "components";
