let devices = [];
let savedNameplateTemplates = [];

const API_BASE = '';
const BUILTIN_NAMEPLATE_TEMPLATES = [
    {
        templateId: '__builtin_pheno_red',
        name: 'Pheno 红色底栏',
        templateConfig: { backgroundStyle: 'formal_red', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_green',
        name: 'Pheno 绿色底栏',
        templateConfig: { backgroundStyle: 'formal_green', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_band',
        name: 'Pheno 绿色横幅',
        templateConfig: { backgroundStyle: 'plain', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_profile',
        name: 'Pheno 职务名片',
        templateConfig: { backgroundStyle: 'formal_blue', title: 'Technical Expert', subtitle: '', sleepIntervalSeconds: 43200 },
    },
];

function authHeaders() {
    if (typeof getAuthHeaders === 'function') {
        return getAuthHeaders();
    }
    const token = localStorage.getItem('authToken');
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

document.addEventListener('DOMContentLoaded', () => {
    const start = async () => {
        bindNameplateInputs();
        await loadSavedNameplateTemplates();
        loadDevices();
        log('名单下发页面初始化完成');
    };

    if (typeof requireAuth === 'function') {
        requireAuth().then(user => {
            if (user) start();
        });
    } else {
        start();
    }
});

function bindNameplateInputs() {
    const namesInput = document.getElementById('nameplateNamesInput');
    if (namesInput) {
        namesInput.addEventListener('input', updateNameplateDispatchHint);
    }
    const aiFiles = document.getElementById('nameplateAiFiles');
    if (aiFiles) {
        aiFiles.addEventListener('change', renderSelectedAiFiles);
    }
    const savedTemplateSelect = document.getElementById('nameplateSavedTemplateSelect');
    if (savedTemplateSelect) {
        savedTemplateSelect.addEventListener('change', () => {
            applySavedNameplateTemplateById(savedTemplateSelect.value);
        });
    }
}

function log(message, type = 'info') {
    const statusBar = document.getElementById('statusBar');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    if (statusBar) statusBar.textContent = `[${timestamp}] ${emoji} ${message}`;
    console.log(`[${timestamp}] ${message}`);
}

async function loadDevices() {
    try {
        const response = await fetch(`${API_BASE}/api/devices/list`, {
            headers: { ...authHeaders() }
        });
        const result = await response.json();

        if (!response.ok || !result.success) {
            throw new Error(result.error || '加载设备列表失败');
        }

        devices = (result.devices || []).map(device => ({
            id: device.deviceId,
            name: device.deviceName || device.deviceId,
            addedAt: device.addedAt ? new Date(device.addedAt).getTime() : Date.now()
        }));
        renderNameplateDeviceList();
        updateNameplateDispatchHint();
    } catch (error) {
        console.error('加载设备列表错误:', error);
        log('加载设备列表失败: ' + error.message, 'error');
        renderNameplateDeviceList();
    }
}

function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function parseNameInput() {
    const nameText = document.getElementById('nameplateNamesInput')?.value || '';
    return nameText
        .split(/[\n\r、,，;；\t]+/)
        .map(item => item.trim())
        .filter(Boolean);
}

function getCurrentTemplateConfig() {
    return {
        backgroundStyle: document.getElementById('nameplateBatchStyle')?.value || 'formal_red',
        title: document.getElementById('nameplateBatchTitle')?.value?.trim() || '',
        subtitle: document.getElementById('nameplateBatchSubtitle')?.value?.trim() || '',
        sleepIntervalSeconds: parseInt(document.getElementById('nameplateBatchWakeInterval')?.value || '43200', 10),
    };
}

async function loadSavedNameplateTemplates() {
    try {
        const response = await fetch(`${API_BASE}/api/nameplate/templates`, {
            headers: { ...authHeaders() }
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || '保存模板加载失败');
        }
        savedNameplateTemplates = Array.isArray(result.templates) ? result.templates : [];
        renderSavedNameplateTemplateSelect();
        if (savedNameplateTemplates.length) {
            applySavedNameplateTemplateById(savedNameplateTemplates[0].templateId);
        } else {
            applySavedNameplateTemplateById(BUILTIN_NAMEPLATE_TEMPLATES[0].templateId);
        }
    } catch (error) {
        savedNameplateTemplates = [];
        renderSavedNameplateTemplateSelect();
        applySavedNameplateTemplateById(BUILTIN_NAMEPLATE_TEMPLATES[0].templateId);
        console.warn('读取保存模板失败:', error);
    }
}

function renderSavedNameplateTemplateSelect() {
    const select = document.getElementById('nameplateSavedTemplateSelect');
    if (!select) return;

    const builtinOptions = BUILTIN_NAMEPLATE_TEMPLATES.map(template => (
        `<option value="${escapeHtml(template.templateId)}">${escapeHtml(template.name)}</option>`
    )).join('');
    const savedOptions = savedNameplateTemplates.map(template => (
        `<option value="${escapeHtml(template.templateId)}">${escapeHtml(template.name || '会议名牌模板')}</option>`
    )).join('');

    select.innerHTML = `
        <option value="">手动配置</option>
        <optgroup label="内置模板">${builtinOptions}</optgroup>
        <optgroup label="已保存模板">${savedOptions || '<option value="" disabled>暂无保存模板</option>'}</optgroup>
    `;
}

function applySavedNameplateTemplateById(templateId) {
    const template = [...BUILTIN_NAMEPLATE_TEMPLATES, ...savedNameplateTemplates]
        .find(item => item.templateId === templateId);
    if (!template) return;

    const config = template.templateConfig || {};

    const styleSelect = document.getElementById('nameplateBatchStyle');
    const titleInput = document.getElementById('nameplateBatchTitle');
    const subtitleInput = document.getElementById('nameplateBatchSubtitle');
    const wakeSelect = document.getElementById('nameplateBatchWakeInterval');
    const savedTemplateSelect = document.getElementById('nameplateSavedTemplateSelect');

    if (styleSelect && config.backgroundStyle) styleSelect.value = config.backgroundStyle;
    if (titleInput && config.title !== undefined) titleInput.value = config.title || '';
    if (subtitleInput && config.subtitle !== undefined) subtitleInput.value = config.subtitle || '';
    if (wakeSelect && config.sleepIntervalSeconds) wakeSelect.value = String(config.sleepIntervalSeconds);
    if (savedTemplateSelect) savedTemplateSelect.value = templateId;
}

function applyParsedDraft(parsed) {
    if (!parsed) return;

    const namesInput = document.getElementById('nameplateNamesInput');
    if (namesInput && Array.isArray(parsed.names)) {
        namesInput.value = parsed.names.join('\n');
    }

    const config = parsed.templateConfig || {};
    const styleSelect = document.getElementById('nameplateBatchStyle');
    const titleInput = document.getElementById('nameplateBatchTitle');
    const subtitleInput = document.getElementById('nameplateBatchSubtitle');
    const wakeSelect = document.getElementById('nameplateBatchWakeInterval');

    if (styleSelect && config.backgroundStyle) styleSelect.value = config.backgroundStyle;
    if (titleInput && config.title !== undefined) titleInput.value = config.title || '';
    if (subtitleInput && config.subtitle !== undefined) subtitleInput.value = config.subtitle || '';
    if (wakeSelect && config.sleepIntervalSeconds) wakeSelect.value = String(config.sleepIntervalSeconds);

    updateNameplateDispatchHint();
    renderParsedPreview(parsed);
}

function renderSelectedAiFiles() {
    const input = document.getElementById('nameplateAiFiles');
    const preview = document.getElementById('nameplateParsedPreview');
    if (!input || !preview || input.files.length === 0) return;

    const fileNames = Array.from(input.files).map(file => escapeHtml(file.name)).join('、');
    preview.innerHTML = `
        <div class="activity-row">
            <strong>已选择 ${input.files.length} 个文件</strong>
            <span>${fileNames}</span>
        </div>
    `;
}

async function parseNameplateDraft() {
    const textInput = document.getElementById('nameplateAiTextInput');
    const fileInput = document.getElementById('nameplateAiFiles');
    const parseButton = document.getElementById('nameplateAiParseBtn');
    const preview = document.getElementById('nameplateParsedPreview');
    const sourceText = textInput?.value?.trim() || '';
    const files = fileInput?.files ? Array.from(fileInput.files) : [];

    if (!sourceText && files.length === 0) {
        log('请输入文字或上传图片/表格后再解析', 'error');
        return;
    }

    const originalText = parseButton ? parseButton.textContent : '';
    if (parseButton) {
        parseButton.disabled = true;
        parseButton.textContent = '解析中...';
    }
    if (preview) {
        preview.innerHTML = `
            <div class="activity-row">
                <strong>正在解析</strong>
                <span>生成草稿后请先确认，再点击下发名单</span>
            </div>
        `;
    }

    try {
        const formData = new FormData();
        formData.append('text', sourceText);
        formData.append('templateConfig', JSON.stringify(getCurrentTemplateConfig()));
        files.forEach(file => formData.append('files', file));

        const response = await fetch(`${API_BASE}/api/nameplates/parse`, {
            method: 'POST',
            headers: { ...authHeaders() },
            body: formData
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || 'AI解析失败');
        }

        applyParsedDraft(result.parsed);
        const count = result.parsed?.names?.length || 0;
        const modeText = result.parsed?.aiUsed ? 'AI解析' : '本地规则解析';
        log(`${modeText}完成，已生成 ${count} 个姓名草稿，请确认后下发`, 'success');
    } catch (error) {
        console.error('AI解析失败:', error);
        log('AI解析失败: ' + error.message, 'error');
        if (preview) {
            preview.innerHTML = `
                <div class="activity-row">
                    <strong>解析失败</strong>
                    <span>${escapeHtml(error.message)}</span>
                </div>
            `;
        }
    } finally {
        if (parseButton) {
            parseButton.disabled = false;
            parseButton.textContent = originalText || 'AI解析为草稿';
        }
    }
}

function renderParsedPreview(parsed) {
    const preview = document.getElementById('nameplateParsedPreview');
    if (!preview) return;

    const names = parsed?.names || [];
    const warnings = parsed?.warnings || [];
    const sourceText = parsed?.sourceSummary || (parsed?.aiUsed ? 'AI解析结果' : '本地规则解析');
    const namesHtml = names.slice(0, 12).map((name, index) => `
        <div class="nameplate-preview-row">
            <span>${index + 1}</span>
            <strong>${escapeHtml(name)}</strong>
        </div>
    `).join('');
    const moreText = names.length > 12 ? `<span>另有 ${names.length - 12} 个姓名已填入下方名单</span>` : '';
    const warningHtml = warnings.length ? `
        <div class="activity-row warning">
            <strong>注意</strong>
            <span>${warnings.map(escapeHtml).join('；')}</span>
        </div>
    ` : '';

    preview.innerHTML = `
        <div class="activity-row">
            <strong>${parsed?.aiUsed ? 'AI草稿已生成' : '草稿已生成'}</strong>
            <span>${escapeHtml(sourceText)} · ${names.length} 个姓名</span>
        </div>
        <div class="nameplate-preview-list">${namesHtml || '<span>未识别到姓名</span>'}${moreText}</div>
        ${warningHtml}
    `;
}

function renderNameplateDeviceList() {
    const container = document.getElementById('nameplateDeviceList');
    if (!container) return;

    if (devices.length === 0) {
        container.innerHTML = '<div class="nameplate-device-empty">请先在设备总览中添加设备</div>';
        updateNameplateDispatchHint();
        return;
    }

    const existingInputs = Array.from(container.querySelectorAll('input[name="nameplateDevice"]'));
    const hasExistingSelection = existingInputs.length > 0;
    const selectedIds = new Set(existingInputs.filter(input => input.checked).map(input => input.value));
    const orderedDevices = [...devices].sort((a, b) => a.addedAt - b.addedAt);

    container.innerHTML = orderedDevices.map((device, index) => {
        const checked = !hasExistingSelection || selectedIds.has(device.id);
        return `
            <label class="nameplate-device-row">
                <input type="checkbox" name="nameplateDevice" value="${escapeHtml(device.id)}" ${checked ? 'checked' : ''} onchange="updateNameplateDispatchHint()">
                <span class="nameplate-device-order">${index + 1}</span>
                <span class="nameplate-device-name">${escapeHtml(device.name)}</span>
                <span class="nameplate-device-code">${escapeHtml(device.id)}</span>
            </label>
        `;
    }).join('');

    updateNameplateDispatchHint();
}

function updateNameplateDispatchHint() {
    const selectedCount = getSelectedNameplateDeviceIds().length;
    const nameCount = parseNameInput().length;
    const assignCount = Math.min(selectedCount, nameCount);

    const hint = document.getElementById('nameplateDispatchHint');
    if (hint) {
        hint.textContent = selectedCount > 0
            ? `已选 ${selectedCount} 台设备，名单约 ${nameCount} 人，可下发 ${assignCount} 个铭牌`
            : '请至少选择一台目标设备';
    }

    const selectedCountEl = document.getElementById('nameplateSelectedCount');
    if (selectedCountEl) selectedCountEl.textContent = selectedCount;

    renderNameplateSummary(nameCount, selectedCount, assignCount);
}

function renderNameplateSummary(nameCount, selectedCount, assignCount) {
    const container = document.getElementById('nameplateSummary');
    if (!container) return;

    const extraNames = Math.max(0, nameCount - selectedCount);
    const idleDevices = Math.max(0, selectedCount - nameCount);
    container.innerHTML = `
        <div class="activity-row">
            <strong>${assignCount} 个铭牌可下发</strong>
            <span>${nameCount} 个姓名 / ${selectedCount} 台设备</span>
        </div>
        <div class="activity-row">
            <strong>${extraNames} 个姓名未分配</strong>
            <span>${idleDevices} 台设备暂无姓名</span>
        </div>
    `;
}

function selectAllNameplateDevices(checked) {
    document.querySelectorAll('input[name="nameplateDevice"]').forEach(input => {
        input.checked = checked;
    });
    updateNameplateDispatchHint();
}

function getSelectedNameplateDeviceIds() {
    return Array.from(document.querySelectorAll('input[name="nameplateDevice"]:checked'))
        .map(input => input.value)
        .filter(Boolean);
}

async function dispatchNameplates() {
    const namesText = document.getElementById('nameplateNamesInput')?.value?.trim() || '';
    const selectedDeviceIds = getSelectedNameplateDeviceIds();

    if (!namesText) {
        log('请输入要下发的人名名单', 'error');
        return;
    }
    if (selectedDeviceIds.length === 0) {
        log('请选择至少一台目标设备', 'error');
        return;
    }

    const submitButton = document.querySelector('.nameplate-submit');
    const originalText = submitButton ? submitButton.textContent : '';
    if (submitButton) {
        submitButton.disabled = true;
        submitButton.textContent = '下发中...';
    }

    try {
        const templateConfig = getCurrentTemplateConfig();

        const response = await fetch(`${API_BASE}/api/nameplates/dispatch`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({
                text: namesText,
                deviceIds: selectedDeviceIds,
                templateConfig
            })
        });

        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || '名单下发失败');
        }

        renderNameplateResult(result);
        const skippedText = result.skippedNames && result.skippedNames.length
            ? `，${result.skippedNames.length} 个姓名未分配`
            : '';
        const idleText = result.unassignedDevices && result.unassignedDevices.length
            ? `，${result.unassignedDevices.length} 台设备未分配姓名`
            : '';
        const failedText = result.failed && result.failed.length
            ? `，${result.failed.length} 台失败`
            : '';

        log(`已下发 ${result.assignedCount} 个铭牌${skippedText}${idleText}${failedText}，设备下次唤醒后刷新`, 'success');
        console.log('铭牌下发结果:', result);
    } catch (error) {
        console.error('名单下发失败:', error);
        log('名单下发失败: ' + error.message, 'error');
    } finally {
        if (submitButton) {
            submitButton.disabled = false;
            submitButton.textContent = originalText || '下发名单';
        }
    }
}

function renderNameplateResult(result) {
    const container = document.getElementById('nameplateResult');
    if (!container) return;

    const assignments = result.assignments || [];
    if (assignments.length === 0) {
        container.innerHTML = `
            <div class="activity-row">
                <strong>没有成功下发</strong>
                <span>请检查名单和设备选择</span>
            </div>
        `;
        return;
    }

    container.innerHTML = assignments.slice(0, 6).map(item => `
        <div class="activity-row">
            <strong>${escapeHtml(item.name || item.activeContentLabel || '铭牌')}</strong>
            <span>${escapeHtml(item.deviceName || item.deviceId)} · v${escapeHtml(item.imageVersion || '')}</span>
        </div>
    `).join('');
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
