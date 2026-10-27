import React, { useEffect, useState } from 'react';
import { bridge, HostState } from './api/bridge';
import { ConnectionCard } from './components/ConnectionCard';
import { SpeakerCard } from './components/SpeakerCard';
import { MicCard } from './components/MicCard';
import { KeyboardDiagnostics } from './components/KeyboardDiagnostics';
import { RemoteInputModal } from './components/RemoteInputModal';
import { SettingsModal } from './components/SettingsModal';

/**
 * 主面板 —— v1.0_test：移除副屏与文件互传，新增键盘/触摸板诊断。
 */
export default function App() {
  const [state, setState] = useState<HostState | null>(null);

  // Modal 开关
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [remoteInputOpen, setRemoteInputOpen] = useState(false);
  const [remoteInputHint, setRemoteInputHint] = useState('');

  // —— 400ms 状态推流（C++ tickState）——
  useEffect(() => bridge.subscribeState(s => setState(s)), []);

  // —— 事件监听（C++ postEvent）——
  useEffect(() => bridge.onEvent('on-remote-input-request', (evt: any) => {
    setRemoteInputHint(evt.data?.hint || '');
    setRemoteInputOpen(true);
  }), []);

  const handleManualConnect = async (host: string, port: number) => {
    if (!host) return;
    await bridge.act('session.connectManual', { host, port });
  };

  const handleRemoteSend = async (text: string) => {
    if (!text) { setRemoteInputOpen(false); return; }
    await bridge.act('session.sendInputText', { text, flags: 0x04 });
    await bridge.act('session.sendInputDone');
    setRemoteInputOpen(false);
  };

  if (!state) {
    return (
      <div className="w-screen h-screen flex items-center justify-center bg-bg">
        <div className="data-text text-on-tertiary">等待宿主状态…</div>
      </div>
    );
  }

  return (
    <>
      <div className="w-screen h-screen bg-bg p-4 overflow-auto">
        {/* 顶部 App 徽章（右上角） */}
        <div className="flex justify-end mb-2">
          <div className="badge" data-state="ok">
            <span className="w-[6px] h-[6px] rounded-full bg-state-ok" />
            AllPeriph
          </div>
        </div>

        {/* 连接卡 —— 整宽 */}
        <ConnectionCard state={state} onManualConnect={handleManualConnect} />

        {/* 双栏功能区 */}
        <div className="mt-[14px] grid grid-cols-2 gap-[14px]">
          <SpeakerCard state={state} />
          <MicCard state={state} />
        </div>

        {/* 键盘 / 触摸板诊断（检测定位键盘实现 bug） */}
        <div className="mt-[14px]">
          <KeyboardDiagnostics state={state} />
        </div>

        {/* 底部 */}
        <div className="mt-[14px]">
          <FooterWithActions
            state={state}
            onSettings={() => setSettingsOpen(true)}
          />
        </div>
      </div>

      {/* Modal 层 */}
      <SettingsModal open={settingsOpen} onClose={() => setSettingsOpen(false)} state={state} />
      <RemoteInputModal
        open={remoteInputOpen}
        hint={remoteInputHint}
        onClose={() => setRemoteInputOpen(false)}
        onSend={handleRemoteSend}
      />
    </>
  );
}

function FooterWithActions({ state, onSettings }: {
  state: HostState;
  onSettings: () => void;
}) {
  const [autostart, setAutostart] = useState(false);
  React.useEffect(() => { setAutostart(!!state.config.autostart); }, [state.config.autostart]);

  const toggleAutostart = async () => {
    const next = !autostart;
    setAutostart(next);
    await bridge.act('tray.setAutostart', { enable: next });
  };

  return (
    <div className="flex items-center justify-between gap-3 pt-2">
      <div className="flex items-center gap-3">
        <label className="switch" data-on={autostart} onClick={toggleAutostart}>
          <span className="switch-thumb" />
        </label>
        <span className="body-text text-on-surface">开机自启</span>
      </div>

      <div className="flex items-center gap-2">
        <button className="btn-ghost h-[38px]" onClick={() => bridge.act('window.hide')}>隐藏</button>
        <button className="btn-ghost h-[38px]" onClick={onSettings}>设置</button>
        <button className="btn-primary h-[38px]" onClick={() => bridge.act('window.quit')}>退出</button>
      </div>
    </div>
  );
}
