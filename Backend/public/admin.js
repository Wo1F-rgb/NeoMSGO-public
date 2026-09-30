const $ = id => document.getElementById(id);
let accounts = [], selectedId = null, catalog = { suits: [], weapons: [] };
const nameOf = (id, type) => catalog[type].find(x => x.id === id)?.display_name || id?.split(':')[1] || '未装備';
function message(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
async function api(path, method = 'GET', data) {
  const response = await fetch(path, { method, headers: data ? { 'Content-Type': 'application/json' } : {}, body: data ? JSON.stringify(data) : undefined });
  const result = await response.json();
  if (!response.ok) {
    if (response.status === 401) { $('login-panel').hidden = false; $('workspace').hidden = true; }
    throw new Error(result.error?.message || '読み込めませんでした。');
  }
  return result;
}
function element(tag, text, className) { const node = document.createElement(tag); node.textContent = text; if (className) node.className = className; return node; }
function render() {
  const needle = $('filter').value.toLocaleLowerCase(); $('accounts').replaceChildren();
  for (const account of accounts.filter(a => `${a.username} ${a.display_name}`.toLocaleLowerCase().includes(needle))) {
    const row = element('tr', ''); row.classList.toggle('selected-row', account.account_id === selectedId);
    const name = element('td', ''), button = element('button', account.username, 'account-select');
    button.addEventListener('click', () => select(account.account_id)); name.append(button, element('small', account.display_name)); row.append(name);
    row.append(element('td', account.is_test ? 'デバッグ用' : account.is_guest ? 'ゲスト' : '通常アカウント'));
    row.append(element('td', account.disabled ? '利用停止中' : account.sessions ? 'セッションあり' : '有効'));
    row.append(element('td', new Date(account.last_login_at).toLocaleString('ja-JP')));
    row.append(element('td', `${account.deck.slots.filter(s => s.suit_id).length}機 / 保存版 ${account.deck.revision}`)); $('accounts').append(row);
  }
  if (!$('accounts').children.length) { const row = element('tr', ''); const cell = element('td', '該当するアカウントはありません。'); cell.colSpan = 5; row.append(cell); $('accounts').append(row); }
}
function select(id) {
  selectedId = id; const account = accounts.find(a => a.account_id === id);
  $('account-detail').hidden = !account; $('password-result').hidden = true; $('temporary-password').textContent = '';
  if (!account) return;
  $('detail-name').textContent = account.display_name;
  $('detail-meta').textContent = `${account.username} · アカウントID ${account.account_id}`;
  $('reset-password').disabled = account.is_guest; $('disable-account').textContent = account.disabled ? '利用を再開' : '利用を停止';
  $('deck').replaceChildren();
  account.deck.slots.forEach((slot, index) => {
    const card = element('article', '', 'suit'); card.classList.toggle('selected', account.deck.selected_slot === index);
    card.append(element('span', `${String(index + 1).padStart(2, '0')}${account.deck.selected_slot === index ? ' / 選択中' : ''}`, 'eyebrow'));
    card.append(element('h3', slot.suit_id ? nameOf(slot.suit_id, 'suits') : '機体なし'));
    const list = element('ol', ''); slot.weapon_ids.forEach(id => list.append(element('li', nameOf(id, 'weapons')))); card.append(list); $('deck').append(card);
  }); render();
}
async function refresh() {
  const [result, manifest] = await Promise.all([api('/admin/accounts'), api('/admin/catalog')]); catalog = manifest; accounts = result.accounts;
  $('login-panel').hidden = true; $('workspace').hidden = false;
  $('account-count').textContent = `${accounts.length}アカウント · サーバー保存`;
  const server = result.server; $('server-status').textContent = server.online ? `${server.name} / ${server.players}人接続` : `${server.name} / ゲーム停止中`;
  $('server-status').classList.toggle('online', server.online); render(); if (selectedId) select(selectedId); message('保存済みの情報を読み込みました。');
}
$('login-form').addEventListener('submit', async event => { event.preventDefault(); try { await api('/admin/login', 'POST', { key: $('admin-key').value }); $('admin-key').value = ''; await refresh(); } catch(e) { message(e.message, true); } });
$('refresh').addEventListener('click', () => refresh().catch(e => message(e.message, true)));
$('filter').addEventListener('input', render);
$('logout').addEventListener('click', async () => { try { await api('/admin/logout', 'POST', {}); accounts = []; selectedId = null; $('test-credentials').replaceChildren(); $('test-credentials').hidden = true; $('workspace').hidden = true; $('login-panel').hidden = false; $('temporary-password').textContent = ''; message('管理画面からログアウトしました。'); } catch(e) { message(e.message, true); } });
$('reset-password').addEventListener('click', async () => {
  const account = accounts.find(a => a.account_id === selectedId); if (!account) return;
  if (!confirm(`${account.username} のパスワードを再発行します。既存のログインも失効します。`)) return;
  $('reset-password').disabled = true;
  try { const result = await api(`/admin/accounts/${selectedId}/reset-password`, 'POST', {}); $('temporary-password').textContent = result.temporary_password; $('password-result').hidden = false; message('パスワードを再発行しました。対象ユーザーへ渡してください。'); }
  catch(e) { message(e.message, true); } finally { $('reset-password').disabled = account.is_guest; }
});
$('disable-account').addEventListener('click', async () => { const account = accounts.find(a => a.account_id === selectedId); if (!account) return;
  if (!confirm(`${account.username} の利用を${account.disabled ? '再開' : '停止'}します。`)) return;
  try { await api(`/admin/accounts/${selectedId}/disable`, 'POST', { disabled: !account.disabled }); await refresh(); } catch(e) { message(e.message, true); }
});
$('copy-password').addEventListener('click', async () => { try { await navigator.clipboard.writeText($('temporary-password').textContent); message('コピーしました。'); } catch { message('コピーできませんでした。表示された文字を選択してコピーしてください。', true); } });
$('hide-password').addEventListener('click', () => { $('temporary-password').textContent = ''; $('password-result').hidden = true; });
refresh().catch(e => message(e.message));
async function showTestAccounts() {
  const result = await api('/admin/test-accounts');
  const table = element('table', ''), head = element('tr', '');
  head.append(element('th', 'テスト用ログイン名'), element('th', 'パスワード'), element('th', '状態')); table.append(head);
  for (const account of result.accounts) { const row = element('tr', ''); row.append(element('td', account.username), element('td', account.password), element('td', account.disabled ? '停止中' : '有効')); table.append(row); }
  $('test-credentials').replaceChildren(table); $('test-credentials').hidden = false;
  message(`${result.accounts.length}件のテスト用ログイン情報を表示しました。`);
}
$('load-test-accounts').addEventListener('click', () => showTestAccounts().catch(e => message(e.message, true)));
$('test-form').addEventListener('submit', async event => { event.preventDefault(); try { await api('/admin/test-accounts', 'POST', { username: $('test-name').value }); await refresh(); await showTestAccounts(); } catch(e) { message(e.message, true); } });
