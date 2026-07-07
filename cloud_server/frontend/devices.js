let devices = [];
let deviceStatus = {};
let selectedDeviceId = null;
let deviceSearchTerm = '';

const API_BASE = '';
const DEVICE_POLL_INTERVAL_MS = 8000;

function authHeaders() {
    if (typeof getAuthHeaders === 'function') {
        return getAuthHeaders();
    }
    const token = localStorage.getItem('authToken');
    return token ? { Authorization: 'Bearer ' + token } : {};
}

document.addEventListener('DOMContentLoaded', () => {
    setupDevicePage();
    document.addEventListener('visibilitychange', handleVisibilityChange);

    if (typeof requireAuth === 'function') {
        requireAuth().then(() => {
            loadDevices();
            startPolling();
            log('系统初始化完成');
        });
    } else {
        loadDevices();
        startPolling();
        log('系统初始化完成');
    }
});

function setupDevicePage() {
    const searchInput = document.getElementById('deviceSearchInput');
    if (searchInput) {
        searchInput.addEventListener('input', (event) => {
            deviceSearchTerm = event.target.value.trim().toLowerCase();
            renderDevices();
        });
    }

    const openAddDeviceButton = document.getElementById('openAddDevice');
    if (openAddDeviceButton) {
        openAddDeviceButton.addEventListener('click', showAddDeviceModal);
    }

    document.querySelectorAll('[data-close-add-device]').forEach((element) => {
        element.addEventListener('click', hideAddDeviceModal);
    });

    document.querySelectorAll('[data-quick-action]').forEach((button) => {
        button.addEventListener('click', () => runQuickAction(button.dataset.quickAction));
    });

    const deviceIdInput = document.getElementById('newDeviceId');
    if (deviceIdInput) {
        deviceIdInput.addEventListener('keypress', (event) => {
            if (event.key === 'Enter') addDevice();
        });
    }

    const deviceNameInput = document.getElementById('deviceName');
    if (deviceNameInput) {
        deviceNameInput.addEventListener('keypress', (event) => {
            if (event.key === 'Enter') addDevice();
        });
    }

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') hideAddDeviceModal();
    });
}

function log(message, type = 'info') {
    const statusBar = document.getElementById('statusBar');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : type === 'warning' ? '⚠️' : 'ℹ️';
    if (statusBar) {
        statusBar.textContent = `[${timestamp}] ${emoji} ${message}`;
    }
    console.log(`[${timestamp}] ${message}`);
}

async function loadDevices() {
    try {
        const response = await fetch(`${API_BASE}/api/devices/list`, {
            headers: { ...authHeaders() }
        });

        if (!response.ok) {
            log('加载设备列表失败', 'error');
            return;
        }

        const result = await response.json();
        if (result.success) {
            devices = result.devices.map(normalizeDevice);
            ensureSelectedDevice();
            renderDevices();
            console.log('已加载设备列表:', devices);
        }
    } catch (error) {
        console.error('加载设备列表错误:', error);
        log('加载设备列表失败: ' + error.message, 'error');
    }
}

function normalizeDevice(device) {
    const addedAt = device.addedAt ? new Date(device.addedAt).getTime() : Date.now();
    return {
        id: device.deviceId,
        name: device.deviceName || device.deviceId,
        addedAt: Number.isNaN(addedAt) ? Date.now() : addedAt,
        imageVersion: Number(device.imageVersion || 0),
        activeContentMode: device.activeContentMode || 'template',
        activeContentLabel: device.activeContentLabel || '会议名牌',
        sleepIntervalSeconds: device.sleepIntervalSeconds
    };
}

