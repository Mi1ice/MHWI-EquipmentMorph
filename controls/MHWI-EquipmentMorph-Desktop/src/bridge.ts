import { invoke, isTauri } from "@tauri-apps/api/core";
export type Key = { index: number; name: string; minimum: number; maximum: number; requested: number; applied: number; source: string; owned: boolean; contribution: number };
export type Target = { handle: string; part: number; model: string; keys: Key[] };
export type Snapshot = { connected: boolean; pid: number; generation: string; source: string; targets: Target[]; error: string | null; shortcutError: string | null };
export const native = isTauri();
export const preview = !native && import.meta.env.DEV && new URLSearchParams(location.search).has("fixture");
const disconnected: Snapshot = { connected: false, pid: 0, generation: "0", source: "0", targets: [], error: null, shortcutError: null };
const fixtureKey = (name: string, index: number, weight = 0): Key => ({ index, name, minimum: 0, maximum: 1, requested: weight, applied: weight, source: weight ? "10" : "0", owned: weight !== 0, contribution: weight });
const fixture: Snapshot = {
  connected: true, pid: 4242, generation: "1", source: "10", error: null, shortcutError: null,
  targets: [
    { handle: "18446744073709551613", part: 1, model: "pl/f_equip/pl001_0000/body/mod/f_body001_0000", keys: [fixtureKey("vrc.blink", 0, 0.13724681), fixtureKey("smile", 1), fixtureKey("cheek", 2)] },
    { handle: "22", part: 3, model: "pl/f_equip/pl001_0000/wst/mod/f_wst001_0000", keys: [fixtureKey("adjust", 0)] },
  ],
};
if (preview) {
  const mode = new URLSearchParams(location.search).get("fixture");
  if (mode === "empty") fixture.targets = [];
  if (mode === "disconnected") Object.assign(fixture, disconnected);
  if (mode === "conflict") fixture.error = "形态键互斥冲突，原贡献保留";
  if (mode === "many") fixture.targets[0].keys = Array.from({ length: 64 }, (_, i) => fixtureKey("morph_" + (i + 1), i, i === 0 ? 0.25 : 0));
}
export async function read(): Promise<Snapshot> {
  if (native) return invoke("snapshot");
  return structuredClone(preview ? fixture : disconnected);
}
export async function change(action: "set" | "drop" | "release", target: string, key: number, weight: number, generation: string) {
  if (native) return invoke("change", { action, target, key, weight, generation });
  if (!preview || generation !== fixture.generation) throw new Error("连接已变化，请重新选择形态键");
  const selected = fixture.targets.find(t => t.handle === target)?.keys.find(k => k.index === key);
  if (action !== "release" && !selected) throw new Error("装备或形态键已失效");
  if (action === "release") fixture.targets.flatMap(t => t.keys).forEach(k => Object.assign(k, { owned: false, requested: 0, applied: 0, contribution: 0, source: "0" }));
  else if (selected) {
    const value = action === "drop" ? 0 : Math.fround(weight);
    if (!Number.isFinite(value) || value < selected.minimum || value > selected.maximum) throw new Error("权重超出此形态键的范围");
    Object.assign(selected, { owned: action === "set", requested: value, applied: value, contribution: value, source: action === "set" ? "10" : "0" });
  }
  fixture.error = null;
}
export async function hide() { if (native) await invoke("hide_window"); }
export async function exit() { if (native) await invoke("exit_app"); }
export function number(value: number) { return Number(Math.fround(value).toPrecision(9)).toString(); }
export const parts = ["头部", "身体", "腕部", "腰部", "腿部"];
