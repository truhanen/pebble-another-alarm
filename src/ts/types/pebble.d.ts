// Ambient declaration for the PebbleKit JS global available in the phone-side
// JS runtime. Only the surface this watchface actually uses is declared.

interface PebbleAppMessageEvent {
  payload: Record<string, unknown>;
}

interface PebbleWebviewClosedEvent {
  response: string;
}

interface PebbleReadyEvent {
  ready: boolean;
}

interface Pebble {
  addEventListener(type: 'ready', cb: (e: PebbleReadyEvent) => void): void;
  addEventListener(type: 'appmessage', cb: (e: PebbleAppMessageEvent) => void): void;
  addEventListener(type: 'showConfiguration', cb: (e: Event) => void): void;
  addEventListener(type: 'webviewclosed', cb: (e: PebbleWebviewClosedEvent) => void): void;
  addEventListener(type: string, cb: (e: any) => void): void;

  openURL(url: string): void;

  sendAppMessage(
    message: Record<string, unknown>,
    onSuccess?: (e: unknown) => void,
    onError?: (e: unknown) => void,
  ): void;
}

declare const Pebble: Pebble;