async function addDevice() {
    const deviceIdInput = document.getElementById('newDeviceId');
    const deviceNameInput = document.getElementById('deviceName');
    if (!deviceIdInput || !deviceNameInput) return;

    let deviceId = deviceIdInput.value.trim().toUpperCase();
    const deviceName = deviceNameInput.value.trim() || deviceId;

    if (!deviceId) {
        log('请输入设备ID或MAC地址', 'error');
        return;
    }

    deviceId = deviceId.replace(/[-:]/g, '');

    if (!/^[0-9A-F]+$/.test(deviceId)) {
        log('设备ID格式错误，请输入十六进制MAC地址（6位或12位）', 'error');
        return;
    }

    if (deviceId.length === 6) {
        log(`识别为短设备码: ${deviceId}`);
    } else if (deviceId.length === 12) {
        log(`识别为完整MAC: ${deviceId}`);
    } else {
        log('设备ID格式错误，请输入6位或12位的MAC地址', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/devices/add`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({
                deviceId,
                deviceName
            })
        });

        const result = await response.json();

        if (result.success) {
            log(`设备 ${deviceName} 添加成功`, 'success');
            deviceIdInput.value = '';
            deviceNameInput.value = '';
            hideAddDeviceModal();
            selectedDeviceId = deviceId;
            await loadDevices();

            if (!confirm(`设备已添加\n\n设备码：${deviceId}\n\n请对照设备屏幕显示的设备码，确认是否一致？\n\n点击“确定”表示一致，点击“取消”删除此设备。`)) {
                log('检测到设备码不一致，正在删除...', 'warning');
                await fetch(`${API_BASE}/api/devices/${deviceId}`, {
                    method: 'DELETE',
                    headers: { ...authHeaders() }
                });
                selectedDeviceId = null;
                log('设备已删除，请重新核对后添加', 'error');
                await loadDevices();
            }
        } else {
            log(result.error || '添加设备失败', 'error');
        }
    } catch (error) {
        console.error('添加设备错误:', error);
        log('添加设备失败: ' + error.message, 'error');
    }
}

async function removeDevice(deviceId) {
    if (!confirm(`确定要删除设备 ${deviceId} 吗？`)) {
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/devices/${deviceId}`, {
            method: 'DELETE',
            headers: { ...authHeaders() }
        });

        const result = await response.json();

        if (result.success) {
            log('设备已删除', 'success');
            delete deviceStatus[deviceId];
            if (selectedDeviceId === deviceId) selectedDeviceId = null;
            await loadDevices();
        } else {
            log(result.error || '删除设备失败', 'error');
        }
    } catch (error) {
        console.error('删除设备错误:', error);
        log('删除设备失败: ' + error.message, 'error');
    }
}

function renderDevices() {
    updateFleetMetrics();
    ensureSelectedDevice();

    const tableBody = document.getElementById('deviceTableBody');
    if (!tableBody) return;

    const filteredDevices = getFilteredDevices();
    if (filteredDevices.length > 0 && !filteredDevices.some((device) => device.id === selectedDeviceId)) {
        selectedDeviceId = filteredDevices[0].id;
    }

    if (devices.length === 0) {
        tableBody.innerHTML = `
            <tr>
                <td colspan="6">
                    <div class="meeting-empty-inline">还没有设备</div>
                </td>
            </tr>
        `;
        setText('deviceListSummary', '0 台设备');
        renderSelectedDevice();
        return;
    }

    if (filteredDevices.length === 0) {
        tableBody.innerHTML = `
            <tr>
                <td colspan="6">
                    <div class="meeting-empty-inline">没有匹配的设备</div>
                </td>
            </tr>
        `;
        setText('deviceListSummary', `${devices.length} 台设备 / 0 个匹配`);
        renderSelectedDevice();
        return;
    }

    tableBody.innerHTML = filteredDevices.map((device) => {
        const view = getDeviceView(device);
        const selectedClass = device.id === selectedDeviceId ? ' selected' : '';
        const safeId = escapeJsString(device.id);

        return `
            <tr class="${selectedClass}" onclick="selectDevice('${safeId}')">
                <td>
                    <div class="meeting-dev-cell">
                        <span class="meeting-dev-thumb ${view.statusClass === 'offline' ? 'dark' : ''}" aria-hidden="true"></span>
                        <span>${escapeHtml(device.name)}</span>
                    </div>
                </td>
                <td>${escapeHtml(device.id)}</td>
                <td>${escapeHtml(view.content)}</td>
                <td>
                    <span class="meeting-status ${view.statusClass}">
                        <i class="meeting-dot" aria-hidden="true"></i>
                        ${view.statusText}
                    </span>
                </td>
                <td>${escapeHtml(view.lastSeenText)}</td>
                <td>
                    <div class="meeting-row-actions">
                        <button class="meeting-table-action icon-edit" type="button" title="单台编辑与下发" aria-label="单台编辑与下发" onclick="event.stopPropagation(); openDevice('${safeId}')"></button>
                        <button class="meeting-table-action icon-delete danger" type="button" title="删除设备" aria-label="删除设备" onclick="event.stopPropagation(); removeDevice('${safeId}')"></button>
                    </div>
                </td>
            </tr>
        `;
    }).join('');

    setText('deviceListSummary', `${filteredDevices.length} / ${devices.length} 台设备`);
    renderSelectedDevice();
}

