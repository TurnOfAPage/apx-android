import React, { useState } from 'react';
import { bridge, HostState } from '../api/bridge';
import { StatusBadge } from './StatusBadge';
import { phaseSentence, upTimeText } from '../utils/status';

interface Props {
  state: HostState;
  onManualConnect: (host: string, port: number) => void;
}

/** transports 里的 USB / 蓝牙探测状态（C++ refreshUsbStatus 探测，~10s 刷一次） */
interface UsbTransports {
  usbDevice?: boolean;
  usbNcm?: boolean;
  usbHid?: boolean;
  usbName?: string;
  usbSpeed?: string;
  btSupported?: boolean;
}

/**
 * 连接卡 —— v1.0_test：
 *  - 无线 / 蓝牙 / USB 三个选项改为**实时显示手机端三个模式的可用状态**（状态灯 + 文案），
 *    不再是本地拨杆开关；
 *  - 手动 IP 旁新增「扫描局域网」，扫出的 IP 可点击填入。
 */
export const ConnectionCard: React.FC<Props> = ({ state, onManualConnect }) => {
  const [host, setHost] = useState('');
  const [port, setPort] = useState('9511');
  const [scanning, setScanning] = useState(false);
  const [lanIps, setLanIps] = useState<string[]>([]);
  const s = state.link;
  const tp = (state.transports ?? {}) as UsbTransports;

  // 三个模式可用状态（实时）：
  const wifiOk = s.phase === 3;                       // 无线：9511 已连
  const btOk = tp.btSupported === true;               // 蓝牙：本机蓝牙可用（手机端配对后识别）
  const usbOk = tp.usbDevice === true && tp.usbHid === true; // USB：HID 已挂载

  const scanLan = async () => {
    if (scanning) return;
    setScanning(true);
    setLanIps([]);
    try {
      const res = (await bridge.act('session.scanLan', {})) as { ips?: string[] };
      setLanIps(Array.isArray(res?.ips) ? res.ips : []);
    } catch {
      setLanIps([]);
    } finally {
      setScanning(false);
    }
  };

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

        {/* 手动 IP + 扫描局域网 */}
        <div className="flex items-center gap-2 shrink-0">
          <input
            className="input-field w-[170px] h-[38px] text-[13px]"
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
          <button
            className="btn-ghost h-[38px]"
            onClick={scanLan}
            disabled={scanning || s.phase === 3 || s.phase === 2}
          >
            {scanning ? '扫描中…' : '扫描局域网'}
          </button>
        </div>
      </div>

      {/* 扫描结果：IP 可点击填入 */}
      {lanIps.length > 0 && (
        <div className="px-5 pb-3 -mt-1 flex items-center gap-2 flex-wrap">
          <span className="data-text text-on-tertiary">扫描结果：</span>
          {lanIps.map(ip => (
            <button
              key={ip}
              className="chip text-[13px]"
              onClick={() => setHost(ip)}
            >
              {ip}
            </button>
          ))}
        </div>
      )}

      {/* 三个模式实时可用状态 */}
      <div className="flex items-center gap-6 px-5 pb-3">
        <ModeStatus label="无线" ok={wifiOk} hint={wifiOk ? '9511 已连接' : '未连接'} />
        <ModeStatus label="蓝牙" ok={btOk} hint={btOk ? '蓝牙可用' : '本机不支持'} />
        <ModeStatus label="USB" ok={usbOk} hint={usbOk ? 'HID 已挂载' : '未挂载'} />
        <span className="data-text text-on-tertiary ml-auto">实时状态 · 由手机端驱动</span>
      </div>
    </div>
  );
};

function ModeStatus({ label, ok, hint }: { label: string; ok: boolean; hint: string }) {
  return (
    <div className="flex items-center gap-2">
      <span className={`w-[8px] h-[8px] rounded-full ${ok ? 'bg-state-ok' : 'bg-stroke'}`} />
      <span className="body-text text-on-surface">{label}</span>
      <span className="data-text text-on-tertiary">{hint}</span>
    </div>
  );
}
