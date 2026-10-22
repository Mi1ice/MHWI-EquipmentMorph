import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./tests",
  outputDir: "../../.build/control-center-browser-tests",
  reporter: "list",
  use: { channel: "msedge", headless: true, baseURL: "http://127.0.0.1:1420", viewport: { width: 1120, height: 760 } },
});
