import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { checkContract, materialmc } from "./api/client";
import "@fontsource-variable/roboto-flex/wght.css";
import "./styles/global.css";
import { initTheme } from "./theme/theme";
import { loadTranslations } from "./i18n";

initTheme();
void loadTranslations();

createRoot(document.getElementById("root") as HTMLElement).render(
  <StrictMode>
    <App />
  </StrictMode>,
);

if (import.meta.env.DEV && materialmc.connected) {
  void checkContract();
}
