import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { AccountStore } from './store.mjs';
import { createServers, listen } from './http.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const flag = (name, fallback) => { const i = process.argv.indexOf(name); return i < 0 ? fallback : process.argv[i + 1]; };
const dataDir = resolve(flag('--data-dir', resolve(repo, 'Saved/OnlineServer')));
const configPath = resolve(flag('--config', resolve(dataDir, 'server.json')));
await mkdir(dataDir, { recursive: true });
const config = JSON.parse(await readFile(configPath, 'utf8'));
const catalog = JSON.parse(await readFile(resolve(config.catalog_file), 'utf8'));
async function localKey(name) {
  const path = resolve(dataDir, name);
  try { await writeFile(path, randomBytes(32).toString('base64url'), { flag: 'wx', mode: 0o600 }); }
  catch (error) { if (error.code !== 'EEXIST') throw error; }
  const key = (await readFile(path, 'utf8')).trim();
  if (!/^[A-Za-z0-9_-]{43}$/.test(key)) throw new Error(`Invalid local key file: ${name}`);
  return key;
}
const serverKey = await localKey('game-server.key'), adminKey = await localKey('admin.key');
const debugVaultKey = Buffer.from(await localKey('debug-vault.key'), 'base64url');
const store = new AccountStore({ path: resolve(dataDir, 'accounts.sqlite'), catalog, server: config.server, debugVaultKey });
const listeners = [];
for (const host of [...new Set(config.bind_addresses || ['127.0.0.1'])]) {
  // Only loopback or explicit Tailscale IPv4. Never accidentally expose all interfaces.
  const octets = host.split('.').map(Number);
  if (!(host === '127.0.0.1' || (octets.length === 4 && octets[0] === 100 && octets[1] >= 64 && octets[1] <= 127 && octets.every(n => Number.isInteger(n) && n >= 0 && n <= 255))))
    throw new Error('API bind address must be loopback or an explicit Tailscale IPv4 address');
  const { api } = createServers({ store, serverKey, adminKey, onError: e => console.error(JSON.stringify(e)) });
  await listen(api, config.api_port || 17980, host); listeners.push(api);
}
const { admin } = createServers({ store, serverKey, adminKey, onError: e => console.error(JSON.stringify(e)) });
await listen(admin, config.admin_port || 17981, '127.0.0.1'); listeners.push(admin);
const running = { pid: process.pid, realm_id: store.realmId, api_port: config.api_port || 17980,
  bind_addresses: config.bind_addresses || ['127.0.0.1'], admin_url: `http://127.0.0.1:${config.admin_port || 17981}`, started_at: new Date().toISOString() };
await writeFile(resolve(dataDir, 'running.json'), JSON.stringify(running, null, 2));
console.log(JSON.stringify({ event: 'ready', ...running }));
let stopping = false;
async function stop() {
  if (stopping) return; stopping = true;
  for (const listener of listeners) listener.closeAllConnections();
  await Promise.all(listeners.map(listener => new Promise(done => listener.close(done))));
  store.close(); process.exit(0);
}
process.on('SIGINT', stop); process.on('SIGTERM', stop);
