import React, { useState } from 'react';
import { bridge, HostState } from '../api/bridge';
import { StatusBadge } from './StatusBadge';
import { Switch } from './Switch';
import { phaseSentence, upTimeText } from '../utils/status';

interface Props {
  state: HostState;
  onManualConnect: (host: string, port: number) => void;
}

/** transports 里的 USB 三态诊断（C++ refreshUsbStatus 探测，~10s 刷一次） */
interface UsbTransports {
  usbDevice?: boolean;
  usbNcm?: boolean;
  usbHid?: boolean;
  usbName?: string;
  usbSpeed?: string;
  btSupported?: boolean;
}

/**
 * 连接卡 —— 整宽卡，第一屏最重要的信息。
 * impeccable anti-pattern：
 *   ✗ 不要卡套卡（本卡是顶层卡，内部直接布局，不嵌 card）
 *   ✗ 不要装饰性 sparkline（旧面板上的 RTT 历史图在 Web 版删掉 —— convey nothing meaningful）
 *   ✗ 不要玻璃拟态（backdrop-blur）
 *
 * 蓝牙 / USB 开关：老面板连接卡有三个传输开关（无线/蓝牙/USB），其中无线控制
 * 连接、蓝牙与 USB 只是**本地显示开关** —— 真开关在手机端（蓝牙配对 / root 配
 * gadget），PC 是被动 host，架构上无权启停。WebView2 版曾把这两个开关省掉，
 * 用户找不到入口；现补回：拨开展示说明与真实探测状态（USB 三态来自
 * transports，比老面板的静态文案更进一步）。
 *
 * 无线开关（2026-10-17 补齐）：这是**真开关**，直接启停 9511 会话 ——
 * 开 = session.startAuto（自动发现手机），关 = session.disconnect。
 * 状态由 400ms tick 的 link.phase 驱动：未启用(0)/手动失败(4) 视为关。
 */
export const ConnectionCard: React.FC<Props> = ({ state, onManualConnect }) => {
  const [host, setHost] = useState('');
  const [port, setPort] = useState('9511');
  const [btOpen, setBtOpen] = useState(false);
  const [usbOpen, setUsbOpen] = useState(false);
  const s = state.link;
  const tp = (state.transports ?? {}) as UsbTransports;
  const usbSpeedText = tp.usbSpeed && tp.usbSpeed !== 'unknown' ? ` · ${tp.usbSpeed}` : '';
  const usbLine = !tp.usbDevice
    ? '未检测到手机（检查 USB 线 / 手机端 USB 开关）'
    : tp.usbNcm
      ? `NCM 已挂载${usbSpeedText}`
      : '设备在线，NCM 未挂载（手机端重开「有线（USB）」开关）';
  // 无线开关状态：phase 0=未启用 4=手动连接失败 视为关；发现/连接/已连接 视为开
  const wifiOn = s.phase !== 0 && s.phase !== 4;

  return (
    <div className="card animate-fade-in">
      <div className="card-inner flex items-center justify-between gap-4">
        <div className="flex flex-col gap-1">
          <StatusBadge phase={s.phase as 0} />
          <div className="body-text text-on-surface">{phaseSentence(state)}</div>
          {s.peer && (
            <div className="data-text">
              {s.peer} · RTT {s.rttMs.toFixed(0)} ms · 已连 {upTimeText(s.upMs)}
            </div>
          )}
        </div>

        {/* 手动 IP —— 自动发现失败时的兜底入口 */}
        <div className="flex items-center gap-2 shrink-0">
          <input
            className="input-field w-[200px] h-[38px] text-[13px]"
            placeholder="手机 IP"
            value={host}
            onChange={e => setHost(e.target.value)}
            disabled={s.phase === 3 || s.phase === 2}
          />
          <input
            className="input-field w-[72px] h-[38px] text-[13px]"
            placeholder="端口"
            value={port}
            onChange={e => setPort(e.target.value.replace(/\D/g, ''))}
            disabled={s.phase === 3 || s.phase === 2}
          />
          <button
            className="btn-ghost h-[38px]"
            onClick={() => onManualConnect(host, parseInt(port) || 9511)}
            disabled={!host || s.phase === 3 || s.phase === 2}
          >
            手动连接
          </button>
        </div>
      </div>

      {/* 传输开关：无线 = 真开关（启停 9511 会话）；蓝牙 / USB = 本地显示开关（真开关在手机端） */}
      <div className="flex items-center gap-6 px-5 pb-3 -mt-1">
        <label className="flex items-center gap-2 cursor-pointer select-none">
          <span className="body-text text-on-surface">无线</span>
          <Switch
            on={wifiOn}
            onChange={on => bridge.act(on ? 'session.startAuto' : 'session.disconnect')}
          />
        </label>
        <label className="flex items-center gap-2 cursor-pointer select-none">
          <span className="body-text text-on-surface">蓝牙</span>
          <Switch on={btOpen} onChange={setBtOpen} />
        </label>
        <label className="flex items-center gap-2 cursor-pointer select-none">
          <span className="body-text text-on-surface">USB</span>
          <Switch on={usbOpen} onChange={setUsbOpen} />
        </label>
        {(btOpen || usbOpen) && (
          <span className="data-text text-on-tertiary ml-auto">由手机端启用，PC 仅展示状态</span>
        )}
      </div>

      {/* 展开区：拨开哪个显示哪个的状态/说明 */}
      {btOpen && (
        <div className="px-5 pb-4">
          <div className="pt-3 border-t border-stroke">
            <div className="body-text text-on-surface">蓝牙键鼠（手机当无线鼠标 / 键盘）</div>
            <div className="data-text text-on-tertiary mt-1">
              {tp.btSupported
                ? '本机支持蓝牙 · 手机端蓝牙配对后自动启用，PC 免驱识别为鼠标 / 键盘 / 多媒体键'
                : '手机端蓝牙配对后自动启用，PC 免驱识别为鼠标 / 键盘 / 多媒体键'}
            </div>
          </div>
        </div>
      )}
      {usbOpen && (
        <div className="px-5 pb-4">
          <div className="pt-3 border-t border-stroke">
            <div className="body-text text-on-surface">USB 外设（插线即用）</div>
            <div className="data-text text-on-tertiary mt-1">{usbLine}</div>
            {tp.usbDevice && tp.usbName && (
              <div className="data-text text-on-tertiary">{tp.usbName}</div>
            )}
            <div className="data-text text-on-tertiary">
              UAC2 声卡（音箱）+ HID 键鼠 + NCM 网络，免驱零配置
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
