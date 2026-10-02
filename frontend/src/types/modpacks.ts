export type ModpackProvider = "modrinth" | "curseforge" | "legacy-ftb" | "ftb-app";

export interface ModpackProject {
  provider: ModpackProvider;
  id: string;
  slug: string;
  name: string;
  description: string;
  authors: string[];
  iconUrl: string;
  websiteUrl: string;
}

export interface ModpackSearchResult {
  projects: ModpackProject[];
  offset: number;
  sortingMethods: Array<{ id: string; name: string }>;
}

export interface ModpackSearchParams {
  provider: ModpackProvider;
  query: string;
  offset?: number;
  sort?: string;
  minecraftVersion?: string;
}

export interface ModpackVersionsParams {
  provider: ModpackProvider;
  projectId: string;
}

export interface RemoteVersion {
  id: string;
  name: string;
  versionNumber: string;
  type: string;
  gameVersions: string[];
  loaders: string[];
  date: string;
  fileName: string;
  compatible: boolean;
  downloadUrl?: string;
  changelog?: string;
}

export interface ModpackImportParams {
  name: string;
  group?: string | null;
}

export interface ModpackInstallParams extends ModpackVersionsParams, ModpackImportParams {
  versionId: string;
}

