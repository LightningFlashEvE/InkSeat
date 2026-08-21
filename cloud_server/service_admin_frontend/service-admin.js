const ADMIN_TOKEN_KEY = 'serviceAdminToken';
const state = {userPage: 1, userPages: 1, devicePage: 1, devicePages: 1, owner: '', selectedDevice: null, resetUsername: ''};
let searchTimer = 0;

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const token = () => sessionStorage.getItem(ADMIN_TOKEN_KEY) || '';

async function adminFetch(path, init = {}) {
    const headers = new Headers(init.headers || {});
    if (token()) headers.set('Authorization', `Bearer ${token()}`);
    const response = await fetch(path, {...init, headers});
    if (response.status === 401) {
        sessionStorage.removeItem(ADMIN_TOKEN_KEY);
        showLogin('会话已失效，请重新登录', true);
        throw new Error('会话已失效');
    }
    return response;
}

function showLogin(message = '', error = false) {
    $('#appView').classList.add('hidden');
    $('#loginView').classList.remove('hidden');
    $('#adminPassword').value = '';
    if (message) {
        $('#loginStatus').textContent = message;
        $('#loginStatus').classList.toggle('error', error);
    }
}

function showApp(admin) {
    $('#loginView').classList.add('hidden');
    $('#appView').classList.remove('hidden');
    $('#adminIdentity').textContent = admin.username;
    loadOverview();
}

function formatDate(value, includeTime = true) {
    if (!value) return '—';
    const date = typeof value === 'number' ? new Date(value) : new Date(value);
    if (Number.isNaN(date.getTime())) return '—';
    return new Intl.DateTimeFormat('zh-CN', includeTime ? {dateStyle: 'medium', timeStyle: 'short'} : {dateStyle: 'medium'}).format(date);
}

function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>'"]/g, (char) => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'}[char]));
}

function statusLabel(status) { return ({online: '在线', sleeping: '睡眠', offline: '离线'})[status] || '未知'; }

function toast(message) {
    const node = $('#toast');
    node.textContent = message;
    node.classList.remove('hidden', 'is-closing');
    clearTimeout(node._timer);
    node._timer = setTimeout(() => {
        node.classList.add('is-closing');
        setTimeout(() => node.classList.add('hidden'), 180);
    }, 2400);
}

async function readJson(response) {
    const data = await response.json().catch(() => ({}));
    if (!response.ok || !data.success) throw new Error(data.error || `请求失败（HTTP ${response.status}）`);
    return data;
}

async function loadOverview() {
    try {
        const data = await readJson(await adminFetch('/api/service-admin/overview'));
        const overview = data.overview;
        $('#statUsers').textContent = overview.users;
        $('#statDevices').textContent = overview.devices;
        $('#statOnline').textContent = overview.online;
        $('#statSleeping').textContent = overview.sleeping;
        $('#statOffline').textContent = overview.offline;
    } catch (error) { toast(error.message); }
}

async function loadUsers() {
    const params = new URLSearchParams({page: state.userPage, pageSize: 20, q: $('#userSearch').value.trim()});
    $('#usersBody').innerHTML = '<tr><td colspan="6">正在加载…</td></tr>';
    try {
        const data = await readJson(await adminFetch(`/api/service-admin/users?${params}`));
        state.userPages = data.pagination.pages;
        $('#usersCount').textContent = `共 ${data.pagination.total} 位用户`;
        $('#usersPage').textContent = `${state.userPage} / ${state.userPages}`;
        $('#usersPrev').disabled = state.userPage <= 1;
        $('#usersNext').disabled = state.userPage >= state.userPages;
        $('#usersBody').innerHTML = data.users.length ? data.users.map((user) => `
            <tr>
                <td><button class="text-button user-devices" data-owner="${escapeHtml(user.username)}">${escapeHtml(user.username)}</button></td>
                <td>${formatDate(user.createdAt)}</td><td>${formatDate(user.lastLoginAt)}</td><td>${user.deviceCount}</td>
                <td>${user.mustChangePassword ? '<span class="badge required">待改密</span>' : '<span class="badge">正常</span>'}</td>
                <td><button class="row-button reset-user" data-username="${escapeHtml(user.username)}">重置密码</button></td>
            </tr>`).join('') : '<tr><td colspan="6">没有匹配的用户</td></tr>';
    } catch (error) { $('#usersBody').innerHTML = `<tr><td colspan="6">${escapeHtml(error.message)}</td></tr>`; }
}

