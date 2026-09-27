// The ARM-DOS Online line protocol (apps/online/README.md has the full description).
// After CONNECT both sides exchange frames:
//
//   STX(02h)  type  len-lo  len-hi  payload[len]  sum
//
// sum = (type + len-lo + len-hi + every payload byte) & FFh. Anything outside a frame is
// ignored (so modem result codes and the service's text banner are harmless), and a bad
// sum makes the receiver hunt for the next STX. The line is 8-bit clean (the modem
// has no flow control characters; "+++" needs a guard time, which a frame never has).
//
// Client -> service                       Service -> client
//   H name\0password\0version  sign on       W name\0text          welcome
//   S query                    search        D id2 kind title\0 channel\0   document begins
//   A title                    article       T bytes               document text (doc.mjs)
//   F id2 link2                follow a link E lines2 flags        document ends
//   D word                     dictionary    G name\0caption\0 w2 h2 size4   picture begins
//   W place                    weather       B bytes               picture data (GIF)
//   N                          news          F                     picture ends
//   T mmdd                     on this day   R class text          E(rror)/I(nfo) message
//   R                          random        S text                status ("Searching...")
//   X                          cancel        Z                     cancel acknowledged
//   Q                          sign off      Q text                goodbye (then the service hangs up)

export const STX = 0x02;
export const MAX_PAYLOAD = 1024;

/** type (char) + payload (Uint8Array | number[] | binary string) -> frame bytes. */
export function frame(type, payload = []) {
  const p = typeof payload === 'string' ? Array.from(payload, (c) => c.charCodeAt(0) & 0xFF) : Array.from(payload);
  const t = type.charCodeAt(0);
  const out = new Uint8Array(p.length + 5);
  out[0] = STX; out[1] = t; out[2] = p.length & 0xFF; out[3] = p.length >> 8;
  let sum = t + out[2] + out[3];
  for (let i = 0; i < p.length; i++) { out[4 + i] = p[i]; sum += p[i]; }
  out[4 + p.length] = sum & 0xFF;
  return out;
}

/** Streaming frame parser: feed(bytes) calls onFrame(type, payloadUint8Array). */
export class FrameReader {
  constructor(onFrame, { onJunk = null } = {}) { this.onFrame = onFrame; this.onJunk = onJunk; this.st = 0; this.buf = []; this.errors = 0; }
  feed(bytes) {
    for (const b of bytes) {
      switch (this.st) {
        case 0: if (b === STX) { this.st = 1; this.buf = []; } else this.onJunk?.(b); break;
        case 1: this.type = b; this.sum = b; this.st = 2; break;
        case 2: this.len = b; this.sum += b; this.st = 3; break;
        case 3: this.len |= b << 8; this.sum += b; this.st = this.len > MAX_PAYLOAD ? (this.errors++, 0) : this.len ? 4 : 5; break;
        case 4: this.buf.push(b); this.sum += b; if (this.buf.length === this.len) this.st = 5; break;
        case 5:
          this.st = 0;
          if ((this.sum & 0xFF) === b) this.onFrame(String.fromCharCode(this.type), Uint8Array.from(this.buf));
          else this.errors++;
          break;
      }
    }
  }
}

export const bin = (u8) => String.fromCharCode(...u8);
export const u16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
export const u32 = (v) => [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >>> 24) & 0xFF];
