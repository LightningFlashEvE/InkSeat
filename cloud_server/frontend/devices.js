// 全局变量
let devices = [];
let deviceStatus = {};

// API 基础地址（前后端分离时，API通过nginx代理到后端）
const API_BASE = '';

function authHeaders() {
    if (typeof getAuthHeaders === 'function') {
        return getAuthHeaders();
    }
    const token = localStorage.getItem('authToken');
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    // 需要先检查是否已登录
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

// 日志函数
function log(message, type = 'info') {
    const statusBar = document.getElementById('statusBar');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    statusBar.textContent = `[${timestamp}] ${emoji} ${message}`;
    console.log(`[${timestamp}] ${message}`);
}

// 从服务器加载设备列表
async function loadDevices() {
    try {
        const response = await fetch(`${API_BASE}/api/devices/list`, {
            headers: {
                ...authHeaders()
            }
        });
        if (response.ok) {
            const result = await response.json();
            if (result.success) {
                devices = result.devices.map(device => ({
                    id: device.deviceId,
                    name: device.deviceName || device.deviceId,
                    addedAt: device.addedAt ? new Date(device.addedAt).getTime() : Date.now()
                }));
                console.log('已加载设备列表:', devices);
                renderDevices();
            }
        } else {
            log('加载设备列表失败', 'error');
        }
    } catch (error) {
        console.error('加载设备列表错误:', error);
        log('加载设备列表失败: ' + error.message, 'error');
    }
}

// 添加设备
async function addDevice() {
    const deviceIdInput = document.getElementById('newDeviceId');
    const deviceNameInput = document.getElementById('deviceName');

    let deviceId = deviceIdInput.value.trim().toUpperCase();
    const deviceName = deviceNameInput.value.trim() || deviceId;

    if (!deviceId) {
        log('请输入设备ID或MAC地址', 'error');
        return;
    }

    // 去掉可能的分隔符
    deviceId = deviceId.replace(/[-:]/g, '');

    // 验证是否为十六进制
    if (!/^[0-9A-F]+$/.test(deviceId)) {
        log('设备ID格式错误，请输入十六进制MAC地址（6位或12位）', 'error');
        return;
    }

    // 根据长度验证
    if (deviceId.length === 6) {
        log(`识别为短设备码: ${deviceId}`, 'info');
    } else if (deviceId.length === 12) {
        log(`识别为完整MAC: ${deviceId}`, 'info');
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
                deviceId: deviceId,
                deviceName: deviceName
            })
        });

        const result = await response.json();

        if (result.success) {
            log(`设备 ${deviceName} 添加成功`, 'success');
            // 清空输入
            deviceIdInput.value = '';
            deviceNameInput.value = '';
            // 重新加载设备列表
            await loadDevices();
            // 提示用户核对设备码
            if (!confirm(`✅ 设备已添加\n\n设备码：${deviceId}\n\n请对照设备屏幕显示的设备码，确认是否一致？\n\n点击"确定"表示一致，点击"取消"删除此设备。`)) {
                log('检测到设备码不一致，正在删除...', 'warning');
                await fetch(`${API_BASE}/api/devices/${deviceId}`, {
                    method: 'DELETE',
                    headers: { ...authHeaders() }
                });
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

// 删除设备
async function removeDevice(deviceId) {
    if (!confirm(`确定要删除设备 ${deviceId} 吗？`)) {
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/devices/${deviceId}`, {
            method: 'DELETE',
            headers: {
                ...authHeaders()
            }
        });

        const result = await response.json();

        if (result.success) {
            log('设备已删除', 'success');
            delete deviceStatus[deviceId];
            // 重新加载设备列表
            await loadDevices();
        } else {
            log(result.error || '删除设备失败', 'error');
        }
    } catch (error) {
        console.error('删除设备错误:', error);
        log('删除设备失败: ' + error.message, 'error');
    }
}

// 渲染设备列表
function renderDevices() {
    const container = document.getElementById('devicesContainer');

    if (devices.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <h3>📭 还没有设备</h3>
                <p>点击上方添加设备按钮，输入ESP32的设备ID来添加设备</p>
            </div>
        `;
        return;
    }

    container.innerHTML = '<div class="devices-grid"></div>';
    const grid = container.querySelector('.devices-grid');

    devices.forEach(device => {
        const status = deviceStatus[device.id] || {};
        const isSleeping = status.sleeping === true;  // 明确检查是否为true
        // 在线/睡眠窗口由后端统一计算，前端只负责展示，避免与后端时间窗口不一致。
        const isOnline = status.online === true;
        const hasTelemetry = !!(
            status.lastSeen ||
            status.lastManualWake ||
            status.lastAutoWake ||
            status.ip ||
            status.remoteIp ||
            status.rssi !== undefined ||
            status.uptime_ms !== undefined ||
            status.freeHeap !== undefined ||
            status.currentSleepSeconds !== undefined ||
            status.sleepIntervalSeconds !== undefined ||
            status.activeContentLabel ||
            status.estimatedNextAutoWakeAt
        );
        const ipText = status.ip || (status.remoteIp ? `${status.remoteIp}（来源）` : '未上报');
        const telemetryHtml = hasTelemetry ? `
                    <div class="device-info-item">
                        <span class="device-info-label">当前内容</span>
                        <span class="device-info-value">${status.activeContentLabel || '普通图片'}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">最后唤醒</span>
                        <span class="device-info-value">${formatWakeSummary(status)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">预计唤醒</span>
                        <span class="device-info-value">${formatEstimatedWake(status)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">手动唤醒</span>
                        <span class="device-info-value">${formatDateTime(status.lastManualWake)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">自动唤醒</span>
                        <span class="device-info-value">${formatDateTime(status.lastAutoWake)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">WiFi信号</span>
                        <span class="device-info-value">${getSignalBars(status.rssi)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">运行时间</span>
                        <span class="device-info-value">${formatUptime(status.uptime_ms)}</span>
                    </div>

                    <div class="device-info-item">
                        <span class="device-info-label">唤醒间隔</span>
                        <span class="device-info-value">${formatSleepInterval(status.sleepIntervalSeconds || status.currentSleepSeconds)}</span>
                    </div>
                ` : '';

        // 调试日志（如需开启，可在此添加受控开关；不要保留硬编码设备ID）

        // 确定显示状态：睡眠 > 在线 > 离线
        let statusText, statusColor, statusClass;
        if (isSleeping) {
            statusText = '睡眠';
            statusColor = '#ffc107';  // 黄色表示睡眠
            statusClass = 'status-sleeping';
        } else if (isOnline) {
            statusText = '在线';
            statusColor = '#28a745';  // 绿色表示在线
            statusClass = 'status-online';
        } else {
            statusText = '离线';
            statusColor = '#dc3545';  // 红色表示离线
            statusClass = 'status-offline';
        }

        const card = document.createElement('div');
        card.className = 'device-card';
        card.onclick = () => openDevice(device.id);

        card.innerHTML = `
            <div class="device-status">
                <span class="status-dot ${statusClass}"></span>
                <span style="color: ${statusColor}">
                    ${statusText}
                </span>
            </div>

            <div class="device-id">${device.name}</div>

            <div class="device-info">
                <div class="device-info-item">
                    <span class="device-info-label">设备ID</span>
                    <span class="device-info-value">${device.id}</span>
                </div>

                ${isSleeping ? `
                    <div class="device-info-item">
                        <span class="device-info-label">状态</span>
                        <span class="device-info-value" style="color: #ffc107;">💤 Deep-sleep 模式</span>
                    </div>
                    ${telemetryHtml}
                ` : isOnline ? `
                    ${telemetryHtml || `
                        <div class="device-info-item">
                            <span class="device-info-label">状态</span>
                            <span class="device-info-value">等待设备上报信息</span>
                        </div>
                    `}
                ` : `
                    <div class="device-info-item">
                        <span class="device-info-label">状态</span>
                        <span class="device-info-value" style="color: #dc3545;">设备离线</span>
                    </div>
                    ${telemetryHtml}
                `}

                <div class="device-info-item">
                    <span class="device-info-label">添加时间</span>
                    <span class="device-info-value">${formatDate(device.addedAt)}</span>
                </div>
            </div>

            <div class="device-actions" onclick="event.stopPropagation()">
                <button class="btn btn-success btn-small" onclick="openDevice('${device.id}')">
                    📱 管理设备
                </button>
                <button class="btn btn-danger btn-small" onclick="removeDevice('${device.id}')">
                    🗑️ 删除
                </button>
            </div>
        `;

        grid.appendChild(card);
    });
}

// 打开设备管理页面
function openDevice(deviceId) {
    window.location.href = `control.html?deviceId=${encodeURIComponent(deviceId)}`;
}

// HTTP轮询
let pollingInterval = null;
let isPolling = false;  // 防止并发请求

function startPolling() {
    // 立即执行一次
    pollDeviceStatus();

    // 每2秒轮询一次（从5秒优化为2秒，提升响应速度）
    if (pollingInterval) {
        clearInterval(pollingInterval);
    }
    pollingInterval = setInterval(pollDeviceStatus, 2000);
}

function stopPolling() {
    if (pollingInterval) {
        clearInterval(pollingInterval);
        pollingInterval = null;
    }
}

async function pollDeviceStatus() {
    // 防止并发请求
    if (isPolling) {
        return;
    }

    isPolling = true;
    try {
        const response = await fetch(`${API_BASE}/api/devices`, {
            headers: {
                ...authHeaders()
            },
            cache: 'no-cache'  // 禁用缓存，确保获取最新状态
        });
        if (response.ok) {
            const result = await response.json();
            if (result.success && result.devices) {
                // 更新设备状态
                let hasChanges = false;
                result.devices.forEach(device => {
                    const oldStatus = deviceStatus[device.deviceId];
                    const newStatus = {
                        online: device.online !== undefined ? device.online : true,
                        sleeping: device.sleeping !== undefined ? device.sleeping : false,  // 保存睡眠状态
                        rssi: device.rssi,
                        ip: device.ip,
                        remoteIp: device.remoteIp,
                        lastWakeType: device.lastWakeType,
                        lastWakeCause: device.lastWakeCause,
                        lastManualWake: device.lastManualWake,
                        lastAutoWake: device.lastAutoWake,
                        uptime_ms: device.uptime_ms,
                        freeHeap: device.freeHeap,
                        currentSleepSeconds: device.currentSleepSeconds,
                        sleepIntervalSeconds: device.sleepIntervalSeconds,
                        activeContentMode: device.activeContentMode,
                        activeTemplateId: device.activeTemplateId,
                        activeContentLabel: device.activeContentLabel,
                        estimatedNextAutoWakeAt: device.estimatedNextAutoWakeAt,
                        wakePolicyPending: device.wakePolicyPending,
                        lastSeen: device.lastSeen
                    };

                    // 调试：打印睡眠设备的状态
                    if (newStatus.sleeping || (oldStatus && oldStatus.sleeping)) {
                        console.log(`[设备状态] ${device.deviceId}:`, {
                            online: newStatus.online,
                            sleeping: newStatus.sleeping,
                            lastSeen: newStatus.lastSeen,
                            oldSleeping: oldStatus ? oldStatus.sleeping : 'N/A',
                            deviceData: device
                        });
                    }

                    // 检查是否有变化，避免不必要的重渲染
                    // 注意：sleeping状态变化必须触发重渲染，因为会影响显示
                    if (!oldStatus ||
                        oldStatus.online !== newStatus.online ||
                        oldStatus.sleeping !== newStatus.sleeping ||  // 检查睡眠状态变化
                        oldStatus.lastSeen !== newStatus.lastSeen ||  // lastSeen变化也可能影响状态
                        oldStatus.rssi !== newStatus.rssi ||
                        oldStatus.ip !== newStatus.ip ||
                        oldStatus.remoteIp !== newStatus.remoteIp ||
                        oldStatus.lastWakeType !== newStatus.lastWakeType ||
                        oldStatus.lastWakeCause !== newStatus.lastWakeCause ||
                        oldStatus.lastManualWake !== newStatus.lastManualWake ||
                        oldStatus.lastAutoWake !== newStatus.lastAutoWake ||
                        oldStatus.uptime_ms !== newStatus.uptime_ms ||
                        oldStatus.freeHeap !== newStatus.freeHeap ||
                        oldStatus.currentSleepSeconds !== newStatus.currentSleepSeconds ||
                        oldStatus.sleepIntervalSeconds !== newStatus.sleepIntervalSeconds ||
                        oldStatus.activeContentMode !== newStatus.activeContentMode ||
                        oldStatus.activeTemplateId !== newStatus.activeTemplateId ||
                        oldStatus.activeContentLabel !== newStatus.activeContentLabel ||
                        oldStatus.estimatedNextAutoWakeAt !== newStatus.estimatedNextAutoWakeAt ||
                        oldStatus.wakePolicyPending !== newStatus.wakePolicyPending) {
                        hasChanges = true;
                    }

                    deviceStatus[device.deviceId] = newStatus;
                });

                // 只在有变化时重新渲染，提升性能
                if (hasChanges) {
                    renderDevices();
                }
            }
        }
    } catch (e) {
        console.error('轮询失败:', e);
    } finally {
        isPolling = false;
    }
}

// 工具函数：格式化信号强度
function getSignalBars(rssi) {
    if (rssi === undefined || rssi === null) return '未上报';

    let bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -80) bars = 1;

    const html = '<span class="signal-strength">';
    let result = html;

    for (let i = 1; i <= 4; i++) {
        const height = i * 3 + 5;
        const active = i <= bars ? 'active' : '';
        result += `<span class="signal-bar ${active}" style="height: ${height}px"></span>`;
    }

    result += `</span> ${rssi} dBm`;
    return result;
}

// 工具函数：格式化运行时间
function formatUptime(ms) {
    if (ms === undefined || ms === null) return '未上报';

    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (days > 0) return `${days}天 ${hours % 24}小时`;
    if (hours > 0) return `${hours}小时 ${minutes % 60}分钟`;
    if (minutes > 0) return `${minutes}分钟`;
    return `${seconds}秒`;
}

// 工具函数：格式化内存
function formatMemory(bytes) {
    if (bytes === undefined || bytes === null) return '未上报';

    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
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

// 工具函数：格式化日期
function formatDate(timestamp) {
    const date = new Date(timestamp);
    return date.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit'
    });
}

function formatDateTime(timestamp) {
    if (!timestamp) return '未上报';
    return formatDate(timestamp);
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

// 处理回车键
document.getElementById('newDeviceId').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        addDevice();
    }
});

document.getElementById('deviceName').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        addDevice();
    }
});

// 退出登录
function logout() {
    if (confirm('确定要退出登录吗？')) {
        if (typeof clearAuth === 'function') {
            clearAuth();
        } else {
            localStorage.removeItem('authToken');
            localStorage.removeItem('authUser');
        }
        window.location.href = 'login.html';
    }
}