async function loadDevices() {
    const params = new URLSearchParams({page: state.devicePage, pageSize: 20});
    const query = $('#deviceSearch').value.trim();
    const status = $('#deviceStatus').value;
    if (query) params.set('q', query);
    if (status) params.set('status', status);
    if (state.owner) params.set('owner', state.owner);
    $('#devicesBody').innerHTML = '<tr><td colspan="6">正在加载…</td></tr>';
    try {
        const data = await readJson(await adminFetch(`/api/service-admin/devices?${params}`));
        state.devicePages = data.pagination.pages;
        $('#devicesCount').textContent = `共 ${data.pagination.total} 台设备`;
        $('#devicesPage').textContent = `${state.devicePage} / ${state.devicePages}`;
        $('#devicesPrev').disabled = state.devicePage <= 1;
        $('#devicesNext').disabled = state.devicePage >= state.devicePages;
        $('#devicesBody').innerHTML = data.devices.length ? data.devices.map((device) => `
            <tr data-device-id="${escapeHtml(device.deviceId)}">
                <td><span class="cell-main">${escapeHtml(device.deviceName)}</span><span class="cell-sub">${escapeHtml(device.deviceId)}</span></td>
                <td>${escapeHtml(device.owner || '—')}</td><td><span class="badge ${device.status}">${statusLabel(device.status)}</span></td>
                <td>${formatDate(device.lastSeen)}</td><td>${escapeHtml(device.activeContentLabel || '—')}</td>
                <td><span class="cell-main">云端 ${device.imageVersion ?? '—'}</span><span class="cell-sub">本地 ${device.localImageVersion ?? '—'}</span></td>
            </tr>`).join('') : '<tr><td colspan="6">没有匹配的设备</td></tr>';
        data.devices.forEach((device) => {
            const row = $(`#devicesBody tr[data-device-id="${CSS.escape(device.deviceId)}"]`);
            if (row) row._device = device;
        });
    } catch (error) { $('#devicesBody').innerHTML = `<tr><td colspan="6">${escapeHtml(error.message)}</td></tr>`; }
}

function showDeviceDetail(device) {
    state.selectedDevice = device;
    $$('#devicesBody tr').forEach((row) => row.classList.toggle('selected', row.dataset.deviceId === device.deviceId));
    $('#deviceDetail').innerHTML = `
        <p class="eyebrow">DEVICE DETAIL</p><h2>${escapeHtml(device.deviceName)}</h2><p class="subtle">${escapeHtml(device.deviceId)} · ${escapeHtml(device.owner)}</p>
        <div class="detail-grid">
            <div><span>运行状态</span><strong>${statusLabel(device.status)}</strong></div><div><span>最后上报</span><strong>${formatDate(device.lastSeen)}</strong></div>
            <div><span>固件版本</span><strong>${escapeHtml(device.firmwareVersion || '—')}</strong></div><div><span>固件构建</span><strong>${escapeHtml(device.firmwareBuild || '—')}</strong></div>
            <div><span>云端图片</span><strong>${device.imageVersion ?? '—'}</strong></div><div><span>本地图片</span><strong>${device.localImageVersion ?? '—'}</strong></div>
            <div><span>刷新结果</span><strong>${escapeHtml(device.lastUpdateResult || '—')}</strong></div><div><span>刷新阶段</span><strong>${escapeHtml(device.lastUpdateStage || '—')}</strong></div>
            <div class="wide"><span>刷新诊断</span><strong>${escapeHtml(device.lastUpdateError || '无错误')}</strong></div>
            <div class="wide"><span>当前内容</span><strong>${escapeHtml(device.activeContentLabel || '—')}</strong></div>
        </div>`;
}

function switchSection(name) {
    const titles = {overview: '运行概览', users: '用户管理', devices: '设备管理'};
    $$('.nav-item').forEach((item) => item.classList.toggle('active', item.dataset.section === name));
    $$('.section').forEach((section) => section.classList.toggle('active', section.id === `${name}Section`));
    $('#pageTitle').textContent = titles[name];
    if (name === 'overview') loadOverview();
    if (name === 'users') loadUsers();
    if (name === 'devices') loadDevices();
}

function openResetModal(username) {
    state.resetUsername = username;
    $('#resetUsername').textContent = username;
    $('#resetConfirmView').classList.remove('hidden');
    $('#resetResultView').classList.add('hidden');
    $('#temporaryPassword').textContent = '';
    $('#resetModal').classList.remove('hidden', 'is-closing');
}

