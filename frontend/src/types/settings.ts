/**
 * Global launcher settings exposed to the web UI.
 *
 * The backend keeps an explicit allow-list (`SettingsApi.cpp`). "Sensitive" keys (anything that decides which programs or
 * libraries run: Java path, JVM arguments, commands, environment, native library paths, external tools, folder locations
 * typed by hand) are only written after the user confirms the change in a native dialog; a rejected confirmation makes
 * `settings.set` fail with `PERMISSION_DENIED`. Folder and program paths can instead be chosen natively with
 * `settings.pickFolder` / `settings.pickFile`. Secrets (tokens, passwords) are write-only: `settings.get` returns ""
 * for them and reports in `_secretsSet` whether a value is stored.
 */
export interface LauncherSettings {
  // Folders
  InstanceDir: string;
  CentralModsDir: string;
  IconsDir: string;
  DownloadsDir: string;
  SkinsDir: string;
  JavaDir: string;
  AdditionalInstanceDirs: string[];
  DownloadsDirWatchRecursive: boolean;
  MoveModsFromDownloadsDir: boolean;
  // Java
  JavaPath: string;
  JvmArgs: string;
  MinMemAlloc: number;
  MaxMemAlloc: number;
  PermGen: number;
  LowMemWarning: boolean;
  AutomaticJavaSwitch: boolean;
  AutomaticJavaDownload: boolean;
  IgnoreJavaCompatibility: boolean;
  IgnoreJavaWizard: boolean;
  UserAskedAboutAutomaticJavaDownload: boolean;
  // Game window
  LaunchMaximized: boolean;
  MinecraftWinWidth: number;
  MinecraftWinHeight: number;
  CloseAfterLaunch: boolean;
  QuitAfterGameStop: boolean;
  // Console
  ShowConsole: boolean;
  AutoCloseConsole: boolean;
  ShowConsoleOnError: boolean;
  ConsoleMaxLines: number;
  ConsoleOverflowStop: boolean;
  ConsoleFont: string;
  ConsoleFontSize: number;
  LogPrePostOutput: boolean;
  // Game time
  ShowGameTime: boolean;
  ShowGlobalGameTime: boolean;
  RecordGameTime: boolean;
  ShowGameTimeWithoutDays: boolean;
  // Workarounds / performance
  OnlineFixes: boolean;
  UseNativeOpenAL: boolean;
  CustomOpenALPath: string;
  UseNativeGLFW: boolean;
  CustomGLFWPath: string;
  UseNativeSDL: boolean;
  CustomSDLPath: string;
  EnableFeralGamemode: boolean;
  EnableMangoHud: boolean;
  UseDiscreteGpu: boolean;
  UseZink: boolean;
  // Custom commands and environment (sensitive)
  PreLaunchCommand: string;
  WrapperCommand: string;
  PostExitCommand: string;
  /** JSON object of environment variables. */
  Env: string;
  // Mods
  ModMetadataDisabled: boolean;
  ModDependenciesDisabled: boolean;
  SkipModpackUpdatePrompt: boolean;
  ShowModIncompat: boolean;
  DownloadGameFilesDuringInstanceCreation: boolean;
  /** JSON array of "release" | "beta" | "alpha"; empty = all. */
  ModUpdateReleaseTypes: string;
  // User interface
  InstSortMode: "Name" | "LastLaunch" | "Playtime" | string;
  InstRenamingMode: "AskEverytime" | "PhysicalDir" | "MetadataOnly" | string;
  EditInstanceOnDoubleClick: boolean;
  EnableCat: boolean;
  TheCat: boolean;
  BackgroundCat: string;
  CatOpacity: number;
  CatFit: "fit" | "fill" | "strech" | string;
  LastUsedGroupForNewInstance: string;
  LastOfflinePlayerName: string;
  // Network
  NumberOfConcurrentDownloads: number;
  NumberOfConcurrentTasks: number;
  NumberOfManualRetries: number;
  RequestTimeout: number;
  ProxyType: "Default" | "None" | "SOCKS5" | "HTTP" | string;
  ProxyAddr: string;
  ProxyPort: number;
  ProxyUser: string;
  /** Secret (write-only). */
  ProxyPass: string;
  // Services / API
  PastebinType: number;
  PastebinCustomAPIBase: string;
  MetaURLOverride: string;
  ResourceURLOverride: string;
  LegacyFMLLibsURLOverride: string;
  MetaRefreshOnLaunch: boolean;
  FallbackMRBlockedMods: boolean;
  UserAgentOverride: string;
  /** Secrets (write-only). */
  MSAClientIDOverride: string;
  ModrinthToken: string;
  FlameKeyOverride: string;
  TechnicClientID: string;
  FTBAppInstancesPath: string;
  // External tools (sensitive)
  JsonEditor: string;
  JProfilerPath: string;
  JProfilerPort: number;
  JVisualVMPath: string;
  /** JSON object { name: command } of world tools. */
  WorldTools: string;

  /** Which secrets are stored (values are never sent to the page). */
  _secretsSet: Partial<Record<SecretSettingKey, boolean>>;
}

export type SecretSettingKey = "ProxyPass" | "MSAClientIDOverride" | "ModrinthToken" | "FlameKeyOverride" | "TechnicClientID";

export type LauncherSettingKey = Exclude<keyof LauncherSettings, "_secretsSet">;

export type FolderSettingKey =
  | "InstanceDir"
  | "CentralModsDir"
  | "IconsDir"
  | "DownloadsDir"
  | "SkinsDir"
  | "JavaDir"
  | "AdditionalInstanceDirs"
  | "FTBAppInstancesPath";

export type FileSettingKey = "JavaPath" | "JsonEditor" | "JProfilerPath" | "JVisualVMPath" | "CustomOpenALPath" | "CustomGLFWPath" | "CustomSDLPath";

export interface SettingsChangedEvent {
  keys: LauncherSettingKey[];
}
