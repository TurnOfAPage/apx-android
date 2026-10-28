import React from 'react';
import { bridge, AudioDevice } from '../api/bridge';

/**
 * 播放设备列表（音箱 / 麦克风设备下拉的数据源）。
 *
 * 与后端 query `audioDevices`、Win32 原生面板 refreshSpeakerCombo 同源：
 *   · Windows：真实播放端点；
 *   · macOS：Process Tap 采全局混音、不挂具体端点；Linux：未实现 —— 两者都是空表。
 * 空表就如实显示「本机不支持枚举播放设备」，不编造条目。
 */
export function useAudioDevices() {
  const [devices, setDevices] = React.useState<AudioDevice[]>([]);
  const [defaultName, setDefaultName] = React.useState('');

  React.useEffect(() => {
    let alive = true;
    bridge.audioDevices()
      .then((r) => {
        if (!alive) return;
        setDevices(r.devices);
        setDefaultName(r.defaultName);
      })
      .catch(() => {
        // 查询失败（宿主没起 / 协议不认识）——按「枚举不到」处理，不假装有列表
        if (!alive) return;
        setDevices([]);
        setDefaultName('');
      });
    return () => { alive = false; };
  }, []);

  return { devices, defaultName };
}