function getFilteredDevices() {
    if (!deviceSearchTerm) return devices;

    return devices.filter((device) => {
        const status = deviceStatus[device.id] || {};
        const content = status.activeContentLabel || device.activeContentLabel || '';
        return [
            device.id,
            device.name,
            content,
            status.ip,
            status.remoteIp
        ].some((value) => String(value || '').toLowerCase().includes(deviceSearchTerm));
    });
}

function ensureSelectedDevice() {
    if (selectedDeviceId && devices.some((device) => device.id === selectedDeviceId)) {
        return;
    }
    selectedDeviceId = devices.length > 0 ? devices[0].id : null;
}

function selectDevice(deviceId) {
    selectedDeviceId = deviceId;
    renderDevices();
}

function getDeviceView(device) {
    const status = deviceStatus[device.id] || {};
    const isSleeping = status.sleeping === true;
    const isOnline = status.online === true;
    let statusText = '离线';
    let statusClass = 'offline';

    if (isSleeping) {
        statusText = '睡眠';
        statusClass = 'sleeping';
    } else if (isOnline) {
        statusText = '在线';
        statusClass = 'online';
    }

    const content = status.activeContentLabel || device.activeContentLabel || '会议名牌';
    const ipText = status.ip || (status.remoteIp ? `${status.remoteIp}（来源）` : '未上报');

    return {
        status,
        statusText,
        statusClass,
        content,
        ipText,
        lastSeenText: status.lastSeen ? formatWakeSummary(status) : '未上报',
        signalText: formatSignalText(status.rssi),
        sleepText: formatSleepInterval(status.sleepIntervalSeconds || status.currentSleepSeconds || device.sleepIntervalSeconds),
        estimatedWakeText: formatEstimatedWake(status)
    };
}

