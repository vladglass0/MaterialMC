/**
 * Questions the backend asks the user (launcher/interaction/UserInteraction.h, launcher/api/PromptApi.cpp).
 * Shown by `components/PromptHost.tsx`.
 */

export type PromptKind =
  | "message"
  | "text"
  | "optionalMods"
  | "untrustedMods"
  | "blockedMods"
  | "networkFailed"
  | "list"
  | "choice"
  | "review";

export type PromptButtonRole = "accept" | "reject" | "destructive" | "neutral";

export interface PromptButton {
  id: string;
  label: string;
  role: PromptButtonRole;
}

export interface Prompt {
  id: number;
  kind: PromptKind;
  title: string;
  /** May contain a small HTML subset (b, i, br, p, a, ul, li); rendered by `RichText`, never as raw HTML. */
  text: string;
  icon: "" | "info" | "warning" | "error" | "question";
  buttons: PromptButton[];
  defaultButton: string | null;
  checkbox: string | null;
  payload: Record<string, unknown>;
}

export interface PromptAnswerParams {
  promptId: number;
  /** Empty or missing when dismissed. */
  button?: string;
  checked?: boolean;
  data?: Record<string, unknown>;
}

export interface BlockedModInfo {
  name: string;
  url: string;
  hash: string;
  matched: boolean;
  localPath: string;
}

export interface I18nCatalogKey {
  s: string;
  c?: string[][];
  n?: boolean;
}

export interface I18nCatalog {
  language: string;
  locale: string;
  strings: Record<string, string>;
  plurals: Record<string, Record<string, string>>;
}
