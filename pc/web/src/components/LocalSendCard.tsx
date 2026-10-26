import React, { useState } from 'react';
import { bridge, HostState } from '../api/bridge';
import { Switch } from './Switch';

interface Props { state: HostState }

/**
 * 互传卡 —— 双栏右下（旧面板此格是 USB 卡）。
 *
 * 链路（web_panel.cpp 的 handleAct 与 panel_http.cpp 的 handleAct 逐分支同构）：
 *   开/关：act localsend.toggle {enable} → C++ LocalSendReceiver 起/停 53317
 *          （被占用时自动回退 53318/53319，实际端口由 announce 带出，界面照实显示）
 *   收文件：C++ postEvent on-localsend-received（托盘气泡 + 本卡计数/高亮）
 *   列表：400ms 的 state.localsend 刷新（附近设备由多播 announce 积累）
 *   目录：act localsend.openDir → 系统文件管理器打开落盘目录（下载/AllPeriph）
 *
 * 字段口径由 apxpc::localsend::statusJson 统一给出（C++ 侧 testStatusJson 钉住），
 * 前端不各取各的字段名 —— 改名即破坏这里，所以两边一起改。
 */
export const LocalSendCard: React.FC<Props> = ({ state }) => {
  const ls = state.localsend;
  const running = ls?.running ?? false;
  const devices = ls?.devices ?? [];
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [lastFrom, setLastFrom] = useState('');
  const [status, setStatus] = useState('');

  // 收到文件：本卡给一句「刚收到 X 发来的文件」，不必开文件面板也能确认
  React.useEffect(() => bridge.onEvent('on-localsend-received', (evt: any) => {
    const from = evt?.data?.from || '';
    setLastFrom(from ? `${from} 发来文件` : '收到文件');
    setErr('');
  }), []);

  // 发送结果（C++ 后台线程推回来）：成功给一句「已发送 N 个到 X」，失败把原因写清
  React.useEffect(() => bridge.onEvent('on-localsend-sent', (evt: any) => {
    const d = evt?.data || {};
    setStatus('');
    if (d.ok) setErr('');
    else setErr(d.error || '发送失败');
    if (d.ok) setLastFrom(`已发送 ${d.count} 个文件到 ${d.peerAlias || d.ip}`);
  }), []);

  const toggle = async (enable: boolean) => {
    setBusy(true);
    setErr('');
    try {
      await bridge.act('localsend.toggle', { enable });
    } catch (e: any) {
      // bridge 对 ok:false 走 reject，C++ 侧 failResp 的文案在 message 里
      setErr(e?.message || '切换失败');
    } finally {
      setBusy(false);
    }
  };

  // 选设备 → 系统文件对话框 → 发送。openFile 只有 WebView2 桌面壳支持；
  // 浏览器面板（apxhost panel）里 bridge 会直接抛「不支持系统文件对话框」，
  // 这里如实显示出来，而不是让按钮点了没反应。
  const sendTo = async (dev: { ip: string; port: number; alias: string }) => {
    setErr('');
    setStatus(`正在发送到 ${dev.alias || dev.ip}…`);
    try {
      const pick: any = await bridge.openFile('全部文件|*.*');
      if (!pick?.ok || !pick?.path) { setStatus(''); return; }   // 用户取消
      await bridge.act('localsend.send', { ip: dev.ip, port: dev.port, paths: [pick.path] });
    } catch (e: any) {
      setStatus('');
      setErr(e?.message || '发送失败');
    }
  };

  const openDir = async () => {
    setErr('');
    try {
      await bridge.act('localsend.openDir');
    } catch (e: any) {
      setErr(e?.message || '打开目录失败');
    }
  };

  return (
    <div className="card card-inner animate-slide-up flex flex-col gap-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="section-title">互传</span>
          {running && (
            <span className="text-[11px] text-state-ok font-mono tracking-[0.04em]">RUNNING</span>
          )}
        </div>
        <Switch on={running} onChange={(v) => { if (!busy) toggle(v); }} />
      </div>

      {running ? (
        <div className="flex items-center gap-4">
          <div className="flex flex-col gap-0.5">
            <span className="caption-text">端口</span>
            <span className="data-text">{ls?.port || '—'}</span>
          </div>
          <div className="flex flex-col gap-0.5">
            <span className="caption-text">附近设备</span>
            <span className="data-text">{devices.length}</span>
          </div>
          <div className="flex flex-col gap-0.5">
            <span className="caption-text">已收</span>
            <span className="data-text">{ls?.received ?? 0}</span>
          </div>
        </div>
      ) : (
        <div className="btn-ghost h-[34px] justify-start px-3 text-[12px] text-on-variant cursor-default">
          {ls?.error && ls.error !== '未启动' ? ls.error : '未开启'}
        </div>
      )}

      {/* 附近设备：多播 announce 积累的近期列表（最多显示 3 条，完整列表在日志里） */}
      {running && devices.length > 0 && (
        <div className="flex flex-col gap-1">
          {devices.slice(0, 3).map((d) => (
            <div key={d.ip + d.port} className="flex items-center justify-between gap-2">
              <span className="body-text text-on-surface truncate">{d.alias || d.ip}</span>
              <button
                className="btn-ghost h-[26px] px-2 text-[12px] shrink-0"
                onClick={() => sendTo(d)}
                title={`发送文件到 ${d.alias || d.ip}`}
              >
                发送
              </button>
            </div>
          ))}
        </div>
      )}

      <div className="mt-auto flex flex-col gap-0.5">
        {err ? (
          <div className="caption-text text-state-error">{err}</div>
        ) : status ? (
          <div className="caption-text text-on-variant">{status}</div>
        ) : ls?.warn ? (
          // 多播 53317 绑不上时仍能收直连 —— 如实说明，而不是假装一切正常
          <div className="caption-text text-state-warn">{ls.warn}</div>
        ) : lastFrom ? (
          <div className="caption-text text-state-ok">{lastFrom}</div>
        ) : (
          <div className="caption-text">
            {running ? '手机可直发本机；点设备右侧「发送」把本机文件传过去' : '打开开关：手机 / 官方 LocalSend 可直发到本机'}
          </div>
        )}
        <div className="flex items-center justify-between">
          <span className="data-text truncate" title={ls?.dir || ''}>{ls?.dir || ''}</span>
          <button className="btn-ghost h-[30px] px-2 text-[12px]" onClick={openDir} disabled={!ls?.dir}>
            打开目录
          </button>
        </div>
      </div>
    </div>
  );
};