function updateFleetMetrics() {
    const total = devices.length;
    let online = 0;
    let sleeping = 0;
    let offline = 0;
    let using = 0;
    let latestSeen = null;

    devices.forEach((device) => {
        const status = deviceStatus[device.id] || {};
        if (status.sleeping === true) {
            sleeping += 1;
        } else if (status.online === true) {
            online += 1;
        } else {
            offline += 1;
        }

        const content = status.activeContentLabel || device.activeContentLabel || '';
        if (Number(status.imageVersion || device.imageVersion || 0) > 0 || (content && content !== '会议名牌')) {
            using += 1;
        }

        if (status.lastSeen) {
            const seen = new Date(status.lastSeen).getTime();
            if (!Number.isNaN(seen) && (latestSeen === null || seen > latestSeen)) {
                latestSeen = seen;
            }
        }
    });

    setText('fleetTotal', total);
    setText('fleetOnline', online);
    setText('fleetSleeping', sleeping);
    setText('fleetOffline', offline);
    setText('fleetUsing', using);
    setText('fleetHealthValue', total);
    setText('fleetOnlineLegend', `${online} (${formatPercent(online, total)})`);
    setText('fleetSleepingLegend', `${sleeping} (${formatPercent(sleeping, total)})`);
    setText('fleetOfflineLegend', `${offline} (${formatPercent(offline, total)})`);

    const healthRing = document.getElementById('fleetHealthRing');
    if (healthRing) {
        const onlinePct = total > 0 ? (online / total) * 100 : 0;
        const sleepingPct = total > 0 ? ((online + sleeping) / total) * 100 : 0;
        healthRing.style.setProperty('--meeting-online-end', `${onlinePct}%`);
        healthRing.style.setProperty('--meeting-sleeping-end', `${sleepingPct}%`);
    }

    const lastSync = document.getElementById('fleetLastSync');
    if (lastSync) {
        const syncText = new Date().toLocaleTimeString('zh-CN', {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
        lastSync.textContent = latestSeen ? `最近上报 ${formatDate(latestSeen)} / 同步 ${syncText}` : `同步 ${syncText}`;
    }

    renderFleetActivity();
}

function renderFleetActivity() {
    const container = document.getElementById('fleetActivity');
    if (!container) return;

    const rows = devices
        .map((device) => ({ device, status: deviceStatus[device.id] || {} }))
        .filter((item) => item.status.lastSeen)
        .sort((a, b) => new Date(b.status.lastSeen).getTime() - new Date(a.status.lastSeen).getTime())
        .slice(0, 5);

    if (rows.length === 0) {
        container.innerHTML = `
            <div class="activity-row">
                <strong>暂无设备上报</strong>
                <span>添加设备后会显示最近唤醒状态</span>
            </div>
        `;
        return;
    }

    container.innerHTML = rows.map(({ device, status }) => `
        <div class="activity-row">
            <strong>${escapeHtml(device.name || device.id)}</strong>
            <span>${escapeHtml(formatWakeSummary(status))} · ${escapeHtml(status.activeContentLabel || '会议名牌')}</span>
        </div>
    `).join('');
}

function renderSelectedDevice() {
    const device = devices.find((item) => item.id === selectedDeviceId);
    if (!device) {
        setText('selectedDeviceName', '未选择');
        setText('selectedDeviceId', '-');
        setText('selectedDeviceContent', '-');
        setText('selectedDeviceIp', '未上报');
        setSignalIndicator('selectedDeviceSignal', null);
        setText('selectedDeviceSleep', '默认 12小时');
        setText('selectedPreviewName', '会议牌');
        setText('selectedPreviewContent', '会议名牌');
        setText('selectedPreviewWake', '等待上报');
        return;
    }

    const view = getDeviceView(device);
    setText('selectedDeviceName', device.name);
    setText('selectedDeviceId', device.id);
    setText('selectedDeviceContent', view.content);
    setText('selectedDeviceIp', view.ipText);
    setSignalIndicator('selectedDeviceSignal', view.status.rssi);
    setText('selectedDeviceSleep', view.sleepText);
    setText('selectedPreviewName', device.name);
    setText('selectedPreviewContent', view.content);
    setText('selectedPreviewWake', view.estimatedWakeText);
}

function setText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
}

function setSignalIndicator(id, rssi) {
    const el = document.getElementById(id);
    if (!el) return;

    if (rssi === undefined || rssi === null) {
        el.textContent = '未上报';
        return;
    }

    const level = getSignalLevel(rssi);
    el.innerHTML = `
        <span class="meeting-signal-ui level-${level}" title="${escapeHtml(formatSignalText(rssi))}">
            <span class="meeting-cellular-icon" aria-hidden="true">
                <i></i><i></i><i></i><i></i>
            </span>
        </span>
    `;
}

function openDevice(deviceId) {
    window.location.href = `control.html?v=20260707auth1&deviceId=${encodeURIComponent(deviceId)}`;
}

let pollingInterval = null;
let isPolling = false;

function startPolling() {
    if (document.hidden) return;

    pollDeviceStatus();

    if (pollingInterval) {
        clearInterval(pollingInterval);
    }
    pollingInterval = setInterval(pollDeviceStatus, DEVICE_POLL_INTERVAL_MS);
}

function stopPolling() {
    if (pollingInterval) {
        clearInterval(pollingInterval);
        pollingInterval = null;
    }
}

function handleVisibilityChange() {
    if (document.hidden) {
        stopPolling();
    } else {
        startPolling();
    }
}

async function pollDeviceStatus() {
    if (isPolling) return;

    isPolling = true;
    try {
        const response = await fetch(`${API_BASE}/api/devices`, {
            headers: { ...authHeaders() },
            cache: 'no-cache'
        });

        if (response.ok) {
            const result = await response.json();
            if (result.success && result.devices) {
                let hasChanges = false;

                result.devices.forEach((device) => {
                    const normalized = normalizeDevice(device);
                    const knownDevice = devices.find((item) => item.id === normalized.id);
                    if (knownDevice) {
                        ['name', 'addedAt', 'imageVersion', 'activeContentMode', 'activeContentLabel', 'sleepIntervalSeconds'].forEach((key) => {
                            if (knownDevice[key] !== normalized[key]) {
                                knownDevice[key] = normalized[key];
                                hasChanges = true;
                            }
                        });
                    } else {
                        devices.push(normalized);
                        hasChanges = true;
                    }

                    const oldStatus = deviceStatus[device.deviceId];
                    const newStatus = {
                        online: device.online !== undefined ? device.online : true,
                        sleeping: device.sleeping !== undefined ? device.sleeping : false,
                        rssi: device.rssi,
                        ip: device.ip,
                        remoteIp: device.remoteIp,
                        lastWakeType: device.lastWakeType,
                        lastWakeCause: device.lastWakeCause,
                        currentSleepSeconds: device.currentSleepSeconds,
                        sleepIntervalSeconds: device.sleepIntervalSeconds,
                        activeContentMode: device.activeContentMode,
                        activeTemplateId: device.activeTemplateId,
                        activeContentLabel: device.activeContentLabel,
                        estimatedNextAutoWakeAt: device.estimatedNextAutoWakeAt,
                        wakePolicyPending: device.wakePolicyPending,
                        imageVersion: device.imageVersion,
                        lastSeen: device.lastSeen
                    };

                    if (!oldStatus || JSON.stringify(oldStatus) !== JSON.stringify(newStatus)) {
                        hasChanges = true;
                    }

                    deviceStatus[device.deviceId] = newStatus;
                });

                if (hasChanges) {
                    ensureSelectedDevice();
                    renderDevices();
                } else {
                    updateFleetMetrics();
                    renderSelectedDevice();
                }
            }
        }
    } catch (error) {
        console.error('轮询失败:', error);
    } finally {
        isPolling = false;
    }
}

function showAddDeviceModal() {
    const modal = document.getElementById('addDeviceModal');
    if (!modal) return;
    modal.hidden = false;
    document.body.classList.add('modal-open');
    const input = document.getElementById('newDeviceId');
    if (input) {
        setTimeout(() => input.focus(), 0);
    }
}

function hideAddDeviceModal() {
    const modal = document.getElementById('addDeviceModal');
    if (!modal || modal.hidden) return;
    modal.hidden = true;
    document.body.classList.remove('modal-open');
}

function runQuickAction(action) {
    if (!selectedDeviceId) {
        log('请先选择设备', 'error');
        return;
    }

    const device = devices.find((item) => item.id === selectedDeviceId);
    const deviceName = device ? device.name : selectedDeviceId;

    if (action === 'push') {
        openDevice(selectedDeviceId);
        return;
    }

    const actionNames = {
        restart: '远程重启',
        capture: '远程截屏',
        group: '设备分组'
    };
    log(`${actionNames[action] || '操作'}暂未接入设备端接口：${deviceName}`, 'warning');
}

function getSignalBars(rssi) {
    if (rssi === undefined || rssi === null) return '未上报';

    let bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -80) bars = 1;

    let result = '<span class="signal-strength">';
    for (let i = 1; i <= 4; i++) {
        const height = i * 3 + 5;
        const active = i <= bars ? 'active' : '';
        result += `<span class="signal-bar ${active}" style="height: ${height}px"></span>`;
    }

    result += `</span> ${rssi} dBm`;
    return result;
}

