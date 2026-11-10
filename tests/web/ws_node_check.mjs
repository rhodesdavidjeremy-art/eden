// Independent check of the gateway's WebSocket proxy using Node's standards-compliant WebSocket client
// (the same protocol implementation family browsers use). Usage: node ws_node_check.mjs <port> <cookie>
import net from 'node:net';
const [port, cookie] = [process.argv[2], process.argv[3]];

// a stand-in "VNC server" on 5900-like port is started by the caller; here we only talk to the gateway
const ws = new WebSocket(`ws://127.0.0.1:${port}/websockify`, { protocols: ['binary'], headers: { Cookie: cookie } });
ws.binaryType = 'arraybuffer';
let got = 0, expected = 0, step = 0;
const timer = setTimeout(() => { console.log('FAIL timeout'); process.exit(1); }, 15000);
ws.onerror = (e) => { console.log('FAIL error', e.message || e.type); process.exit(1); };
ws.onopen = () => console.log('open, protocol =', ws.protocol);
ws.onmessage = (ev) => {
  const bytes = new Uint8Array(ev.data);
  if (step === 0) {
    console.log('greeting:', JSON.stringify(new TextDecoder().decode(bytes)));
    step = 1; expected = 100000; const payload = new Uint8Array(expected).map((_, i) => i % 251); ws.send(payload);
    globalThis.payload = payload; return;
  }
  const want = globalThis.payload.subarray(got, got + bytes.length);
  if (bytes.length !== want.length || !bytes.every((b, i) => b === want[i])) { console.log('FAIL mismatch'); process.exit(1); }
  got += bytes.length;
  if (got === expected) { console.log('PASS echoed', got, 'bytes'); clearTimeout(timer); ws.close(1000); }
};
ws.onclose = (e) => { console.log('closed', e.code); process.exit(got === expected ? 0 : 1); };
