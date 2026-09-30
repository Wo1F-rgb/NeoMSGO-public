// Local visual verification against the real application with an isolated disposable DB.
import { mkdtemp, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AccountStore } from '../src/store.mjs';
import { createServers, listen } from '../src/http.mjs';
const dir = await mkdtemp(join(tmpdir(), 'neomsgo-admin-ui-'));
const catalog = JSON.parse(await readFile(new URL('../../Saved/OnlineServer/catalog.json', import.meta.url), 'utf8'));
const store = new AccountStore({ path: join(dir, 'accounts.sqlite'), catalog, debugVaultKey: Buffer.alloc(32, 9),
  server: { server_id: 'ui-fixture', name: '管理画面の動作確認用', game_address: '127.0.0.1:17777', max_players: 12 } });
await store.register({ username: 'Sample_Pilot', password: 'fixture-only-password-2026' });
store.guest();
await store.createTestAccount({ username: 'debug_preview' });
const { admin } = createServers({ store, serverKey: 'fixture-only-server-key', adminKey: 'fixture-only-admin-key' });
await listen(admin, 17891, '127.0.0.1');
console.log(JSON.stringify({ admin: 'http://127.0.0.1:17891', purpose: 'isolated visual verification', pid: process.pid }));
