import React from 'react';
import { bridge, HostState } from '../api/bridge';
import { Switch } from './Switch';
import { ChipGroup } from './ChipGroup';

interface Props { state: HostState }

const BITRATES = [
  { label: '8 M', value: '8000' },
  { label: '12 M', value: '12000' },
  { label: '16 M', value: '16000' },
];
const RESOLUTIONS = [
  { label: '800×600',  value: '0' },
  { label: '1280×720', value: '1' },
  { label: '1600×900', value: '2' },
  { label: '1920×1080',value: '3' },
];

/**
 * 副屏卡 —— 双栏左上。
 * 动作：开关投屏、镜像/扩展、码率分段、分辨率分段。
 */
export const ScreenCard: React.FC<Props> = ({ state }) => {
  const [on, setOn] = React.useState(false);
  const [mirror, setMirror] = React.useState('mirror');
  const [bitrate, setBitrate] = React.useState('8000');
  const [resIdx, setResIdx] = React.useState('2');

  // 从 C++ state 同步开关状态
  const running = state.media.screen?.running ?? false;
  React.useEffect(() => { setOn(running); }, [running]);

  const toggle = async () => {
    await bridge.act('screen.toggle');
    // 等待状态推流更新
    setTimeout(() => setOn(!on), 400);
  };

  const scr = state.media.screen;

  return (
    <div className="card card-inner animate-slide-up flex flex-col gap-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="section-title">副屏</span>
          {running && <span className="text-[11px] text-state-ok font-mono tracking-[0.04em]">RUNNING</span>}
        </div>
        <Switch on={on} onChange={toggle} />
      </div>

      <div className="flex gap-2">
        <ChipGroup options={[
          { label: '桌面镜像', value: 'mirror' },
          { label: '扩展屏',  value: 'extend' },
        ]} value={mirror} onChange={setMirror} />
      </div>

      <div className="flex items-center gap-2">
        <span className="caption-text shrink-0 w-[42px]">码率</span>
        <ChipGroup options={BITRATES} value={bitrate} onChange={setBitrate} />
      </div>

      <div className="flex items-center gap-2">
        <span className="caption-text shrink-0 w-[42px]">分辨率</span>
        <ChipGroup options={RESOLUTIONS} value={resIdx} onChange={setResIdx} />
      </div>

      {/* 状态大字：说现在能干什么（不要重复徽章的"已连接"） */}
      <div className="mt-1 flex flex-col gap-0.5">
        {scr?.running ? (
          <>
            <div className="body-text text-on-surface">正在投屏</div>
            <div className="data-text">
              {scr.deviceName} · {scr.fps.toFixed(0)} fps · {scr.width}×{scr.height}
              {' · '}编码 {scr.encodeMs.toFixed(1)} ms · 丢帧 {scr.dropped}
            </div>
          </>
        ) : scr?.error ? (
          <>
            <div className="body-text text-state-error">投屏失败</div>
            <div className="caption-text text-state-error">{scr.error}</div>
          </>
        ) : !state.media.connected ? (
          <>
            <div className="body-text text-on-variant">等待媒体连接</div>
            <div className="caption-text">需与手机同一 Wi‑Fi，打开「Wi‑Fi 控制」</div>
          </>
        ) : (
          <>
            <div className="body-text text-on-surface">未开始</div>
            <div className="caption-text">打开右侧开关：把本机桌面投到手机</div>
          </>
        )}

        {/* mac/Linux：本机不挂虚拟显示器，副屏的出路 = 用户自装 Sunshine + Moonlight。
            我们只做检测 + 指引（不内嵌 Moonlight 客户端：GPLv3 与 MIT 冲突，ROADMAP §2.5）。
            C++ 侧没给这些字段时（WebView2/Win 有真副屏）整个块不渲染。 */}
        {scr?.error && !scr.running && typeof scr.sunshineInstalled === 'boolean' && (
          <div className="mt-1 pt-2 border-t border-stroke flex flex-col gap-1">
            {scr.sunshineInstalled ? (
              scr.sunshineRunning ? (
                <>
                  <div className="caption-text text-on-surface">
                    检测到 Sunshine 已在跑（端口 {scr.sunshineHttpsPort}）—— 手机端装 Moonlight 即可连
                  </div>
                  {scr.sunshineUrl && (
                    <a
                      className="caption-text text-state-ok underline underline-offset-2"
                      href={scr.sunshineUrl}
                      target="_blank"
                      rel="noreferrer"
                    >
                      打开 Sunshine 设置页（浏览器会提示自签证书，点「高级 → 继续访问」）
                    </a>
                  )}
                </>
              ) : (
                <div className="caption-text text-on-tertiary">
                  检测到已安装 Sunshine，但 {scr.sunshineHttpsPort} 端口没有服务在监听 —— 打开 Sunshine 应用后刷新本页
                </div>
              )
            ) : (
              <div className="caption-text text-on-tertiary">
                mac 上的副屏出路：安装 <a className="text-state-ok underline underline-offset-2"
                  href="https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html"
                  target="_blank" rel="noreferrer">Sunshine</a>（本机）+ Moonlight（手机）；
                本面板只做检测与指引，不内置客户端（Moonlight 是 GPLv3，与本项目 MIT 冲突）。
                安装后若没被检测到，说明它不在 ~/.config/sunshine/，可在 Sunshine 里确认端口后手动连。
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
};
