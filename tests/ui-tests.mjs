import { resolve } from "node:path";

// CI sets LOGOS_QT_MCP automatically; for interactive use: nix build .#test-framework -o result-mcp
const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

test("chess_ui: loads UI", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Logos Chess"]); },
    { timeout: 15000, interval: 500, description: "UI to load" }
  );
});

test("chess_ui: connects to backend", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["Connected"]); },
    { timeout: 15000, interval: 500, description: "backend to connect" }
  );
});

run();
