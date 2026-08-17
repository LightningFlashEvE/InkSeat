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

document.addEventListener('DOMContentLoaded', async () => {
    setupDevicePage();
    document.addEventListener('visibilitychange', handleVisibilityChange);

    try {
        if (typeof requireAuth === 'function') {
            const user = await requireAuth();
            if (!user) return;
        }

        await loadDevices();
        startPolling();
        log('系统初始化完成');
    } catch (error) {
        console.error('设备页面初始化失败:', error);
        log(error?.message || '认证服务暂不可用，请稍后重试', 'error');
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

    const pairingCodeInput = document.getElementById('pairingCode');
    if (pairingCodeInput) {
        pairingCodeInput.addEventListener('input', () => {
            pairingCodeInput.value = pairingCodeInput.value.replace(/\D/g, '').slice(0, 6);
        });
        pairingCodeInput.addEventListener('keypress', (event) => {
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
        const response = await authFetch(`${API_BASE}/api/devices/list`, {
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
    const pairingCodeInput = document.getElementById('pairingCode');
    const submitButton = document.getElementById('addDeviceSubmit');
    if (!deviceIdInput || !deviceNameInput || !pairingCodeInput) return;

    let deviceId = deviceIdInput.value.trim().toUpperCase();
    const deviceName = deviceNameInput.value.trim() || deviceId;
    const pairingCode = pairingCodeInput.value.trim();

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

    if (!/^\d{6}$/.test(pairingCode)) {
        log('请输入设备屏幕显示的6位配对码', 'error');
        pairingCodeInput.focus();
        pairingCodeInput.select();
        return;
    }

    try {
        if (submitButton) {
            submitButton.disabled = true;
            submitButton.textContent = '验证并添加中...';
        }

        const response = await authFetch(`${API_BASE}/api/devices/add`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({
                deviceId,
                deviceName,
                pairingCode
            })
        });

        const result = await response.json().catch(() => ({}));

        if (response.ok && result.success) {
            log(`设备 ${deviceName} 添加成功`, 'success');
            deviceIdInput.value = '';
            deviceNameInput.value = '';
            pairingCodeInput.value = '';
            hideAddDeviceModal();
            selectedDeviceId = deviceId;
            await loadDevices();
        } else {
            throw new Error(result.error || `添加设备失败（HTTP ${response.status}）`);
        }
    } catch (error) {
        console.error('添加设备错误:', error);
        log('添加设备失败: ' + error.message, 'error');
        pairingCodeInput.value = '';
        pairingCodeInput.focus();
    } finally {
        if (submitButton) {
            submitButton.disabled = false;
            submitButton.textContent = '验证并添加';
        }
    }
}

async function removeDevice(deviceId) {
    if (!confirm(`确定要删除设备 ${deviceId} 吗？`)) {
        return;
    }

    try {
        const response = await authFetch(`${API_BASE}/api/devices/${deviceId}`, {
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
                        <button class="meeting-table-action icon-edit" type="button" title="编辑设备屏幕与名称" aria-label="编辑设备屏幕与名称" onclick="event.stopPropagation(); openDevice('${safeId}')"></button>
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
        setText('selectedDeviceFirmware', '未上报');
        setText('selectedDeviceLocalVersion', '未上报');
        setText('selectedDeviceCloudVersion', '-');
        setText('selectedDeviceResetReason', '未上报');
        setDiagnosticState('selectedDeviceUpdateResult', null);
        setText('selectedDeviceUpdateStage', '未上报');
        setText('selectedDeviceUpdateError', '无');
        setText('selectedDeviceUpdateAt', '未上报');
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
    setText('selectedDeviceFirmware', formatFirmware(view.status));
    setText('selectedDeviceLocalVersion', formatImageVersion(view.status.localImageVersion));
    setText('selectedDeviceCloudVersion', formatImageVersion(view.status.imageVersion ?? device.imageVersion));
    setText('selectedDeviceResetReason', formatResetReason(view.status.resetReason));
    setDiagnosticState('selectedDeviceUpdateResult', view.status.lastUpdateResult, view.status.lastUpdateDurationMs);
    setText('selectedDeviceUpdateStage', formatUpdateStage(view.status.lastUpdateStage));
    setText('selectedDeviceUpdateError', formatUpdateError(view.status.lastUpdateError, view.status.gpio0StuckLow));
    setText('selectedDeviceUpdateAt', view.status.lastUpdateAt ? formatDate(view.status.lastUpdateAt) : '未上报');
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
    window.location.href = `control.html?v=20260817templateuifix1&view=device-editor&deviceId=${encodeURIComponent(deviceId)}`;
}

let pollingInterval = null;
let isPolling = false;
let pollingDisabledByAuth = false;

function startPolling() {
    if (pollingDisabledByAuth || document.hidden) return;

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

function handleAuthExpired() {
    pollingDisabledByAuth = true;
    stopPolling();
}

window.addEventListener('auth:expired', handleAuthExpired);

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
        const response = await authFetch(`${API_BASE}/api/devices`, {
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
                        firmwareVersion: device.firmwareVersion,
                        firmwareBuild: device.firmwareBuild,
                        resetReason: device.resetReason,
                        localImageVersion: device.localImageVersion,
                        gpio0StuckLow: device.gpio0StuckLow,
                        targetImageVersion: device.targetImageVersion,
                        updateAttemptId: device.updateAttemptId,
                        lastUpdateResult: device.lastUpdateResult,
                        lastUpdateStage: device.lastUpdateStage,
                        lastUpdateError: device.lastUpdateError,
                        lastUpdateDurationMs: device.lastUpdateDurationMs,
                        lastUpdateAt: device.lastUpdateAt,
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

async function runQuickAction(action) {
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

    if (action === 'reset-credentials') {
        await resetDeviceCredentials(selectedDeviceId, deviceName);
        return;
    }

    const actionNames = {
        restart: '远程重启',
        capture: '远程截屏',
        group: '设备分组'
    };
    log(`${actionNames[action] || '操作'}暂未接入设备端接口：${deviceName}`, 'warning');
}

async function resetDeviceCredentials(deviceId, deviceName) {
    const confirmation = window.prompt(
        `高风险操作：这会允许设备在短时间内用新的本地密钥替换现有凭据。\n\n` +
        `只有设备 NVS 已擦除或密钥确实丢失时才使用。解绑设备不会清除密钥。\n\n` +
        `请输入设备编号 ${deviceId} 继续：`
    );
    if (confirmation === null) return;
    if (confirmation.trim().toUpperCase() !== deviceId.toUpperCase()) {
        log('设备编号不匹配，已取消凭据重置', 'error');
        return;
    }

    try {
        const response = await authFetch(`${API_BASE}/api/device/auth/reset`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({ deviceId })
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || `开启重置窗口失败（HTTP ${response.status}）`);
        }

        const resetUntil = result.resetUntil ? new Date(result.resetUntil).toLocaleString() : '数分钟后';
        log(`已为 ${deviceName} 开启凭据重置窗口，请立即让设备联网（截止 ${resetUntil}）`, 'warning');
    } catch (error) {
        console.error('设备凭据重置失败:', error);
        log('设备凭据重置失败: ' + error.message, 'error');
    }
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

function formatFirmware(status) {
    if (!status.firmwareVersion) return '未上报';
    return status.firmwareBuild
        ? `${status.firmwareVersion} · ${status.firmwareBuild}`
        : status.firmwareVersion;
}

function formatImageVersion(version) {
    if (version === undefined || version === null) return '未上报';
    const normalized = Number(version);
    return Number.isFinite(normalized) ? String(normalized) : '未上报';
}

function formatResetReason(reason) {
    const labels = {
        POWERON: '上电',
        EXTERNAL: '外部复位',
        SOFTWARE: '软件复位',
        PANIC: '程序崩溃',
        INT_WDT: '中断看门狗',
        TASK_WDT: '任务看门狗',
        OTHER_WDT: '看门狗复位',
        DEEPSLEEP: '深睡唤醒',
        BROWNOUT: '电压不足',
        SDIO: 'SDIO复位',
        USB: 'USB复位',
        JTAG: 'JTAG复位',
        EFUSE: 'eFuse错误',
        POWER_GLITCH: '电源毛刺',
        CPU_LOCKUP: 'CPU锁死',
        UNKNOWN: '未知'
    };
    return labels[reason] || reason || '未上报';
}

function formatUpdateStage(stage) {
    const labels = {
        idle: '空闲',
        download: '下载图片',
        verify: '校验图片',
        epd_power_on: '屏幕上电',
        epd_refresh: '屏幕刷新',
        epd_power_off: '屏幕断电',
        nvs_commit: '保存版本',
        done: '完成'
    };
    return labels[stage] || '未上报';
}

function formatUpdateError(error, gpio0StuckLow) {
    if (gpio0StuckLow) return 'GPIO0 持续低电平';
    const labels = {
        none: '无',
        download_http: '网络或 HTTP 错误',
        download_timeout: '图片下载超时',
        size_mismatch: '图片长度不匹配',
        charset_invalid: '图片字符格式错误',
        sha_mismatch: 'SHA-256 校验失败',
        spiffs_write: 'SPIFFS 写入失败',
        epd_not_bound: '屏幕显示接口未绑定',
        busy_power_on: '屏幕上电 BUSY 超时',
        busy_refresh: '屏幕刷新 BUSY 超时',
        busy_power_off: '屏幕断电 BUSY 超时',
        nvs_save: '版本写入 NVS 失败',
        interrupted: '更新过程中发生复位',
        version_expired: '请求的图片版本已过期'
    };
    return labels[error] || (error ? error : '无');
}

function setDiagnosticState(id, result, durationMs) {
    const element = document.getElementById(id);
    if (!element) return;
    const labels = {
        success: '刷新成功',
        failed: '刷新失败',
        pending: '刷新中',
        interrupted: '刷新被中断'
    };
    let text = labels[result] || '未上报';
    if (Number.isFinite(Number(durationMs)) && Number(durationMs) > 0 && result) {
        text += ` · ${(Number(durationMs) / 1000).toFixed(1)}秒`;
    }
    element.textContent = text;
    element.className = `meeting-diagnostic-state ${labels[result] ? result : 'none'}`;
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
