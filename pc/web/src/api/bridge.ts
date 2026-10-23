// IPC 桥接：前端 ↔ C++ 宿主。
//
// 三种传输，按优先级判定：
//   ① WebView2：window.chrome.webview.postMessage / addEventListener（Windows 桌面壳，原路径不变）
//   ② HTTP：URL 带 ?host=1 时启用（macOS / 浏览器面板：宿主 `apxhost panel` 起本地服务后
//      open "http://127.0.0.1:<port>/?host=1"）——
//        · act/query → POST /api/act、/api/query，一问一答（响应体即 act-response 协议）
//        · 状态流   → 400ms GET /api/state（与 WebView2 的 400ms PostWebMessage 等价）
//        · 事件     → SSE GET /api/events
//   ③ mock：开发期无宿主时的假状态（原有 fallback，保留）
//
// 协议（与 web_panel.cpp 顶部注释一致）：
//   前端 → 后端：{kind:'act', id, name, payload} | {kind:'query', id, name} | {kind:'open-file', id, filter}
//   后端 → 前端：{kind:'act-response', id, ok, data} | {kind:'query-response', id, ok, data}
//                | {kind:'open-file-response', id, ok, path} | {kind:'state', t, data} | {kind:'event', name, hint}
//
// 所有请求用 id 做 correlation，响应 Promise 化。状态推流独立走 state 订阅。

type Kind = 'act' | 'query' | 'open-file';

type Transport = 'webview' | 'http' | 'mock';

export interface HostState {
  link: {
    phase: number;             // 0=idle 1=disconnecting 2=connecting 3=connected 4=failed
    phaseName: string;
    peer: string;
    rttMs: number;
    upMs: number;
    error: string;
    autoMode: boolean;
  };
  media: {
    connected: boolean;
    peer: string;
    error: string;
    videoFramesSent: number;
    audioFramesSent: number;
    micFrames: number;
    dropped: number;
    screen?: {
      running: boolean; fps: number; deviceName: string;
      width: number; height: number; framesSent: number;
      encodeMs: number; dropped: number; error: string;
    };
    audio?: {
      running: boolean; peak: number; device: string;
      sampleRate: number; channels: number; framesSent: number; error: string;
    };
    mic?: { running: boolean; device: string };
  };
  transports: Record<string, unknown>;
  counts: { mouse: number; keyboard: number; consumer: number; dropped: number };
  config: Record<string, unknown>;
}

interface Request {
  kind: Kind;
  id: string;
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
  timer: number;
}

type Listener = (state: HostState) => void;

class Bridge {
  private pending = new Map<string, Request>();
  private stateListeners = new Set<Listener>();
  private eventListeners = new Map<string, Set<(data: any) => void>>();
  private seq = 0;
  private transport: Transport = 'mock';

  constructor() {
    if (typeof window !== 'undefined' && (window as any).chrome?.webview) {
      (window as any).chrome.webview.addEventListener('message', this.onMessage.bind(this));
      this.transport = 'webview';
    } else if (typeof window !== 'undefined' &&
               new URLSearchParams(window.location.search).has('host')) {
      // ?host=1 由宿主 open 时附加 —— 只有宿主起的服务才走 HTTP 通道，
      // 避免静态部署的页面误连任何本地端口。
      this.transport = 'http';
      console.info('[bridge] HTTP transport (host=1)');
      this.startHttp();
    } else {
      // 开发期 fallback：模拟宿主（无宿主时给个假状态方便调 UI）
      console.warn('[bridge] host not available, running in mock mode');
      this.transport = 'mock';
      this.simulateMockState();
    }
  }

  /** 当前传输方式（组件可据此隐藏不支持的入口，如浏览器面板的系统文件对话框） */
  get mode(): Transport {
    return this.transport;
  }

  // —— HTTP 传输（macOS / 浏览器面板）——

  private startHttp() {
    // 状态流：400ms 轮询，与 WebView2 侧 400ms 定时 PostWebMessageAsJson 语义等价。
    // 只在有订阅者时才 fetch，避免空转（与 mock 行为一致）。
    window.setInterval(async () => {
      if (this.stateListeners.size === 0) return;
      try {
        const r = await fetch('/api/state');
        if (!r.ok) return;
        const msg = await r.json();
        this.handleEnvelope(msg);
      } catch {
        // 服务未就绪 / 瞬断：跳过本 tick，下一轮重试（与状态轮询的容错语义一致）
      }
    }, 400);

    // 事件流：SSE。建连失败则事件静默降级（状态/动作不受影响）。
    try {
      const es = new EventSource('/api/events');
      es.onmessage = (e) => {
        try { this.handleEnvelope(JSON.parse(e.data)); } catch { /* 坏帧忽略 */ }
      };
    } catch {
      console.warn('[bridge] SSE unavailable, events degraded');
    }
  }