function closeResetModal() {
    const modal = $('#resetModal');
    modal.classList.add('is-closing');
    setTimeout(() => modal.classList.add('hidden'), 180);
}

$('#loginForm').addEventListener('submit', async (event) => {
    event.preventDefault();
    $('#loginStatus').classList.remove('error');
    $('#loginStatus').textContent = '正在验证…';
    try {
        const response = await fetch('/api/service-admin/auth/login', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({username: $('#adminUsername').value.trim(), password: $('#adminPassword').value})});
        const data = await readJson(response);
        sessionStorage.setItem(ADMIN_TOKEN_KEY, data.token);
        showApp(data.admin);
    } catch (error) { $('#loginStatus').textContent = error.message; $('#loginStatus').classList.add('error'); }
});

$$('.nav-item').forEach((item) => item.addEventListener('click', () => switchSection(item.dataset.section)));
$('#logoutButton').addEventListener('click', async () => { try { await adminFetch('/api/service-admin/auth/logout', {method: 'POST'}); } catch (_) {} sessionStorage.removeItem(ADMIN_TOKEN_KEY); showLogin('已安全退出'); });
$('#usersBody').addEventListener('click', (event) => {
    const resetButton = event.target.closest('.reset-user');
    if (resetButton) openResetModal(resetButton.dataset.username);
    const devicesButton = event.target.closest('.user-devices');
    if (devicesButton) { state.owner = devicesButton.dataset.owner; state.devicePage = 1; $('#ownerName').textContent = state.owner; $('#ownerFilter').classList.remove('hidden'); switchSection('devices'); }
});
$('#devicesBody').addEventListener('click', (event) => { const row = event.target.closest('tr[data-device-id]'); if (row && row._device) showDeviceDetail(row._device); });
$('#clearOwner').addEventListener('click', () => { state.owner = ''; state.devicePage = 1; $('#ownerFilter').classList.add('hidden'); loadDevices(); });
$('#userSearch').addEventListener('input', () => { clearTimeout(searchTimer); searchTimer = setTimeout(() => { state.userPage = 1; loadUsers(); }, 250); });
$('#deviceSearch').addEventListener('input', () => { clearTimeout(searchTimer); searchTimer = setTimeout(() => { state.devicePage = 1; loadDevices(); }, 250); });
$('#deviceStatus').addEventListener('change', () => { state.devicePage = 1; loadDevices(); });
$('#usersPrev').addEventListener('click', () => { if (state.userPage > 1) { state.userPage -= 1; loadUsers(); } });
$('#usersNext').addEventListener('click', () => { if (state.userPage < state.userPages) { state.userPage += 1; loadUsers(); } });
$('#devicesPrev').addEventListener('click', () => { if (state.devicePage > 1) { state.devicePage -= 1; loadDevices(); } });
$('#devicesNext').addEventListener('click', () => { if (state.devicePage < state.devicePages) { state.devicePage += 1; loadDevices(); } });
$$('[data-close-modal]').forEach((button) => button.addEventListener('click', closeResetModal));
$('#resetModal').addEventListener('click', (event) => { if (event.target === $('#resetModal')) closeResetModal(); });
$('#confirmReset').addEventListener('click', async () => {
    const button = $('#confirmReset'); button.disabled = true; button.textContent = '正在重置…';
    try {
        const data = await readJson(await adminFetch(`/api/service-admin/users/${encodeURIComponent(state.resetUsername)}/reset-password`, {method: 'POST'}));
        $('#temporaryPassword').textContent = data.temporaryPassword;
        $('#resetConfirmView').classList.add('hidden'); $('#resetResultView').classList.remove('hidden');
        loadUsers();
    } catch (error) { toast(error.message); }
    finally { button.disabled = false; button.textContent = '确认重置'; }
});
$('#copyPassword').addEventListener('click', async () => { try { await navigator.clipboard.writeText($('#temporaryPassword').textContent); toast('临时密码已复制'); } catch (_) { toast('复制失败，请手动复制'); } });
document.addEventListener('keydown', (event) => { if (event.key === 'Escape' && !$('#resetModal').classList.contains('hidden')) closeResetModal(); });

(async function boot() {
    if (!token()) return showLogin();
    try { const data = await readJson(await adminFetch('/api/service-admin/auth/me')); showApp(data.admin); }
    catch (_) { showLogin('请重新登录', true); }
}());
