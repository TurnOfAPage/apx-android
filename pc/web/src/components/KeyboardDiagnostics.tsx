import React from 'react';
import { HostState } from '../api/bridge';

interface Props {
  state: HostState;
}

/** 最近收到的输入事件（C++ 在 dispatch 时记录，供诊断键盘实现 bug）。 */
interface RecentInput {
  t: number;      // 收到时刻（epoch ms）
  kind: string;   // key / mouse / consumer / touch
  detail: string; // 人读描述
}

interface PadKbStatus {
  available?: boolean;
  name?: string;
}

/**
 * 键盘 / 触摸板诊断卡 —— 检测定位键盘实现 bug。
 *  - pad 实体键盘可用状态（手机端经 9511 上报）
 *  - 实时事件计数（C++ 注入侧统计）
 *  - 最近输入事件流（最近 N 条 keydown/keyup/鼠标/触摸）
 */
export const KeyboardDiagnostics: React.FC<Props> = ({ state }) => {
  const counts = state.counts ?? { mouse: 0, keyboard: 0, consumer: 0, dropped: 0 };
  const padKb = ((state as any).padKb ?? {}) as PadKbStatus;
  const recent = ((state as any).recentInputs ?? []) as RecentInput[];

  return (
    <div className="card animate-fade-in">
      <div className="card-inner">
        <div className="flex items-center justify-between mb-2">
          <span className="section-title">键盘 / 触摸板诊断</span>
          <span className="data-text text-on-tertiary">检测定位键盘实现 bug</span>
        </div>

        {/* pad 实体键盘状态 */}
        <div className="flex items-center justify-between py-2 border-b border-stroke">
          <span className="body-text text-on-surface">Pad 实体键盘</span>
          <div className="flex items-center gap-2">
            <span className={`w-[8px] h-[8px] rounded-full ${padKb.available ? 'bg-state-ok' : 'bg-stroke'}`} />
            <span className="data-text text-on-tertiary">
              {padKb.available ? (padKb.name || '已连接') : '未检测到 / 未上报'}
            </span>
          </div>
        </div>

        {/* 事件计数 */}
        <div className="grid grid-cols-4 gap-3 py-3">
          <CountCell label="键盘" value={counts.keyboard} />
          <CountCell label="鼠标/触摸板" value={counts.mouse} />
          <CountCell label="多媒体" value={counts.consumer} />
          <CountCell label="丢弃" value={counts.dropped} alert={counts.dropped > 0} />
        </div>

        {/* 最近输入事件流 */}
        <div className="pt-2">
          <div className="data-text text-on-tertiary mb-1">最近输入事件（新→旧）</div>
          {recent.length === 0 ? (
            <div className="data-text text-on-tertiary py-2">
              暂无事件 —— 在手机端操作键盘 / 触摸板，这里会实时显示收到的按键与触摸事件。
            </div>
          ) : (
            <div className="flex flex-col gap-1 max-h-[160px] overflow-auto">
              {recent.map((r, i) => (
                <div key={`${r.t}-${i}`} className="flex items-center gap-2 data-text">
                  <span className="text-on-tertiary shrink-0">{new Date(r.t).toLocaleTimeString()}</span>
                  <span className="shrink-0 text-on-surface">{r.kind}</span>
                  <span className="text-on-tertiary truncate">{r.detail}</span>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

function CountCell({ label, value, alert }: { label: string; value: number; alert?: boolean }) {
  return (
    <div className="flex flex-col items-start">
      <span className="data-text text-on-tertiary">{label}</span>
      <span className={`body-text ${alert ? 'text-state-error' : 'text-on-surface'}`}>{value}</span>
    </div>
  );
}
