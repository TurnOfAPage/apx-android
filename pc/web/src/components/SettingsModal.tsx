import React, { useState } from 'react';
import { bridge, HostState } from '../api/bridge';

interface Props { open: boolean; onClose: () => void; state: HostState | null }

/**
 * 设置 Modal —— 自适应码率 / 日志级别 / 诊断导出。
 * 替代旧版 settings_win32.cpp 原生窗口。
 */
export const SettingsModal: React.FC<Props> = ({ open, onClose, state }) => {
  const [adaptive, setAdaptive] = useState(!!state?.config.adaptiveBitrate);
  const [logLevel, setLogLevel] = useState(state?.config.logLevel as string || 'info');
  const [mbps, setMbps] = useState(state?.config.bitrateMbps ?? 8);

  // 传输状态（C++ buildState.transports）—— USB gadget 挂载三态诊断
  const tp = (state?.transports ?? {}) as {
    usbDevice?: boolean; usbNcm?: boolean; usbHid?: boolean;
    usbName?: string; usbSpeed?: string; btSupported?: boolean;
  };
  const usbSpeedText = tp.usbSpeed && tp.usbSpeed !== 'unknown' ? ` · ${tp.usbSpeed}` : '';
  const usbLine = !tp.usbDevice
    ? '未检测到手机（检查 USB 线 / 手机端 USB 开关）'
    : tp.usbNcm
      ? `NCM 已挂载${usbSpeedText}`
      : '设备在线，NCM 未挂载（手机端重开「有线（USB）」开关）';

  React.useEffect(() => {
    if (state) {
      setAdaptive(!!state.config.adaptiveBitrate);
      setLogLevel(state.config.logLevel as string || 'info');
      setMbps(state.config.bitrateMbps as number ?? 8);
    }
  }, [state]);

  if (!open) return null;

  const apply = async () => {
    await bridge.act('config.setAdaptive', { on: adaptive });
    await bridge.act('config.setLogLevel', { level: logLevel });
    await bridge.act('config.setBitrate', { mbps });
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 animate-fade-in" onClick={onClose}>
      <div className="card card-inner w-[400px] animate-slide-up" onClick={e => e.stopPropagation()}>
        <div className="flex items-center justify-between mb-4">
          <span className="section-title">设置</span>
          <button className="text-on-tertiary hover:text-on-surface text-[18px] leading-none" onClick={onClose}>✕</button>
        </div>

        {/* 传输状态 —— 上游设计「BT/USB 状态走设置页」（App.tsx 第四格注释） */}
        <div className="flex items-center justify-between py-3 border-b border-stroke gap-3">
          <div className="body-text shrink-0">USB 传输</div>
          <div className="data-text text-right">
            <div>{usbLine}</div>
            {tp.usbDevice && tp.usbName && (
              <div className="text-on-tertiary">{tp.usbName}</div>
            )}
          </div>
        </div>
        {tp.btSupported && (
          <div className="flex items-center justify-between py-3 border-b border-stroke">
            <div className="body-text">蓝牙 HID</div>
            <div className="data-text">本机支持</div>
          </div>
        )}

        <div className="flex items-center justify-between py-3 border-b border-stroke">
          <div className="body-text">副屏自适应码率</div>
          <label className="switch" data-on={adaptive} onClick={() => setAdaptive(!adaptive)}>
            <span className="switch-thumb" />
          </label>
        </div>

        <div className="flex items-center justify-between py-3 border-b border-stroke gap-3">
          <div className="body-text">默认码率</div>
          <select className="input-field h-[36px] text-[13px] w-[120px]" value={String(mbps)}
                  onChange={e => setMbps(parseInt(e.target.value))}>
            <option value={8}>8 Mbps</option>
            <option value={12}>12 Mbps</option>
            <option value={16}>16 Mbps</option>
          </select>
        </div>

        <div className="flex items-center justify-between py-3 gap-3">
          <div className="body-text">日志级别</div>
          <select className="input-field h-[36px] text-[13px] w-[120px]" value={logLevel}
                  onChange={e => setLogLevel(e.target.value)}>
            <option value="debug">debug</option>
            <option value="info">info</option>
            <option value="warn">warn</option>
            <option value="error">error</option>
          </select>
        </div>

        <div className="mt-4 flex justify-end gap-2">
          <button className="btn-ghost" onClick={onClose}>取消</button>
          <button className="btn-primary" onClick={apply}>应用</button>
        </div>
      </div>
    </div>
  );
};
