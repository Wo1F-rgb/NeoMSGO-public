import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { ApiError, digest } from './store.mjs';

const LIMIT = 32768;
const reject = (status, code, message) => { throw new ApiError(status, code, message); };
const bearer = req => /^Bearer ([A-Za-z0-9_-]{32,128})$/.exec(req.headers.authorization || '')?.[1];
function keyMatches(given, expected) {
  return typeof given === 'string' && given.length <= 256 &&
    timingSafeEqual(Buffer.from(digest(given), 'hex'), Buffer.from(digest(expected), 'hex'));
}
function send(res, status, object, extra = {}) {
  const body = JSON.stringify(object);
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...extra });
  res.end(body);
}
async function body(req) {
  if (!(req.headers['content-type'] || '').toLowerCase().startsWith('application/json')) reject(415, 'json_required', 'JSON形式で送信してください。');
  if (Number(req.headers['content-length']) > LIMIT) reject(413, 'body_too_large', '送信内容が大きすぎます。');
  let size = 0; const chunks = [];
  for await (const chunk of req) {
    size += chunk.length;
    if (size > LIMIT) reject(413, 'body_too_large', '送信内容が大きすぎます。');
    chunks.push(chunk);
  }
  let result;
  try { result = JSON.parse(Buffer.concat(chunks).toString('utf8')); }
  catch { reject(400, 'invalid_json', 'JSONを読み取れません。'); }
  if (!result || Array.isArray(result) || typeof result !== 'object') reject(400, 'invalid_json', 'JSONオブジェクトが必要です。');
  return result;
}
function guard(handler, onError) {
  return async (req, res) => {
    try { await handler(req, res); }
    catch (error) {
      if (res.writableEnded || res.destroyed) return;
      if (error instanceof ApiError) send(res, error.status, { error: { code: error.code, message: error.message }, ...error.detail });
      else {
        // Do not log request URLs, bodies, headers, passwords, or tokens.
        onError?.({ event: 'request_failed', kind: error.name });
        send(res, 500, { error: { code: 'internal_error', message: 'サーバーで処理できませんでした。保存内容は再読込して確認してください。' } });
      }
    }
  };
}
function makeServer(handler) {
  const server = http.createServer({ maxHeaderSize: 8192, requestTimeout: 15000, headersTimeout: 10000, keepAliveTimeout: 3000 }, handler);
  server.maxConnections = 64;
  server.on('clientError', (_error, socket) => socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n'));
  return server;
}
export function createServers({ store, serverKey, adminKey, onError, rateLimit = true }) {
  const attempts = new Map(), adminSessions = new Map();
  function throttle(req, category, maximum, window) {
    if (!rateLimit) return;
    const now = store.now(), key = `${req.socket.remoteAddress}:${category}`;
    // Tailscale/loopback address comes from the socket; never trust forwarded IP headers.
    for (const [id, item] of attempts) if (item.until < now) attempts.delete(id);
    const item = attempts.get(key) || { until: now + window, count: 0 };
    item.count++; attempts.set(key, item);
    if (item.count > maximum) reject(429, 'rate_limited', '操作が多すぎます。少し待って再試行してください。');
  }
  const api = makeServer(guard(async (req, res) => {
    // The game API is used by UE clients, not cross-origin websites.
    if (req.headers.origin) reject(403, 'origin_denied', 'この接続元からは操作できません。');
    const path = new URL(req.url, 'http://localhost').pathname;
    const route = `${req.method} ${path}`;
    throttle(req, 'all', 300, 60000);
    if (route === 'GET /health') return send(res, 200, { ok: true, realm_id: store.realmId, catalog_hash: store.catalog.catalog_hash, protocol: 1 });
    if (route === 'GET /v1/servers') return send(res, 200, store.servers());
    if (route === 'GET /v1/rooms') return send(res, 200, store.rooms());
    if (route === 'GET /v1/catalog') return send(res, 200, store.catalog);
    if (route === 'GET /v1/me') return send(res, 200, store.publicAccount(store.authenticated(bearer(req))));
    if (route === 'GET /v1/me/battle-assist') return send(res, 200, store.getBattleAssistProgress(bearer(req)));
    if (route === 'GET /v1/me/deck') return send(res, 200, store.getDeck(bearer(req)));
    if (route === 'POST /v1/rooms') {
      throttle(req, 'room-create', 20, 60000);
      return send(res, 200, store.createRoom(bearer(req), await body(req), req.socket.remoteAddress));
    }
    const roomRoute = /^\/v1\/rooms\/([^/]+)\/(heartbeat|close|tickets|consume-ticket)$/.exec(path);
    if (roomRoute) {
      if (req.method !== 'POST') reject(404, 'not_found', '操作が見つかりません。');
      const roomId = roomRoute[1], operation = roomRoute[2];
      const hasRequestBody = req.headers['transfer-encoding'] || Number(req.headers['content-length']) > 0;
      const data = operation === 'close' && !hasRequestBody
        ? {} : await body(req);
      if (operation === 'tickets') return send(res, 200, store.issueRoomTicket(bearer(req), roomId, data));
      const lease = req.headers['x-ms-room-lease'];
      if (operation === 'heartbeat') return send(res, 200, store.roomHeartbeat(roomId, lease, data));
      if (operation === 'close') return send(res, 200, store.closeRoom(roomId, lease, data));
      return send(res, 200, store.consumeRoomTicket(roomId, lease, data));
    }
    if (path.startsWith('/v1/server/')) {
      if (!keyMatches(req.headers['x-neomsgo-server-key'], serverKey)) reject(401, 'server_unauthorized', 'ゲームサーバーの認証が必要です。');
      const data = await body(req);
      if (route === 'POST /v1/server/heartbeat') return send(res, 200, store.heartbeat(data));
      if (route === 'POST /v1/server/tickets/consume') return send(res, 200, store.consumeTicket(data));
      reject(404, 'not_found', '操作が見つかりません。');
    }
    if (path.startsWith('/v1/auth/')) throttle(req, 'auth', 20, 60000);
    if (route === 'POST /v1/auth/guest') throttle(req, 'guest', 12, 3600000);
    if (route === 'POST /v1/auth/logout') return send(res, 200, store.logout(bearer(req)));
    if (!['POST', 'PUT'].includes(req.method)) reject(404, 'not_found', '操作が見つかりません。');
    const data = await body(req);
    let result;
    switch (route) {
      case 'POST /v1/auth/register': result = await store.register(data); break;
      case 'POST /v1/auth/login': result = await store.login(data); break;
      case 'POST /v1/auth/guest': result = store.guest(); break;
      case 'POST /v1/auth/resume': result = store.resume(data); break;
      case 'POST /v1/auth/upgrade': result = await store.upgrade(bearer(req), data); break;
      case 'POST /v1/me/battle-assist/develop': result = store.developBattleAssist(bearer(req), data); break;
      case 'PUT /v1/me/deck': result = store.saveDeck(bearer(req), data); break;
      case 'POST /v1/game/tickets': result = store.issueTicket(bearer(req), data); break;
      default: reject(404, 'not_found', '操作が見つかりません。');
    }
    send(res, 200, result);
  }, onError));

  // Separate listener: admin routes never exist on the Tailscale game API port.
  const admin = makeServer(guard(async (req, res) => {
    const host = req.headers.host;
    if (!host || !/^(127\.0\.0\.1|localhost):\d+$/.test(host)) reject(403, 'invalid_host', '管理画面はこのサーバーPCから開いてください。');
    if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress)) reject(403, 'local_only', '管理画面はローカル専用です。');
    const path = new URL(req.url, 'http://localhost').pathname;
    if (req.method === 'GET' && ['/', '/admin.js', '/admin.css'].includes(path)) {
      const file = path === '/' ? 'index.html' : path.slice(1);
      const data = await readFile(new URL(`../public/${file}`, import.meta.url));
      const type = file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html';
      res.writeHead(200, { 'Content-Type': `${type}; charset=utf-8`, 'Content-Length': data.length,
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
        'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'" });
      return res.end(data);
    }
    if (!['GET', 'HEAD'].includes(req.method) && req.headers.origin !== `http://${host}`) reject(403, 'origin_denied', '管理画面から操作してください。');
    if (req.method === 'POST' && path === '/admin/login') {
      throttle(req, 'admin-login', 10, 60000);
      const data = await body(req);
      if (!keyMatches(data.key, adminKey)) reject(401, 'invalid_admin_key', '管理キーが違います。');
      const token = randomBytes(32).toString('base64url');
      adminSessions.set(digest(token), store.now() + 3600000);
      return send(res, 200, { ok: true }, { 'Set-Cookie': `msgo_admin=${token}; HttpOnly; SameSite=Strict; Path=/admin; Max-Age=3600` });
    }
    const cookie = /(?:^|;\s*)msgo_admin=([A-Za-z0-9_-]+)/.exec(req.headers.cookie || '')?.[1];
    const session = cookie && digest(cookie);
    if (!session || (adminSessions.get(session) || 0) <= store.now()) reject(401, 'admin_login_required', '管理キーでログインしてください。');
    if (req.method === 'GET' && path === '/admin/accounts') return send(res, 200, { realm_id: store.realmId, accounts: store.listAccounts(), server: store.servers().servers[0] });
    if (req.method === 'GET' && path === '/admin/catalog') return send(res, 200, store.catalog);
    if (req.method === 'GET' && path === '/admin/test-accounts') return send(res, 200, { accounts: store.listTestCredentials() });
    if (req.method === 'POST' && path === '/admin/test-accounts') return send(res, 200, await store.createTestAccount(await body(req)));
    if (req.method === 'GET' && path === '/admin/audit') return send(res, 200, { events: store.db.prepare('SELECT * FROM audit ORDER BY id DESC LIMIT 100').all() });
    if (req.method === 'POST' && path === '/admin/logout') {
      adminSessions.delete(session);
      return send(res, 200, { ok: true }, { 'Set-Cookie': 'msgo_admin=; HttpOnly; SameSite=Strict; Path=/admin; Max-Age=0' });
    }
    const match = /^\/admin\/accounts\/([0-9a-f-]{36})\/(reset-password|disable|recover-guest|grant-battle-assist)$/.exec(path);
    if (req.method === 'POST' && match) {
      const data = await body(req);
      if (match[2] === 'grant-battle-assist') return send(res, 200, store.grantBattleAssistResources(match[1], data));
      if (match[2] === 'reset-password') return send(res, 200, await store.resetPassword(match[1]));
      if (match[2] === 'disable') return send(res, 200, store.setDisabled(match[1], data.disabled));
      return send(res, 200, store.recoverGuest(match[1], data));
    }
    reject(404, 'not_found', '管理操作が見つかりません。');
  }, onError));
  return { api, admin };
}

export async function listen(server, port, host) {
  return new Promise((resolve, rejectPromise) => {
    server.once('error', rejectPromise);
    server.listen(port, host, () => { server.off('error', rejectPromise); resolve(server.address()); });
  });
}