function formatSignalText(rssi) {
    if (rssi === undefined || rssi === null) return '未上报';
    if (rssi > -50) return `${rssi} dBm / 强`;
    if (rssi > -65) return `${rssi} dBm / 良好`;
    if (rssi > -78) return `${rssi} dBm / 较弱`;
    return `${rssi} dBm / 弱`;
}

function getSignalLevel(rssi) {
    if (rssi === undefined || rssi === null) return 0;
    if (rssi > -50) return 4;
    if (rssi > -65) return 3;
    if (rssi > -78) return 2;
    return 1;
}

function formatSleepInterval(seconds) {
    if (seconds === undefined || seconds === null) return '默认 12小时';
    if (seconds <= 0) return '默认 12小时';

    if (seconds % 86400 === 0) return `${seconds / 86400}天`;
    if (seconds % 3600 === 0) return `${seconds / 3600}小时`;
    if (seconds % 60 === 0) return `${seconds / 60}分钟`;
    return `${seconds}秒`;
}

function formatEstimatedWake(status) {
    if (!status.estimatedNextAutoWakeAt) return '等待设备上报';
    const suffix = status.wakePolicyPending ? '（待同步）' : '';
    return `${formatDate(status.estimatedNextAutoWakeAt)}${suffix}`;
}

function formatDate(timestamp) {
    if (!timestamp) return '未上报';
    const date = new Date(timestamp);
    if (Number.isNaN(date.getTime())) return '未上报';

    return date.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit'
    });
}

function formatWakeSummary(status) {
    if (!status.lastSeen) return '未上报';

    const typeLabels = {
        manual: '手动',
        auto: '自动',
        reset: '上电/复位',
        other: '其他'
    };
    const typeText = typeLabels[status.lastWakeType] || '未知';
    return `${formatDate(status.lastSeen)}（${typeText}）`;
}

function formatPercent(value, total) {
    if (!total) return '0%';
    const percent = (value / total) * 100;
    return `${Number.isInteger(percent) ? percent : percent.toFixed(1)}%`;
}

function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, (match) => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    }[match]));
}

function escapeJsString(value) {
    return String(value ?? '').replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

function logout() {
    if (confirm('确定要退出登录吗？')) {
        if (typeof clearAuth === 'function') {
            clearAuth();
        } else {
            localStorage.removeItem('authToken');
            localStorage.removeItem('authUser');
        }
        window.location.href = 'login.html?v=20260707auth1';
    }
}