  private simulateMockState() {
    const mockInterval = setInterval(() => {
      if (this.stateListeners.size === 0) return;
      const state: HostState = {
        link: {
          phase: 3, phaseName: 'connected',
          peer: '192.168.1.5:9511', rttMs: 12, upMs: 12345000,
          error: '', autoMode: true,
        },
        media: {
          connected: true, peer: '192.168.1.5:9502', error: '',
          videoFramesSent: 1234, audioFramesSent: 567, micFrames: 0, dropped: 2,
          screen: { running: true, fps: 30, deviceName: '\\.\DISPLAY2',
                    width: 1920, height: 1080, framesSent: 1234,
                    encodeMs: 16.2, dropped: 3, error: '' },
          audio: { running: false, peak: 0, device: '', sampleRate: 48000, channels: 2, framesSent: 0, error: '' },
          mic: { running: false, device: '' },
        },
        transports: {},
        counts: { mouse: 4567, keyboard: 890, consumer: 45, dropped: 1 },
        config: { autostart: false, logLevel: 'info' },
      };
      this.stateListeners.forEach(l => l(state));
    }, 400);
    // 不保存 mockInterval —— 让页面存活期内持续推
    (window as any).__mockInterval = mockInterval;
  }

  private onMessage(event: MessageEvent) {
    this.handleEnvelope(event.data);
  }

  /** 统一消息分发：webview 事件、HTTP 响应、SSE 事件都走这里 */
  private handleEnvelope(msg: any) {
    if (!msg || typeof msg !== 'object') return;

    // 状态推流（400ms tick）
    if (msg.kind === 'state') {
      this.stateListeners.forEach(l => l(msg.data));
      return;
    }
    // 事件
    if (msg.kind === 'event') {
      const set = this.eventListeners.get(msg.name);
      set?.forEach(cb => cb(msg));
      return;
    }
    // 响应
    if (msg.id && (msg.kind === 'act-response' || msg.kind === 'query-response' || msg.kind === 'open-file-response')) {
      const req = this.pending.get(msg.id);
      if (req) {
        clearTimeout(req.timer);
        if (msg.ok !== false) req.resolve(msg);
        else req.reject(msg);
        this.pending.delete(msg.id);
      }
    }
  }

  /** 发起请求，返回 Promise */
  private request(kind: Kind, name: string, payload?: Record<string, unknown>): Promise<any> {
    const id = `req-${Date.now()}-${++this.seq}`;
    return new Promise((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${kind} ${name} 超时`));
      }, 10_000);
      this.pending.set(id, { kind, id, resolve, reject, timer: timeout });

      const msg = kind === 'open-file'
        ? { kind, id, filter: (payload as any)?.filter ?? '' }
        : { kind, id, name, payload: payload ?? {} };

      if (this.transport === 'webview') {
        (window as any).chrome.webview.postMessage(msg);
        return;
      }

      if (this.transport === 'http') {
        if (kind === 'open-file') {
          // 浏览器沙箱拿不到系统文件绝对路径（<input type=file> 只有相对路径），
          // 宿主也无法在浏览器语境里打开原生文件对话框 —— 诚实拒绝，
          // FilePanelModal 现有 catch 会把该消息展示为发送失败原因。Windows 面板不受影响。
          clearTimeout(timeout);
          this.pending.delete(id);
          reject(new Error('浏览器面板不支持系统文件对话框，请在桌面客户端中发送文件'));
          return;
        }
        // HTTP 一问一答：响应体本身就是 act-response / query-response 协议，
        // 直接喂给 handleEnvelope 做 id 关联；10s 超时 timer 保留作兑底。
        fetch(`/api/${kind}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(msg),
        })
          .then(r => {
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            return r.json();
          })
          .then(resp => this.handleEnvelope(resp))
          .catch(err => {
            const req = this.pending.get(id);
            if (req) {
              clearTimeout(req.timer);
              this.pending.delete(id);
              req.reject(err instanceof Error ? err : new Error(String(err)));
            }
          });
        return;
      }

      // mock 模式：直接回一个空响应
      window.setTimeout(() => {
        this.pending.delete(id);
        resolve({ id, ok: true, data: {} });
      }, 50);
    });
  }

  // —— 公共 API ——

  act(name: string, payload: Record<string, unknown> = {}): Promise<any> {
    return this.request('act', name, payload);
  }
  query(name: string): Promise<any> {
    return this.request('query', name);
  }
  openFile(filter: string): Promise<any> {
    return this.request('open-file', 'dummy', { filter });
  }

  subscribeState(listener: Listener): () => void {
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  }
  onEvent(name: string, cb: (data: any) => void): () => void {
    if (!this.eventListeners.has(name)) this.eventListeners.set(name, new Set());
    this.eventListeners.get(name)!.add(cb);
    return () => this.eventListeners.get(name)?.delete(cb);
  }
}

export const bridge = new Bridge();