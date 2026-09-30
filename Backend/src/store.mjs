import { emptyProgress, emptyAssist, exactObject, validateProgress, validateAssist, validModuleDefinition } from './battle-assist.mjs';
import { DatabaseSync } from 'node:sqlite';
import { randomBytes, randomUUID, createHash, createCipheriv, createDecipheriv, scrypt, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';

const derive = promisify(scrypt);
const SCRYPT = Object.freeze({ N: 131072, r: 8, p: 1, maxmem: 256 * 1024 * 1024 });
const secret = () => randomBytes(32).toString('base64url');
const ROOM_BUILD_ID = 'neomsgo-rooms-5';
// Archived NewYark stays valid for restored legacy rooms; playable prototypes use their own IDs.
const ROOM_MAP_IDS = Object.freeze(['UnrealizerColony', 'Daibao', 'NewYark', 'NewYorkTaigan', 'TaklamakanRuinsPrototype']);
const ROOM_STATES = Object.freeze(['Starting', 'Ready', 'Playing']);
const ROOM_LEGACY_MAX_PLAYERS = 12;
const ROOM_FILLED_MAX_PLAYERS = 50;
const ROOM_HEARTBEAT_VISIBLE_MS = 20000;
const ROOM_STARTING_LEASE_MS = 120000;
const ROOM_ACTIVE_LEASE_MS = 20000;
const ROOM_TICKET_MS = 60000;
const ROOM_STATE_NAMES = Object.freeze({ starting: 'Starting', ready: 'Ready', playing: 'Playing' });

function roomObject(body) {
  if (!body || Array.isArray(body) || typeof body !== 'object') fail(400, 'invalid_room', 'ルーム情報はJSONオブジェクトで送信してください。');
  return body;
}

function roomName(value) {
  if (typeof value !== 'string') fail(422, 'invalid_room_name', 'ルーム名を入力してください。');
  const normalized = value.trim();
  if (!normalized || Array.from(normalized).length > 64 || /[\u0000-\u001f\u007f]/u.test(normalized))
    fail(422, 'invalid_room_name', 'ルーム名は制御文字を含まない1〜64文字で入力してください。');
  return normalized;
}

function roomEnum(value, field) {
  const maximum = field === 'difficulty' ? 3 : 2;
  if (!Number.isInteger(value) || value < 0 || value > maximum)
    fail(422, `invalid_${field}`, `${field}は0〜${maximum}の整数で指定してください。`);
  return value;
}

function roomState(value) {
  if (typeof value !== 'string') fail(422, 'invalid_room_state', 'ルーム状態が不正です。');
  const normalized = ROOM_STATE_NAMES[value.toLowerCase()];
  if (!normalized) fail(422, 'invalid_room_state', 'ルーム状態が不正です。');
  return normalized;
}

function roomPort(value) {
  if (!Number.isInteger(value) || value < 1024 || value > 65535)
    fail(422, 'invalid_game_port', 'ゲームポートは1024〜65535の整数で指定してください。');
  return value;
}

function roomId(value) {
  if (typeof value !== 'string' || !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value))
    fail(404, 'room_not_found', 'ルームが見つかりません。');
  return value.toLowerCase();
}

function connectionId(value) {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(value))
    fail(401, 'invalid_ticket', '接続IDが不正です。');
  return value;
}

function sourceIPv4(value) {
  let address = typeof value === 'string' ? value : '';
  if (address === '::1') address = '127.0.0.1';
  if (address.startsWith('::ffff:')) address = address.slice(7);
  const octets = address.split('.').map(Number);
  const valid = octets.length === 4 && octets.every((part, index) =>
    Number.isInteger(part) && part >= 0 && part <= 255 && String(part) === address.split('.')[index]);
  if (!valid) fail(403, 'invalid_source_address', 'ルームの接続先を確定できない接続元です。');
  const loopback = octets[0] === 127;
  const tailscale = octets[0] === 100 && octets[1] >= 64 && octets[1] <= 127;
  if (!loopback && !tailscale) fail(403, 'source_address_not_allowed', 'ルーム作成はこのネットワークから利用できません。');
  return address;
}
export const digest = value => createHash('sha256').update(value).digest('hex');
export class ApiError extends Error {
  constructor(status, code, message, detail = {}) { super(message); Object.assign(this, { status, code, detail }); }
}
const fail = (status, code, message, detail) => { throw new ApiError(status, code, message, detail); };
function username(value) {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_\-.]{3,32}$/.test(value))
    fail(422, 'invalid_username', 'ログイン名は半角英数字・_・-・. の3〜32文字で入力してください。');
  return value.toLowerCase();
}
function password(value) {
  if (typeof value !== 'string' || value.length < 10 || value.length > 128)
    fail(422, 'invalid_password', 'パスワードは10〜128文字で入力してください。');
  return value;
}
export async function passwordHash(value) {
  password(value);
  const salt = randomBytes(16);
  const hash = await derive(value, salt, 32, SCRYPT);
  return `scrypt$131072$8$1$${salt.toString('hex')}$${hash.toString('hex')}`;
}
async function passwordMatches(value, encoded) {
  // Unknown users use the same KDF; account existence is not exposed by the response.
  const fields = (encoded || 'scrypt$131072$8$1$00000000000000000000000000000000$' + '00'.repeat(32)).split('$');
  if (fields.length !== 6 || fields.slice(0, 4).join('$') !== 'scrypt$131072$8$1') return false;
  const hash = await derive(value, Buffer.from(fields[4], 'hex'), 32, SCRYPT);
  const wanted = Buffer.from(fields[5], 'hex');
  return wanted.length === hash.length && timingSafeEqual(wanted, hash) && Boolean(encoded);
}

