import { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import * as bridge from "./bridge";
import type { Key, Snapshot } from "./bridge";
import "./style.css";
function App() {
  const [state, setState] = useState<Snapshot | null>(null);
  const [targetId, setTargetId] = useState("");
  const [keyIndex, setKeyIndex] = useState(0);
  const [draft, setDraft] = useState<string | null>(null);
  const [localError, setLocalError] = useState("");
  const [filter, setFilter] = useState("");
  const [notice, setNotice] = useState("");
  const last = useRef(""), mounted = useRef(true), pending = useRef(false);
  const update = async () => {
    if (pending.current) return;
    pending.current = true;
    try {
      const next = await bridge.read(), serialized = JSON.stringify(next);
      if (mounted.current && last.current !== serialized) { last.current = serialized; setState(next); }
    } catch { if (mounted.current) setLocalError("暂时无法读取控制状态，连接中断后会自动释放控制。"); }
    finally { pending.current = false; }
  };
  useEffect(() => {
    mounted.current = true; void update();
    const timer = window.setInterval(() => { if (!document.hidden) void update(); }, 250);
    const refresh = () => { if (!document.hidden) void update(); };
    document.addEventListener("visibilitychange", refresh);
    return () => { mounted.current = false; clearInterval(timer); document.removeEventListener("visibilitychange", refresh); };
  }, []);
  const target = state?.targets.find(t => t.handle === targetId) ?? state?.targets[0];
  const key = target?.keys.find(k => k.index === keyIndex) ?? target?.keys[0];
  useEffect(() => { setDraft(null); setFilter(""); setKeyIndex(0); }, [target?.handle, state?.generation]);
  const owned = state?.targets.flatMap(t => t.keys).filter(k => k.owned).length ?? 0;
  const send = async (action: "set" | "drop" | "release", value = 0, selected: Key | undefined = key) => {
    if (!state) return;
    try {
      setLocalError(""); setNotice("");
      await bridge.change(action, target?.handle ?? "0", selected?.index ?? 0, value, state.generation);
      if (action !== "set") setDraft(null);
      if (action === "release") setNotice("已请求释放本窗口的全部控制。");
      await update();
    } catch (error) { setLocalError(String(error).replace(/^Error: /, "")); }
  };
  const apply = (event: React.FormEvent) => {
    event.preventDefault(); if (!key) return;
    const input = draft ?? bridge.number(key.owned ? key.contribution : key.requested);
    const value = Number(input.trim().replace(",", "."));
    if (!input.trim() || !Number.isFinite(value) || value < key.minimum || value > key.maximum) {
      setLocalError("请输入 " + bridge.number(key.minimum) + " 至 " + bridge.number(key.maximum) + " 之间的权重。"); return;
    }
    void send("set", value);
  };
  const source = (k: Key) => k.owned ? k.source === state?.source ? "本窗口控制" : "其他来源优先" : k.source !== "0" ? "其他来源控制" : "未接管";
  const shown = target?.keys.filter(k => k.name.toLocaleLowerCase().includes(filter.toLocaleLowerCase())) ?? [];
  const progress = key && key.maximum > key.minimum ? Math.max(0, Math.min(100, 100 * (key.applied - key.minimum) / (key.maximum - key.minimum))) : 0;
  return <main className="app">
    <header className="masthead">
      <div className="identity"><span className="wordmark">MHWI<span className="wordmark-divider"> / </span>EquipmentMorph</span><span className="scope">装备形态键控制</span></div>
      <div className="window-actions"><span className="shortcut"><kbd>Ctrl</kbd><span>＋</span><kbd>Shift</kbd><span>＋</span><kbd>M</kbd></span><button className="quiet" onClick={() => void bridge.hide()}>隐藏窗口</button></div>
    </header>
    {bridge.preview && <p className="preview-note">界面预览 · 合成数据，没有连接游戏</p>}
    <section className="page-heading"><div><h1>当前装备</h1><p>选择形态键，调整它在游戏中的权重。</p></div><div className="connection" role="status"><span className={"signal " + (state?.connected ? "online" : "")} aria-hidden="true" /><span>{!state ? "正在连接" : state.connected ? state.targets.length ? "已连接游戏" : "等待装备绑定" : "等待游戏连接"}</span>{!!state?.pid && <span className="pid">进程 {state.pid}</span>}</div></section>
    <section className="equipment">
      <label htmlFor="equipment">装备部位与模型</label>
      <select id="equipment" value={target?.handle ?? ""} disabled={!state?.targets.length} onChange={e => { setTargetId(e.target.value); setKeyIndex(0); setDraft(null); }}>
        {!state?.targets.length && <option value="">装备带有形态键数据的模型后，将自动显示在这里</option>}
        {state?.targets.map(t => <option key={t.handle} value={t.handle}>{bridge.parts[t.part]} · {t.model.split("/").at(-1)}</option>)}
      </select>
      <p className="model-path">{target?.model ?? "模型与同名 .morph 数据包会在游戏启动时自动发现。"}</p>
    </section>
    {(localError || state?.error || state?.shortcutError) && <p className="error" role="alert">{localError || state?.error || state?.shortcutError}</p>}
    <div className="workspace">
      <section className="key-list" aria-labelledby="keys-heading">
        <div className="section-heading"><h2 id="keys-heading">形态键</h2><span>{target?.keys.length ?? 0} 个可用</span></div>
        <label className="search"><span className="sr-only">搜索形态键</span><input type="search" value={filter} onChange={e => setFilter(e.target.value)} placeholder="搜索形态键名称" disabled={!target?.keys.length} /></label>
        <div className="table-scroll"><table><thead><tr><th scope="col">名称</th><th scope="col" className="numeric">目标权重</th><th scope="col" className="numeric">已应用</th><th scope="col">控制来源</th></tr></thead><tbody>
          {shown.map(k => <tr key={k.index} className={key?.index === k.index ? "selected" : ""} onClick={() => { setKeyIndex(k.index); setDraft(null); setLocalError(""); }}>
            <td><button className="key-name" title={k.name} aria-pressed={key?.index === k.index} onClick={() => { setKeyIndex(k.index); setDraft(null); }}>{k.name}</button></td><td className="numeric">{bridge.number(k.requested)}</td><td className="numeric applied">{bridge.number(k.applied)}</td><td className="source">{source(k)}</td>
          </tr>)}
        </tbody></table>
        {!shown.length && <div className="empty"><h3>{filter ? "没有匹配的形态键" : state?.connected ? "等待带有形态键的装备" : "等待游戏连接"}</h3><p>{filter ? "换一个名称搜索；已有接管不会改变。" : state?.connected ? "完成装备自动绑定后，可用形态键会显示在这里。" : "游戏运行时会自动启动本程序。请确认运行时与控制桥接已经安装。"}</p></div>}</div>
      </section>
      <section className="editor" aria-labelledby="editor-heading">
        <div className="section-heading"><h2 id="editor-heading">调节形态键</h2><span>{key ? bridge.parts[target!.part] : "未选择"}</span></div>
        <h3 className="selected-name" title={key?.name}>{key?.name ?? "选择一个形态键"}</h3>
        <p className="range">允许范围 <span>{key ? bridge.number(key.minimum) + " ～ " + bridge.number(key.maximum) : "—"}</span></p>
        <form onSubmit={apply}><label htmlFor="weight">本窗口权重</label><div className="input-action"><input id="weight" inputMode="decimal" autoComplete="off" value={draft ?? (key ? bridge.number(key.owned ? key.contribution : key.requested) : "")} onChange={e => setDraft(e.target.value)} disabled={!key} aria-describedby="range-help" /><button className="primary" type="submit" disabled={!key}>应用权重</button></div></form>
        <label className="takeover"><input type="checkbox" checked={key?.owned ?? false} disabled={!key} onChange={e => e.target.checked ? apply({ preventDefault() {} } as React.FormEvent) : void send("drop")} /><span>由本窗口接管</span></label>
        <input className="weight-slider" type="range" aria-label="连续调节权重" min={key?.minimum ?? 0} max={key?.maximum ?? 1} step={key && key.maximum > key.minimum ? (key.maximum - key.minimum) / 1000 : 1} value={key?.owned ? key.contribution : key?.requested ?? 0} disabled={!key?.owned || key.minimum === key.maximum} onChange={e => { const v = Number(e.target.value); setDraft(bridge.number(v)); void send("set", v); }} />
        <div className="endpoints"><span>{key ? bridge.number(key.minimum) : "—"}</span><span>{key ? bridge.number(key.maximum) : "—"}</span></div>
        <p className="field-help" id="range-help">{key?.owned ? key.source === state?.source ? "拖动即时调整；取消接管只释放此键。" : "本窗口已接管，但其他来源优先；本窗口的贡献仍保留。" : "应用权重即可接管；接管后可拖动滑块连续调整。"}</p>
        <div className="applied-readout"><div><span>已应用到运行时</span><strong>{key ? bridge.number(key.applied) : "—"}</strong></div><div className="meter" role="meter" aria-label="已应用权重" aria-valuemin={key?.minimum ?? 0} aria-valuemax={key?.maximum ?? 1} aria-valuenow={key?.applied ?? 0}><span style={{ width: progress + "%" }} /></div><p>已应用值随平滑过程逐步更新。</p></div>
      </section>
    </div>
    <footer className="footer"><div><button className="secondary" disabled={!owned} onClick={() => void send("release")}>{"释放全部控制" + (owned ? "（" + owned + "）" : "")}</button><p>权重 0 仍属于接管。隐藏窗口保留权重；点击“×”释放控制。</p></div><button className="quiet" onClick={() => void bridge.exit()}>退出程序</button></footer>
    <p className="feedback" role="status">{notice || "换装后不重放旧权重 · Ctrl＋Shift＋M 呼出或隐藏"}</p>
  </main>;
}
createRoot(document.getElementById("root")!).render(<App />);