export class AccountStore {
  constructor({ path, catalog, server, debugVaultKey, now = () => Date.now(), sessionLifetimeMs = 24 * 3600000, ticketLifetimeMs = 60000,
    roomLeaseLifetimeMs = ROOM_ACTIVE_LEASE_MS, startingRoomLeaseLifetimeMs = ROOM_STARTING_LEASE_MS, roomTicketLifetimeMs = ROOM_TICKET_MS }) {
    this.db = new DatabaseSync(path, { timeout: 5000 });
    const version = this.db.prepare('PRAGMA user_version').get().user_version;
    if (version > 5) { this.db.close(); throw new Error('Account database was created by a newer server'); }
    this.now = now; this.sessionLifetimeMs = sessionLifetimeMs; this.ticketLifetimeMs = ticketLifetimeMs;
    this.roomLeaseLifetimeMs = roomLeaseLifetimeMs; this.startingRoomLeaseLifetimeMs = startingRoomLeaseLifetimeMs;
    this.roomTicketLifetimeMs = roomTicketLifetimeMs;
    this.roomBuildId = ROOM_BUILD_ID;
    this.debugVaultKey = debugVaultKey;
    if (debugVaultKey && debugVaultKey.length !== 32) throw new Error('Debug vault key must contain 32 bytes');
    this.catalog = catalog; this.server = server; this.kdfActive = 0;
    if (![1, 2, 3].includes(catalog.schema_version) || !catalog.catalog_hash || !Array.isArray(catalog.suits) || !Array.isArray(catalog.weapons))
      throw new Error('A valid exported game catalog is required');
    this.suits = new Map(catalog.suits.map(x => [x.id, x]));
    this.weapons = new Map(catalog.weapons.map(x => [x.id, x]));
    if (this.suits.size !== catalog.suits.length || this.weapons.size !== catalog.weapons.length) throw new Error('Duplicate catalog IDs');
    this.modules = new Map((catalog.modules ?? []).map(x => [x.id, x]));
    this.moduleProfiles = new Map((catalog.suit_profiles ?? []).map(x => [x.suit_id, x]));
    if (this.modules.size !== (catalog.modules ?? []).length || this.moduleProfiles.size !== (catalog.suit_profiles ?? []).length)
      throw new Error('Duplicate module/profile catalog IDs');
    if (catalog.schema_version === 3 && (!Array.isArray(catalog.modules) || !Array.isArray(catalog.suit_profiles) || !Array.isArray(catalog.time_rules)))
      throw new Error('Battle assist catalogue rules are required');
    this.defaultDeck = this.validateDeck(catalog.default_deck);
    this.db.exec(`PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;
      CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS accounts(
        id TEXT PRIMARY KEY, username TEXT UNIQUE NOT NULL, display_name TEXT NOT NULL,
        password_hash TEXT, is_guest INTEGER NOT NULL, disabled INTEGER NOT NULL DEFAULT 0, is_test INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL, last_login_at INTEGER NOT NULL, revision INTEGER NOT NULL DEFAULT 0,
        deck_json TEXT NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS battle_assist_progress(
        account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
        progress_json TEXT NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS sessions(
        hash TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        expires_at INTEGER NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS guest_credentials(
        hash TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        created_at INTEGER NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS tickets(
        hash TEXT PRIMARY KEY, session_hash TEXT NOT NULL REFERENCES sessions(hash) ON DELETE CASCADE,
        account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, server_id TEXT NOT NULL,
        expires_at INTEGER NOT NULL, connection_id TEXT, snapshot_json TEXT NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS rooms(
        room_id TEXT PRIMARY KEY, instance_id TEXT NOT NULL UNIQUE,
        owner_account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        idempotency_key TEXT NOT NULL, name TEXT NOT NULL, host_name TEXT NOT NULL,
        source_address TEXT NOT NULL, game_address TEXT NOT NULL,
        map_id TEXT NOT NULL, rule_id INTEGER NOT NULL, bot_count INTEGER NOT NULL,
        fill_teams_with_bots INTEGER NOT NULL DEFAULT 0,
        difficulty INTEGER NOT NULL, build_id TEXT NOT NULL, catalog_hash TEXT NOT NULL,
        game_port INTEGER NOT NULL, state TEXT NOT NULL,
        human_players INTEGER NOT NULL DEFAULT 1, bot_players INTEGER NOT NULL,
        max_players INTEGER NOT NULL DEFAULT 12, created_at INTEGER NOT NULL,
        last_heartbeat_at INTEGER NOT NULL, lease_expires_at INTEGER NOT NULL,
        lease_hash TEXT NOT NULL, lease_cipher TEXT NOT NULL,
        owner_ticket_consumed INTEGER NOT NULL DEFAULT 0,
        UNIQUE(owner_account_id, idempotency_key)) STRICT;
      CREATE TABLE IF NOT EXISTS room_tickets(
        hash TEXT PRIMARY KEY, room_id TEXT NOT NULL REFERENCES rooms(room_id) ON DELETE CASCADE,
        instance_id TEXT NOT NULL, session_hash TEXT NOT NULL REFERENCES sessions(hash) ON DELETE CASCADE,
        account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        deck_revision INTEGER NOT NULL, catalog_hash TEXT NOT NULL, build_id TEXT NOT NULL,
        expires_at INTEGER NOT NULL, connection_id TEXT, snapshot_json TEXT NOT NULL) STRICT;
      CREATE INDEX IF NOT EXISTS session_account ON sessions(account_id);
      CREATE INDEX IF NOT EXISTS ticket_expiry ON tickets(expires_at);
      CREATE INDEX IF NOT EXISTS room_ticket_expiry ON room_tickets(expires_at);
      CREATE INDEX IF NOT EXISTS room_visibility ON rooms(state, last_heartbeat_at);
      CREATE TABLE IF NOT EXISTS audit(
        id INTEGER PRIMARY KEY, at INTEGER NOT NULL, action TEXT NOT NULL, account_id TEXT, detail TEXT NOT NULL) STRICT;
      CREATE TABLE IF NOT EXISTS test_credentials(
        account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE, sealed TEXT NOT NULL) STRICT;`);
    if (!this.db.prepare('PRAGMA table_info(accounts)').all().some(column => column.name === 'is_test'))
      this.db.exec('ALTER TABLE accounts ADD COLUMN is_test INTEGER NOT NULL DEFAULT 0');
    if (!this.db.prepare('PRAGMA table_info(rooms)').all().some(column => column.name === 'owner_ticket_consumed'))
      this.db.exec('ALTER TABLE rooms ADD COLUMN owner_ticket_consumed INTEGER NOT NULL DEFAULT 0');
    if (!this.db.prepare('PRAGMA table_info(rooms)').all().some(column => column.name === 'fill_teams_with_bots'))
      this.db.exec('ALTER TABLE rooms ADD COLUMN fill_teams_with_bots INTEGER NOT NULL DEFAULT 0');
    this.db.exec('PRAGMA user_version=5');
    this.db.prepare('INSERT OR IGNORE INTO metadata VALUES (?,?)').run('realm_id', randomUUID());
    this.realmId = this.db.prepare('SELECT value FROM metadata WHERE key=?').get('realm_id').value;
    this.db.prepare('INSERT OR IGNORE INTO metadata VALUES (?,?)').run('room_lease_key', randomBytes(32).toString('base64url'));
    const leaseKey = this.db.prepare('SELECT value FROM metadata WHERE key=?').get('room_lease_key').value;
    this.roomLeaseKey = Buffer.from(leaseKey, 'base64url');
    if (this.roomLeaseKey.length !== 32) { this.db.close(); throw new Error('Room lease key is invalid'); }
    this.lastHeartbeat = null;
  }
  close() { this.db.close(); }
  transaction(fn) {
    this.db.exec('BEGIN IMMEDIATE');
    try { const result = fn(); this.db.exec('COMMIT'); return result; }
    catch (e) { this.db.exec('ROLLBACK'); throw e; }
  }
  audit(action, id, detail = {}) {
    this.db.prepare('INSERT INTO audit(at,action,account_id,detail) VALUES (?,?,?,?)').run(this.now(), action, id, JSON.stringify(detail));
  }
  async kdf(fn) {
    if (this.kdfActive >= 2) fail(429, 'busy', 'ログイン処理が混み合っています。少し待って再試行してください。');
    this.kdfActive++;
    try { return await fn(); } finally { this.kdfActive--; }
  }
  validateDeck(input, progress = emptyProgress()) {
    exactObject(input, ['schema_version', 'revision', 'selected_slot', 'slots'], fail, 'invalid_deck');
    if ('revision' in input && (!Number.isSafeInteger(input.revision) || input.revision < 0)) fail(422, 'invalid_revision', '版番号が不正です。');
    if (!input || ![1, 2, 3].includes(input.schema_version) || !Number.isInteger(input.selected_slot) || input.selected_slot < 0 || input.selected_slot > 3 || !Array.isArray(input.slots) || input.slots.length !== 4)
      fail(422, 'invalid_deck', '編成は4機体枠と選択枠0〜3が必要です。');
    const schema = input.schema_version;
    let totalCost = 0;
    const slots = input.slots.map((slot, index) => {
      exactObject(slot, ['suit_id', 'weapon_ids', ...(schema >= 2 ? ['shield_equipped'] : []), ...(schema === 3 ? ['battleAssist'] : [])], fail, 'invalid_deck_slot');
      const assist = schema === 3 ? slot.battleAssist : emptyAssist();
      if (!slot || !Array.isArray(slot.weapon_ids) || slot.weapon_ids.length !== 3)
        fail(422, 'invalid_weapon_slots', `機体${index + 1}の武器枠は3個必要です。`);
      if (schema >= 2 && typeof slot.shield_equipped !== 'boolean')
        fail(422, 'invalid_shield_equipped', `機体${index + 1}のshield_equippedが不正です。`);
      let shieldEquipped = schema >= 2 ? slot.shield_equipped : false;
      if (slot.suit_id === null) {
        if (slot.weapon_ids.some(x => x !== null)) fail(422, 'empty_suit_with_weapon', `空の機体枠${index + 1}に武器は保存できません。`);
        if (shieldEquipped) fail(422, 'empty_suit_with_shield', `空の機体枠${index + 1}にシールドは保存できません。`);
        return { suit_id: null, weapon_ids: [null, null, null], shield_equipped: false, battleAssist: validateAssist(assist, null, this.catalog, this.modules, this.moduleProfiles, progress, fail) };
      }
      const suit = this.suits.get(slot.suit_id);
      if (!suit) fail(422, 'unknown_suit', `機体${index + 1}はこのサーバーのカタログにありません。`);
      totalCost += suit.cost;
      const seen = new Set();
      let shieldCount = 0;
      const weapons = slot.weapon_ids.map(id => {
        if (id === null) return null;
        const weapon = this.weapons.get(id);
        if (!weapon) fail(422, 'unknown_weapon', `機体${index + 1}に未登録の武器があります。`);
        if (weapon.is_shield) {
          if (schema !== 1) fail(422, 'shield_in_weapon_slot', `機体${index + 1}のシールドは独立装備です。`);
          if (++shieldCount > 1 || !suit.native_shield_id || suit.native_shield_id !== id)
            fail(422, 'legacy_shield_mismatch', `機体${index + 1}のlegacy shieldがnative shieldと一致しません。`);
          shieldEquipped = true;
          return null;
        }
        if (seen.has(id)) fail(422, 'duplicate_weapon', `機体${index + 1}の同じ武器を複数枠に装備できません。`);
        seen.add(id); return id;
      });
      if (schema >= 2) {
        if (shieldEquipped && !suit.native_shield_id) fail(422, 'missing_native_shield', `機体${index + 1}にnative shieldがありません。`);
        if (!shieldEquipped && suit.shield_removable === false) fail(422, 'shield_required', `機体${index + 1}のシールドは外せません。`);
      } else if (!shieldEquipped && suit.shield_removable === false) {
        fail(422, 'shield_required', `機体${index + 1}のlegacy編成に必須シールドがありません。`);
      }
      return { suit_id: suit.id, weapon_ids: weapons, shield_equipped: shieldEquipped, battleAssist: validateAssist(assist, suit, this.catalog, this.modules, this.moduleProfiles, progress, fail) };
    });
    if (totalCost > (this.catalog.deck_cost_limit ?? 1720)) fail(422, 'deck_cost_exceeded', 'デッキコストが上限を超えています。');
    // The existing game permits over-capacity loadouts and displays a warning.
    // Authentication must not silently invent a new capacity restriction.
    return { schema_version: 3, selected_slot: input.selected_slot, slots };
  }
  publicAccount(row) {
    const deck = this.validateDeck(JSON.parse(row.deck_json), this.progressForAccount(row.id));
    return { realm_id: this.realmId, account_id: row.id, username: row.username, display_name: row.display_name,
      is_guest: Boolean(row.is_guest), catalog_hash: this.catalog.catalog_hash,
      deck: { ...deck, revision: row.revision } };
  }
  newSession(row, resumeToken) {
    const token = secret(), expires_at = this.now() + this.sessionLifetimeMs;
    this.db.prepare('INSERT INTO sessions VALUES (?,?,?)').run(digest(token), row.id, expires_at);
    this.db.prepare('UPDATE accounts SET last_login_at=? WHERE id=?').run(this.now(), row.id);
    return { ...this.publicAccount(row), token, expires_at, ...(resumeToken ? { guest_resume_token: resumeToken } : {}) };
  }
  async register(body) {
    const name = username(body.username);
    const hash = await this.kdf(() => passwordHash(body.password));
    return this.transaction(() => {
      if (this.db.prepare('SELECT id FROM accounts WHERE username=?').get(name)) fail(409, 'username_taken', 'そのログイン名は使用されています。');
      const id = randomUUID(), now = this.now();
      this.db.prepare('INSERT INTO accounts(id,username,display_name,password_hash,is_guest,created_at,last_login_at,deck_json) VALUES (?,?,?,?,0,?,?,?)')
        .run(id, name, body.username, hash, now, now, JSON.stringify(this.defaultDeck));
      this.audit('register', id);
      return this.newSession(this.db.prepare('SELECT * FROM accounts WHERE id=?').get(id));
    });
  }
  async login(body) {
    const name = username(body.username);
    if (typeof body.password !== 'string' || body.password.length > 128) fail(401, 'invalid_credentials', 'ログイン名またはパスワードが違います。');
    const row = this.db.prepare('SELECT * FROM accounts WHERE username=?').get(name);
    const valid = await this.kdf(() => passwordMatches(body.password, row?.password_hash));
    if (!valid || row.disabled || row.is_guest) fail(401, 'invalid_credentials', 'ログイン名またはパスワードが違います。');
    // Re-read after async hashing: a concurrent admin reset/disable must win.
    const fresh = this.db.prepare('SELECT * FROM accounts WHERE id=?').get(row.id);
    if (!fresh || fresh.disabled || fresh.password_hash !== row.password_hash) fail(401, 'invalid_credentials', '認証情報が変更されました。再試行してください。');
    this.audit('login', row.id);
    return this.newSession(fresh);
  }
  guest() {
    return this.transaction(() => {
      const id = randomUUID(), now = this.now(), resume = secret();
      const number = this.db.prepare('SELECT COUNT(*) AS n FROM accounts').get().n + 1;
      this.db.prepare('INSERT INTO accounts(id,username,display_name,password_hash,is_guest,created_at,last_login_at,deck_json) VALUES (?,?,?,NULL,1,?,?,?)')
        .run(id, `guest_${id}`, `ゲスト${number}`, now, now, JSON.stringify(this.defaultDeck));
      this.db.prepare('INSERT INTO guest_credentials VALUES (?,?,?)').run(digest(resume), id, now);
      this.audit('guest_create', id);
      return this.newSession(this.db.prepare('SELECT * FROM accounts WHERE id=?').get(id), resume);
    });
  }
  resume(body) {
    if (typeof body.resume_token !== 'string' || body.resume_token.length > 128) fail(401, 'invalid_guest', 'ゲストを復元できません。');
    const row = this.db.prepare('SELECT a.* FROM accounts a JOIN guest_credentials g ON g.account_id=a.id WHERE g.hash=?').get(digest(body.resume_token));
    if (!row || row.disabled || !row.is_guest) fail(401, 'invalid_guest', 'ゲストを復元できません。');
    return this.newSession(row, body.resume_token);
  }
  recoverGuest(id, body) {
    if (!body || Array.isArray(body) || typeof body !== 'object'
      || typeof body.expected_display_name !== 'string'
      || !Number.isInteger(body.expected_revision) || body.expected_revision < 0)
      fail(422, 'invalid_guest_recovery', 'ゲスト名と編成版番号が必要です。');
    return this.transaction(() => {
      const row = this.db.prepare('SELECT * FROM accounts WHERE id=?').get(id);
      if (!row || !row.is_guest || row.disabled)
        fail(404, 'guest_not_recoverable', '復旧対象のゲストが見つかりません。');
      if (row.display_name !== body.expected_display_name || row.revision !== body.expected_revision)
        fail(409, 'guest_recovery_precondition_failed', 'ゲスト情報が一致しません。再読込して確認してください。');
      const resume = secret();
      this.db.prepare('INSERT INTO guest_credentials VALUES (?,?,?)').run(digest(resume), row.id, this.now());
      this.audit('guest_recovery', row.id);
      return { account_id: row.id, display_name: row.display_name, guest_resume_token: resume };
    });
  }
  authenticated(token) {
    if (typeof token !== 'string' || token.length > 128) fail(401, 'unauthorized', 'ログインしてください。');
    const row = this.db.prepare('SELECT a.*,s.hash AS session_hash FROM accounts a JOIN sessions s ON s.account_id=a.id WHERE s.hash=? AND s.expires_at>?').get(digest(token), this.now());
    if (!row || row.disabled) fail(401, 'unauthorized', 'ログインが期限切れになりました。再ログインしてください。');
    return row;
  }
  logout(token) {
    const row = this.authenticated(token);
    this.db.prepare('DELETE FROM sessions WHERE hash=?').run(row.session_hash);
    this.audit('logout', row.id); return { ok: true };
  }
  async upgrade(token, body) {
    const before = this.authenticated(token);
    if (!before.is_guest) fail(409, 'not_guest', '通常アカウントです。');
    const name = username(body.username), hash = await this.kdf(() => passwordHash(body.password));
    return this.transaction(() => {
      const row = this.authenticated(token);
      if (!row.is_guest) fail(409, 'not_guest', '通常アカウントです。');
      if (this.db.prepare('SELECT id FROM accounts WHERE username=?').get(name)) fail(409, 'username_taken', 'そのログイン名は使用されています。');
      this.db.prepare('UPDATE accounts SET username=?,display_name=?,password_hash=?,is_guest=0 WHERE id=?').run(name, body.username, hash, row.id);
      this.db.prepare('DELETE FROM guest_credentials WHERE account_id=?').run(row.id);
      this.db.prepare('DELETE FROM sessions WHERE account_id=?').run(row.id);
      this.audit('guest_upgrade', row.id);
      return this.newSession(this.db.prepare('SELECT * FROM accounts WHERE id=?').get(row.id));
    });
  }
  getDeck(token) {
    const row = this.authenticated(token);
    return { realm_id: this.realmId, account_id: row.id, deck: this.publicAccount(row).deck };
  }
  saveDeck(token, body) {
    exactObject(body, ['deck', 'expected_revision'], fail);
    if (!Number.isInteger(body.expected_revision) || body.expected_revision < 0) fail(422, 'invalid_revision', '保存の版番号が必要です。');
    return this.transaction(() => {
      const row = this.authenticated(token);
      const deck = this.validateDeck(body.deck, this.progressForAccount(row.id));
      if (row.revision !== body.expected_revision) fail(409, 'revision_conflict', '別の接続で編成が変更されました。再読込してください。', { current_revision: row.revision });
      this.db.prepare('UPDATE accounts SET deck_json=?,revision=revision+1 WHERE id=?').run(JSON.stringify(deck), row.id);
      this.audit('deck_save', row.id, { revision: row.revision + 1 });
      return this.getDeck(token);
    });
  }
  progressForAccount(accountId) {
    const row = this.db.prepare('SELECT progress_json FROM battle_assist_progress WHERE account_id=?').get(accountId);
    // Reading a legacy account never creates a row or changes its revision.
    return row ? validateProgress(JSON.parse(row.progress_json), this.modules, fail) : emptyProgress();
  }
  getBattleAssistProgress(token) {
    return this.progressForAccount(this.authenticated(token).id);
  }
  developBattleAssist(token, body) {
    exactObject(body, ['module_id', 'expected_revision'], fail, 'invalid_development');
    if (typeof body.module_id !== 'string' || !Number.isSafeInteger(body.expected_revision) || body.expected_revision < 0)
      fail(422, 'invalid_development', '開発IDと版番号が必要です。');
    return this.transaction(() => {
      const account = this.authenticated(token), progress = this.progressForAccount(account.id);
      const module = this.modules.get(body.module_id);
      if (!validModuleDefinition(module) || !module.recipe_known)
        fail(422, 'unsupported_module', 'このモジュールは開発に未対応です。');
      // Retrying an already successful purchase is harmless even with its original revision.
      if (progress.developed_module_ids.includes(module.id)) return progress;
      if (progress.revision !== body.expected_revision)
        fail(409, 'progress_revision_conflict', '開発状況が変更されました。再読込してください。', { current_revision: progress.revision });
      if (!Number.isSafeInteger(module.development_cost_gp) || module.development_cost_gp < 0 || !module.development_materials)
        fail(422, 'invalid_recipe', '開発レシピが不正です。');
      if (progress.gp < module.development_cost_gp) fail(422, 'insufficient_gp', 'GPが不足しています。');
      for (const [material, quantity] of Object.entries(module.development_materials)) {
        if (!Number.isSafeInteger(quantity) || quantity < 0 || quantity > 2147483647) fail(422, 'invalid_recipe', '開発レシピが不正です。');
        if ((progress.materials[material] ?? 0) < quantity) fail(422, 'insufficient_materials', '開発素材が不足しています。');
      }
      const next = structuredClone(progress);
      next.gp -= module.development_cost_gp;
      for (const [material, quantity] of Object.entries(module.development_materials)) next.materials[material] = (next.materials[material] ?? 0) - quantity;
      next.developed_module_ids.push(module.id); next.revision++;
      validateProgress(next, this.modules, fail);
      this.db.prepare('INSERT INTO battle_assist_progress(account_id,progress_json) VALUES (?,?) ON CONFLICT(account_id) DO UPDATE SET progress_json=excluded.progress_json')
        .run(account.id, JSON.stringify(next));
      this.audit('battle_assist_develop', account.id, { module_id: module.id, revision: next.revision });
      return next;
    });
  }
  // Called solely from authenticated local admin route; explicit grants are audited, never defaulted.
  grantBattleAssistResources(accountId, body) {
    exactObject(body, ['expected_revision', 'gp', 'materials'], fail, 'invalid_grant');
    return this.transaction(() => {
      const account = this.db.prepare('SELECT * FROM accounts WHERE id=?').get(accountId);
      if (!account || account.disabled) fail(404, 'account_not_found', 'アカウントがありません。');
      const progress = this.progressForAccount(accountId);
      if (!Number.isSafeInteger(body.expected_revision) || body.expected_revision !== progress.revision)
        fail(409, 'progress_revision_conflict', '開発状況が変更されました。');
      const grant = validateProgress({ gp: body.gp, revision: 0, developed_module_ids: [], materials: body.materials }, this.modules, fail);
      const next = structuredClone(progress); next.gp += grant.gp; next.revision++;
      for (const [material, count] of Object.entries(grant.materials)) next.materials[material] = (next.materials[material] ?? 0) + count;
      validateProgress(next, this.modules, fail);
      this.db.prepare('INSERT INTO battle_assist_progress(account_id,progress_json) VALUES (?,?) ON CONFLICT(account_id) DO UPDATE SET progress_json=excluded.progress_json')
        .run(accountId, JSON.stringify(next));
      this.audit('battle_assist_grant', accountId, { gp: grant.gp, materials: grant.materials, revision: next.revision });
      return next;
    });
  }
  servers() {
    return { realm_id: this.realmId, servers: [{ server_id: this.server.server_id, name: this.server.name || '自宅サーバー',
      game_address: this.server.game_address, online: Boolean(this.lastHeartbeat && this.now() - this.lastHeartbeat.at < 20000),
      players: this.lastHeartbeat?.players ?? 0, max_players: this.server.max_players ?? 12, catalog_hash: this.catalog.catalog_hash }] };
  }
  sealRoomLease(value, roomId) {
    const nonce = randomBytes(12), cipher = createCipheriv('aes-256-gcm', this.roomLeaseKey, nonce);
    cipher.setAAD(Buffer.from(roomId));
    const encrypted = Buffer.concat([cipher.update(value, 'utf8'), cipher.final()]);
    return [nonce, cipher.getAuthTag(), encrypted].map(part => part.toString('base64url')).join('.');
  }
  openRoomLease(sealed, roomId) {
    try {
      const [nonce, tag, encrypted] = String(sealed).split('.').map(part => Buffer.from(part, 'base64url'));
      if (nonce.length !== 12 || tag.length !== 16) throw new Error('invalid room lease');
      const decipher = createDecipheriv('aes-256-gcm', this.roomLeaseKey, nonce);
      decipher.setAuthTag(tag); decipher.setAAD(Buffer.from(roomId));
      return Buffer.concat([decipher.update(encrypted), decipher.final()]).toString('utf8');
    } catch { throw new Error('Room lease credential is unavailable'); }
  }
  normalizeRoomSettings(body, sourceAddress = '127.0.0.1') {
    const input = roomObject(body);
    if (Object.prototype.hasOwnProperty.call(input, 'address') || Object.prototype.hasOwnProperty.call(input, 'game_address') ||
      Object.prototype.hasOwnProperty.call(input, 'host_address'))
      fail(422, 'address_not_allowed', '接続先アドレスはAPIが接続元から決定します。');
    const name = roomName(input.name);
    if (!ROOM_MAP_IDS.includes(input.map_id)) fail(422, 'invalid_map_id', 'このマップはルーム作成に使用できません。');
    const rule_id = roomEnum(input.rule_id, 'rule_id');
    const difficulty = roomEnum(input.difficulty, 'difficulty');
    if (!Number.isInteger(input.bot_count) || input.bot_count < 0 || input.bot_count > 11)
      fail(422, 'invalid_bot_count', 'Bot数は0〜11の整数で指定してください。');
    const fill_teams_with_bots = Object.prototype.hasOwnProperty.call(input, 'fill_teams_with_bots')
      ? input.fill_teams_with_bots : false;
    if (typeof fill_teams_with_bots !== 'boolean')
      fail(422, 'invalid_fill_teams_with_bots', 'fill_teams_with_botsはbooleanで指定してください。');
    if (input.build_id !== this.roomBuildId) fail(409, 'build_mismatch', 'このゲームビルドはルームに参加できません。');
    if (input.catalog_hash !== this.catalog.catalog_hash) fail(409, 'catalog_mismatch', 'ゲームとカタログの版が一致しません。');
    if (typeof input.idempotency_key !== 'string' || !/^[A-Za-z0-9._~:/-]{1,128}$/.test(input.idempotency_key))
      fail(422, 'invalid_idempotency_key', 'idempotency_keyが不正です。');
    const bot_count = fill_teams_with_bots ? 0 : input.bot_count;
    return { name, map_id: input.map_id, rule_id, bot_count, fill_teams_with_bots, difficulty,
      build_id: this.roomBuildId, catalog_hash: this.catalog.catalog_hash, game_port: roomPort(input.game_port),
      max_players: fill_teams_with_bots ? ROOM_FILLED_MAX_PLAYERS : ROOM_LEGACY_MAX_PLAYERS,
      idempotency_key: input.idempotency_key, source_address: sourceIPv4(sourceAddress) };
  }
  roomPublic(row) {
    return { room_id: row.room_id, instance_id: row.instance_id, name: row.name, host_name: row.host_name,
      game_address: row.game_address, map_id: row.map_id, rule_id: row.rule_id, bot_count: row.bot_count,
      difficulty: row.difficulty, players: row.human_players, human_players: row.human_players,
      bot_players: row.bot_players, bots: row.bot_players, max_players: row.max_players,
      fill_teams_with_bots: Boolean(row.fill_teams_with_bots),
      build_id: row.build_id, catalog_hash: row.catalog_hash, state: row.state };
  }
  roomCreateResponse(row) {
    const response = { ...this.roomPublic(row), server_id: row.room_id,
      settings: { name: row.name, map_id: row.map_id, rule_id: row.rule_id, bot_count: row.bot_count,
        fill_teams_with_bots: Boolean(row.fill_teams_with_bots), difficulty: row.difficulty,
        build_id: row.build_id, catalog_hash: row.catalog_hash, game_port: row.game_port },
      room_lease: this.openRoomLease(row.lease_cipher, row.room_id),
      lease_expires_at: row.lease_expires_at, expires_at: row.lease_expires_at, realm_id: this.realmId };
    return response;
  }
  expireRoomsInTransaction(now) {
    const expired = this.db.prepare("SELECT room_id FROM rooms WHERE state IN ('Starting','Ready','Playing') AND lease_expires_at<=?").all(now);
    if (!expired.length) return;
    this.db.prepare("UPDATE rooms SET state='Expired', lease_expires_at=? WHERE state IN ('Starting','Ready','Playing') AND lease_expires_at<=?").run(now, now);
    for (const room of expired) this.db.prepare('DELETE FROM room_tickets WHERE room_id=?').run(room.room_id);
  }
  sweepExpiredRooms() {
    return this.transaction(() => {
      const before = this.db.prepare("SELECT COUNT(*) AS n FROM rooms WHERE state IN ('Starting','Ready','Playing') AND lease_expires_at<=?").get(this.now()).n;
      this.expireRoomsInTransaction(this.now());
      return before;
    });
  }
  createRoom(token, body, remoteAddress = '127.0.0.1') {
    const account = this.authenticated(token);
    const settings = this.normalizeRoomSettings(body, remoteAddress);
    if (body.expected_revision !== undefined && (!Number.isInteger(body.expected_revision) || body.expected_revision < 0))
      fail(422, 'invalid_revision', '保存の版番号が不正です。');
    if (body.expected_revision !== undefined && body.expected_revision !== account.revision)
      fail(409, 'revision_conflict', '別の接続で編成が変更されました。再読込してください。', { current_revision: account.revision });
    const now = this.now();
    return this.transaction(() => {
      this.expireRoomsInTransaction(now);
      const existing = this.db.prepare('SELECT * FROM rooms WHERE owner_account_id=? AND idempotency_key=?')
        .get(account.id, settings.idempotency_key);
      if (existing) {
        const same = existing.name === settings.name && existing.map_id === settings.map_id && existing.rule_id === settings.rule_id &&
          existing.bot_count === settings.bot_count && Boolean(existing.fill_teams_with_bots) === settings.fill_teams_with_bots &&
          existing.difficulty === settings.difficulty && existing.build_id === settings.build_id &&
          existing.catalog_hash === settings.catalog_hash && existing.game_port === settings.game_port;
        if (!same) fail(409, 'idempotency_conflict', '同じidempotency_keyには別のルーム設定を指定できません。', { room_id: existing.room_id });
        return this.roomCreateResponse(existing);
      }
      const active = this.db.prepare("SELECT room_id FROM rooms WHERE owner_account_id=? AND state IN ('Starting','Ready','Playing') AND lease_expires_at>? LIMIT 1")
        .get(account.id, now);
      if (active) fail(409, 'owner_room_exists', '作成者が同時に保持できるルームは1つまでです。', { room_id: active.room_id });
      const room_id = randomUUID(), instance_id = randomUUID(), lease = secret();
      const expires = now + this.startingRoomLeaseLifetimeMs;
      const game_address = `${settings.source_address}:${settings.game_port}`;
      this.db.prepare(`INSERT INTO rooms(
        room_id,instance_id,owner_account_id,idempotency_key,name,host_name,source_address,game_address,
        map_id,rule_id,bot_count,fill_teams_with_bots,difficulty,build_id,catalog_hash,game_port,state,human_players,bot_players,
        max_players,created_at,last_heartbeat_at,lease_expires_at,lease_hash,lease_cipher)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`).run(
        room_id, instance_id, account.id, settings.idempotency_key, settings.name, account.display_name,
        settings.source_address, game_address, settings.map_id, settings.rule_id, settings.bot_count,
        settings.fill_teams_with_bots ? 1 : 0, settings.difficulty, settings.build_id, settings.catalog_hash,
        settings.game_port, 'Starting', 1, settings.fill_teams_with_bots ? 0 : settings.bot_count,
        settings.max_players, now, now, expires, digest(lease), this.sealRoomLease(lease, room_id));
      this.audit('room_create', account.id, { room_id, instance_id, map_id: settings.map_id, game_port: settings.game_port });
      return this.roomCreateResponse(this.db.prepare('SELECT * FROM rooms WHERE room_id=?').get(room_id));
    });
  }
  requireRoomLeaseInTransaction(roomIdValue, lease, expectedInstanceId) {
    const id = roomId(roomIdValue);
    const row = this.db.prepare('SELECT * FROM rooms WHERE room_id=?').get(id);
    if (!row) fail(404, 'room_not_found', 'ルームが見つかりません。');
    const now = this.now();
    if (['Starting', 'Ready', 'Playing'].includes(row.state) && row.lease_expires_at <= now) {
      this.db.prepare("UPDATE rooms SET state='Expired', lease_expires_at=? WHERE room_id=? AND state IN ('Starting','Ready','Playing')").run(now, id);
      this.db.prepare('DELETE FROM room_tickets WHERE room_id=?').run(id);
      fail(410, 'room_expired', 'ルームの有効期限が切れています。');
    }
    if (row.state === 'Closed') fail(410, 'room_closed', 'ルームは終了しています。');
    if (row.state === 'Expired') fail(410, 'room_expired', 'ルームの有効期限が切れています。');
    if (typeof lease !== 'string' || lease.length > 128 || !/^[A-Za-z0-9_-]+$/.test(lease) ||
      !timingSafeEqual(Buffer.from(digest(lease), 'hex'), Buffer.from(row.lease_hash, 'hex')))
      fail(401, 'room_lease_invalid', 'ルームの認証情報が不正です。');
    if (expectedInstanceId !== undefined && expectedInstanceId !== row.instance_id)
      fail(409, 'instance_mismatch', 'ルームのインスタンスが一致しません。');
    return row;
  }
  roomHeartbeat(roomIdValue, lease, body) {
    const input = roomObject(body);
    if (typeof input.instance_id !== 'string') fail(422, 'invalid_instance_id', 'instance_idが必要です。');
    const requestedState = roomState(input.state);
    return this.transaction(() => {
      const row = this.requireRoomLeaseInTransaction(roomIdValue, lease, input.instance_id);
      if (row.build_id !== this.roomBuildId) fail(409, 'build_mismatch', 'ゲームを更新してルームを作り直してください。');
      if (input.catalog_hash !== undefined && input.catalog_hash !== row.catalog_hash) fail(409, 'catalog_mismatch', 'ゲームとカタログの版が一致しません。');
      if (input.build_id !== undefined && input.build_id !== row.build_id) fail(409, 'build_mismatch', 'このゲームビルドはルームに参加できません。');
      if (row.state !== 'Starting' && requestedState === 'Starting') fail(409, 'invalid_room_transition', 'ルーム状態をStartingへ戻せません。');
      if (requestedState !== 'Starting' && !row.owner_ticket_consumed)
        fail(409, 'host_ticket_required', 'host自身の参加ticketをconsumeしてから準備完了にしてください。');
      const parsed = this.roomPlayers(input, row);
      const now = this.now(), expires = now + (requestedState === 'Starting' ? this.startingRoomLeaseLifetimeMs : this.roomLeaseLifetimeMs);
      this.db.prepare('UPDATE rooms SET state=?,human_players=?,bot_players=?,last_heartbeat_at=?,lease_expires_at=? WHERE room_id=?')
        .run(requestedState, parsed.humans, parsed.bots, now, expires, row.room_id);
      return { ok: true, realm_id: this.realmId, catalog_hash: row.catalog_hash, room_id: row.room_id,
        instance_id: row.instance_id, state: input.state, lease_expires_at: expires };
    });
  }
  roomPlayers(input, row) {
    let humans, bots;
    const bFillTeamsWithBots = Boolean(row.fill_teams_with_bots);
    const players = input.players;
    if (Number.isInteger(players)) humans = players;
    else if (players && typeof players === 'object' && !Array.isArray(players)) {
      humans = players.humans ?? players.human ?? players.players;
      bots = players.bots ?? players.bot ?? players.bot_players;
    } else humans = input.human_players ?? input.humans;
    if (!Number.isInteger(humans) || humans < 0 || humans > row.max_players)
      fail(422, 'invalid_players', '参加人数が不正です。');
    if (bFillTeamsWithBots) {
      // A scalar heartbeat from an older host reports humans only. Preserve the
      // last dynamic Bot count until the host sends the object form.
      if (bots === undefined) bots = Math.min(row.bot_players, row.max_players - humans);
      if (!Number.isInteger(bots) || bots < 0 || humans + bots > row.max_players)
        fail(422, 'invalid_players', '参加人数が不正です。');
      if (input.bot_count !== undefined && input.bot_count !== 0)
        fail(409, 'bot_count_mismatch', 'Bot自動補充ルームではbot_countを変更できません。');
      return { humans, bots };
    }
    if (humans + row.bot_count > row.max_players)
      fail(422, 'invalid_players', '参加人数が不正です。');
    if (bots !== undefined && (!Number.isInteger(bots) || bots !== row.bot_count))
      fail(409, 'bot_count_mismatch', 'ルーム作成時のBot数を変更できません。');
    if (input.bot_count !== undefined && input.bot_count !== row.bot_count)
      fail(409, 'bot_count_mismatch', 'ルーム作成時のBot数を変更できません。');
    return { humans, bots: row.bot_count };
  }
  closeRoom(roomIdValue, lease, body = {}) {
    const input = roomObject(body);
    return this.transaction(() => {
      const row = this.requireRoomLeaseInTransaction(roomIdValue, lease, input.instance_id);
      const now = this.now();
      this.db.prepare("UPDATE rooms SET state='Closed',lease_expires_at=? WHERE room_id=?").run(now, row.room_id);
      this.db.prepare('DELETE FROM room_tickets WHERE room_id=?').run(row.room_id);
      this.audit('room_close', row.owner_account_id, { room_id: row.room_id, instance_id: row.instance_id });
      return { ok: true, realm_id: this.realmId, catalog_hash: row.catalog_hash, room_id: row.room_id,
        instance_id: row.instance_id, state: 'Closed' };
    });
  }
  rooms() {
    this.sweepExpiredRooms();
    const cutoff = this.now() - ROOM_HEARTBEAT_VISIBLE_MS;
    return this.db.prepare("SELECT * FROM rooms WHERE state IN ('Ready','Playing') AND last_heartbeat_at>=? AND lease_expires_at>? AND build_id=? ORDER BY created_at DESC")
      .all(cutoff, this.now(), this.roomBuildId).map(row => this.roomPublic(row));
  }
  issueRoomTicket(token, roomIdValue, body) {
    const account = this.authenticated(token), input = roomObject(body), id = roomId(roomIdValue);
    if (typeof input.instance_id !== 'string') fail(422, 'invalid_instance_id', 'instance_idが必要です。');
    if (input.expected_revision !== undefined && (!Number.isInteger(input.expected_revision) || input.expected_revision < 0))
      fail(422, 'invalid_revision', '保存の版番号が不正です。');
    if (input.expected_revision !== undefined && input.expected_revision !== account.revision)
      fail(409, 'revision_conflict', '別の接続で編成が変更されました。再読込してください。', { current_revision: account.revision });
    if (input.build_id !== this.roomBuildId) fail(409, 'build_mismatch', 'ゲームを同じ最新版へ更新してから参加してください。');
    if (input.catalog_hash !== undefined && input.catalog_hash !== this.catalog.catalog_hash) fail(409, 'catalog_mismatch', 'ゲームとカタログの版が一致しません。');
    this.validateDeck(JSON.parse(account.deck_json), this.progressForAccount(account.id));
    if (!JSON.parse(account.deck_json).slots.some(slot => slot.suit_id)) fail(422, 'empty_deck', '出撃する機体を編成してください。');
    return this.transaction(() => {
      const now = this.now();
      this.expireRoomsInTransaction(now);
      const row = this.db.prepare('SELECT * FROM rooms WHERE room_id=?').get(id);
      if (!row) fail(404, 'room_not_found', 'ルームが見つかりません。');
      if (row.build_id !== this.roomBuildId) fail(409, 'build_mismatch', 'ゲームを更新してルームを作り直してください。');
      if (row.instance_id !== input.instance_id) fail(409, 'instance_mismatch', 'ルームのインスタンスが一致しません。');
      if (row.state === 'Starting' && row.owner_account_id !== account.id) fail(409, 'room_starting', 'ルームの準備が完了していません。');
      if (!ROOM_STATES.includes(row.state)) fail(410, row.state === 'Closed' ? 'room_closed' : 'room_expired', 'ルームは利用できません。');
      if (row.lease_expires_at <= now) fail(410, 'room_expired', 'ルームの有効期限が切れています。');
      const humanCapacity = Boolean(row.fill_teams_with_bots)
        ? row.max_players : row.max_players - row.bot_count;
      if (account.id !== row.owner_account_id && row.human_players >= humanCapacity)
        fail(409, 'room_full', 'ルームの参加枠がありません。');
      this.db.prepare('DELETE FROM room_tickets WHERE expires_at<?').run(now);
      const ticket = secret(), expires = now + this.roomTicketLifetimeMs;
      this.db.prepare(`INSERT INTO room_tickets(hash,room_id,instance_id,session_hash,account_id,deck_revision,catalog_hash,build_id,expires_at,connection_id,snapshot_json)
        VALUES (?,?,?,?,?,?,?,?,?,?,?)`).run(digest(ticket), row.room_id, row.instance_id, account.session_hash, account.id,
        account.revision, row.catalog_hash, row.build_id, expires, null, JSON.stringify(this.publicAccount(account)));
      this.audit('room_ticket_issue', account.id, { room_id: row.room_id, instance_id: row.instance_id, deck_revision: account.revision });
      return { ticket, server_id: row.room_id, room_id: row.room_id, instance_id: row.instance_id,
        game_address: row.game_address, expires_at: expires, catalog_hash: row.catalog_hash, build_id: row.build_id };
    });
  }
  consumeRoomTicket(roomIdValue, lease, body) {
    const input = roomObject(body), id = roomId(roomIdValue), ticketValue = input.ticket, connection = connectionId(input.connection_id);
    if (typeof ticketValue !== 'string' || ticketValue.length > 128 || !/^[A-Za-z0-9_-]+$/.test(ticketValue)) fail(401, 'invalid_ticket', '参加チケットが不正です。');
    return this.transaction(() => {
      const row = this.requireRoomLeaseInTransaction(id, lease);
      if (row.build_id !== this.roomBuildId) fail(409, 'build_mismatch', 'ゲームを更新してルームを作り直してください。');
      if (input.instance_id !== undefined && input.instance_id !== row.instance_id) fail(409, 'instance_mismatch', 'ルームのインスタンスが一致しません。');
      if (input.catalog_hash !== undefined && input.catalog_hash !== row.catalog_hash) fail(409, 'catalog_mismatch', '接続先またはゲームの版が一致しません。');
      if (input.build_id !== undefined && input.build_id !== row.build_id) fail(409, 'build_mismatch', 'このゲームビルドはルームに参加できません。');
      const ticket = this.db.prepare(`SELECT t.*,a.disabled,s.expires_at AS session_expires_at
        FROM room_tickets t JOIN accounts a ON a.id=t.account_id JOIN sessions s ON s.hash=t.session_hash
        WHERE t.hash=? AND t.room_id=?`).get(digest(ticketValue), row.room_id);
      if (!ticket || ticket.expires_at <= this.now() || ticket.instance_id !== row.instance_id || ticket.disabled || ticket.session_expires_at <= this.now())
        fail(401, 'invalid_ticket', '参加チケットが期限切れまたは無効です。');
      if (ticket.catalog_hash !== row.catalog_hash || ticket.build_id !== row.build_id) fail(409, 'catalog_mismatch', '接続先またはゲームの版が一致しません。');
      if (ticket.account_id !== row.owner_account_id && row.state === 'Starting') fail(409, 'room_starting', 'ルームの準備が完了していません。');
      if (ticket.connection_id && ticket.connection_id !== connection) fail(409, 'ticket_replayed', 'この参加チケットは別の接続ですでに使用されています。');
      // A retry on the same connection is the idempotent success path. Once the
      // server accepted a snapshot, a subsequent deck save must not turn that
      // retry into a different response.
      if (ticket.connection_id) return { ...JSON.parse(ticket.snapshot_json), room_id: row.room_id, instance_id: row.instance_id,
        connection_id: connection, catalog_hash: row.catalog_hash, build_id: row.build_id };
      const current = this.db.prepare('SELECT revision FROM accounts WHERE id=?').get(ticket.account_id);
      if (!current || current.revision !== ticket.deck_revision) fail(409, 'deck_revision_changed', '参加前に編成が変更されたため、チケットを再取得してください。');
      if (!ticket.connection_id) {
        this.db.prepare('UPDATE room_tickets SET connection_id=? WHERE hash=?').run(connection, ticket.hash);
        if (ticket.account_id === row.owner_account_id)
          this.db.prepare('UPDATE rooms SET owner_ticket_consumed=1 WHERE room_id=? AND owner_ticket_consumed=0').run(row.room_id);
        this.audit('room_ticket_consume', ticket.account_id, { room_id: row.room_id, instance_id: row.instance_id, connection_id: connection });
      }
      return { ...JSON.parse(ticket.snapshot_json), room_id: row.room_id, instance_id: row.instance_id,
        connection_id: connection, catalog_hash: row.catalog_hash, build_id: row.build_id };
    });
  }
  heartbeat(body) {
    if (body.server_id !== this.server.server_id || body.catalog_hash !== this.catalog.catalog_hash) fail(409, 'catalog_mismatch', 'ゲームサーバーとカタログの版が一致しません。');
    if (!Number.isInteger(body.players) || body.players < 0 || body.players > 104) fail(422, 'invalid_players', '参加人数が不正です。');
    this.lastHeartbeat = { at: this.now(), players: body.players };
    return { ok: true, realm_id: this.realmId, catalog_hash: this.catalog.catalog_hash };
  }
  issueTicket(token, body) {
    const row = this.authenticated(token), server = this.servers().servers[0];
    if (body.server_id !== this.server.server_id) fail(404, 'server_not_found', 'サーバーが見つかりません。');
    if (!server.online) fail(503, 'server_offline', 'ゲームサーバーは起動していません。');
    this.validateDeck(JSON.parse(row.deck_json), this.progressForAccount(row.id));
    if (!JSON.parse(row.deck_json).slots.some(x => x.suit_id)) fail(422, 'empty_deck', '出撃する機体を編成してください。');
    const ticket = secret(), expires_at = this.now() + this.ticketLifetimeMs;
    this.db.prepare('DELETE FROM tickets WHERE expires_at<?').run(this.now() - 60000);
    this.db.prepare('INSERT INTO tickets VALUES (?,?,?,?,?,NULL,?)').run(digest(ticket), row.session_hash, row.id, body.server_id, expires_at, JSON.stringify(this.publicAccount(row)));
    return { ticket, game_address: this.server.game_address, server_id: body.server_id, expires_at, catalog_hash: this.catalog.catalog_hash };
  }
  consumeTicket(body) {
    if (typeof body.ticket !== 'string' || body.ticket.length > 128 || typeof body.connection_id !== 'string' || !/^[a-zA-Z0-9_-]{16,128}$/.test(body.connection_id)) fail(401, 'invalid_ticket', '参加チケットが不正です。');
    if (body.server_id !== this.server.server_id || body.catalog_hash !== this.catalog.catalog_hash) fail(409, 'catalog_mismatch', '接続先またはゲームの版が一致しません。');
    return this.transaction(() => {
      const ticket = this.db.prepare('SELECT t.* FROM tickets t JOIN accounts a ON a.id=t.account_id JOIN sessions s ON s.hash=t.session_hash WHERE t.hash=? AND a.disabled=0 AND s.expires_at>?').get(digest(body.ticket), this.now());
      if (!ticket || ticket.expires_at <= this.now() || ticket.server_id !== body.server_id) fail(401, 'invalid_ticket', '参加チケットが期限切れまたは無効です。');
      if (ticket.connection_id && ticket.connection_id !== body.connection_id) fail(409, 'ticket_replayed', 'この参加チケットは使用済みです。');
      if (!ticket.connection_id) {
        this.db.prepare('UPDATE tickets SET connection_id=? WHERE hash=?').run(body.connection_id, ticket.hash);
        this.audit('game_join', ticket.account_id, { server_id: body.server_id });
      }
      return { ...JSON.parse(ticket.snapshot_json), connection_id: body.connection_id };
    });
  }
  listAccounts() {
    return this.db.prepare('SELECT * FROM accounts ORDER BY created_at DESC').all().map(row => ({ ...this.publicAccount(row),
      disabled: Boolean(row.disabled), is_test: Boolean(row.is_test), created_at: row.created_at, last_login_at: row.last_login_at,
      sessions: this.db.prepare('SELECT COUNT(*) AS n FROM sessions WHERE account_id=? AND expires_at>?').get(row.id, this.now()).n }));
  }
  async resetPassword(id) {
    const row = this.db.prepare('SELECT * FROM accounts WHERE id=?').get(id);
    if (!row || row.is_guest) fail(404, 'account_not_found', '通常アカウントが見つかりません。');
    const temporary_password = secret().slice(0, 20), hash = await this.kdf(() => passwordHash(temporary_password));
    this.transaction(() => {
      this.db.prepare('UPDATE accounts SET password_hash=? WHERE id=?').run(hash, id);
      this.db.prepare('DELETE FROM sessions WHERE account_id=?').run(id);
      if (row.is_test) this.saveTestCredential(id, temporary_password);
      this.audit('password_reset', id);
    });
    return { account_id: id, temporary_password };
  }
  setDisabled(id, disabled) {
    if (typeof disabled !== 'boolean') fail(422, 'invalid_disabled', '状態が不正です。');
    return this.transaction(() => {
      const result = this.db.prepare('UPDATE accounts SET disabled=? WHERE id=?').run(disabled ? 1 : 0, id);
      if (!result.changes) fail(404, 'account_not_found', 'アカウントが見つかりません。');
      this.db.prepare('DELETE FROM sessions WHERE account_id=?').run(id);
      if (disabled) this.db.prepare('DELETE FROM guest_credentials WHERE account_id=?').run(id);
      this.audit(disabled ? 'account_disable' : 'account_enable', id);
      return { ok: true };
    });
  }
  saveTestCredential(id, value) {
    if (!this.debugVaultKey) fail(503, 'debug_vault_unavailable', 'デバッグ用の保存キーが設定されていません。');
    const nonce = randomBytes(12), cipher = createCipheriv('aes-256-gcm', this.debugVaultKey, nonce);
    cipher.setAAD(Buffer.from(id));
    const encrypted = Buffer.concat([cipher.update(value, 'utf8'), cipher.final()]);
    const sealed = [nonce, cipher.getAuthTag(), encrypted].map(x => x.toString('base64')).join('.');
    this.db.prepare('INSERT INTO test_credentials VALUES (?,?) ON CONFLICT(account_id) DO UPDATE SET sealed=excluded.sealed').run(id, sealed);
  }
  async createTestAccount(body) {
    if (!this.debugVaultKey) fail(503, 'debug_vault_unavailable', 'デバッグ用の保存キーが設定されていません。');
    const name = username(body.username);
    if (!name.startsWith('debug_')) fail(422, 'test_prefix_required', 'テスト用のログイン名は debug_ から始めてください。');
    const value = secret().slice(0, 20), hash = await this.kdf(() => passwordHash(value));
    return this.transaction(() => {
      if (this.db.prepare('SELECT id FROM accounts WHERE username=?').get(name)) fail(409, 'username_taken', 'そのログイン名は使用されています。');
      const id = randomUUID(), now = this.now();
      this.db.prepare('INSERT INTO accounts(id,username,display_name,password_hash,is_guest,is_test,created_at,last_login_at,deck_json) VALUES (?,?,?,?,0,1,?,?,?)')
        .run(id, name, name, hash, now, now, JSON.stringify(this.defaultDeck));
      this.saveTestCredential(id, value); this.audit('test_account_create', id);
      return { account_id: id, username: name, password: value };
    });
  }
  listTestCredentials() {
    if (!this.debugVaultKey) return [];
    return this.db.prepare('SELECT a.id,a.username,a.disabled,t.sealed FROM accounts a JOIN test_credentials t ON t.account_id=a.id WHERE a.is_test=1 ORDER BY a.username').all().map(row => {
      const [nonce, tag, encrypted] = row.sealed.split('.').map(x => Buffer.from(x, 'base64'));
      const decipher = createDecipheriv('aes-256-gcm', this.debugVaultKey, nonce);
      decipher.setAuthTag(tag); decipher.setAAD(Buffer.from(row.id));
      const value = Buffer.concat([decipher.update(encrypted), decipher.final()]).toString('utf8');
      return { account_id: row.id, username: row.username, disabled: Boolean(row.disabled), password: value };
    });
  }
}
