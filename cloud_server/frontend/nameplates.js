let devices = [];
let savedNameplateTemplates = [];
let activeNameplateDesignConfig = {};
let activeNameplateBackgroundStyle = 'formal_red';
let nameplateDispatchModalReturnFocus = null;
let parsedNameplatePeople = [];
let activeNameplatePreviewIndex = 0;
let nameplatePreviewTimer = null;
let nameplatePreviewRequestId = 0;

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

document.addEventListener('DOMContentLoaded', async () => {
    const start = async () => {
        bindNameplateInputs();
        await loadSavedNameplateTemplates();
        await loadDevices();
        log('名单下发页面初始化完成');
    };

    try {
        if (typeof requireAuth === 'function') {
            const user = await requireAuth();
            if (!user) return;
        }
        await start();
    } catch (error) {
        console.error('名单下发页面初始化失败:', error);
        log(error?.message || '认证服务暂不可用，请稍后重试', 'error');
    }
});

function bindNameplateInputs() {
    bindNameplateDispatchModal();
    bindNameplateReviewList();
    renderNameplateReviewList();
    updateNameplateCardPreviewState();

    const aiFiles = document.getElementById('nameplateAiFiles');
    if (aiFiles) {
        aiFiles.addEventListener('change', renderSelectedAiFiles);
    }
    const savedTemplateSelect = document.getElementById('nameplateSavedTemplateSelect');
    if (savedTemplateSelect) {
        savedTemplateSelect.addEventListener('change', () => {
            if (savedTemplateSelect.value) {
                applySavedNameplateTemplateById(savedTemplateSelect.value);
            } else {
                activeNameplateDesignConfig = {};
                activeNameplateBackgroundStyle = 'formal_red';
                scheduleNameplatePreview();
            }
        });
    }

    ['nameplateBatchTitle', 'nameplateBatchSubtitle'].forEach(id => {
        document.getElementById(id)?.addEventListener('input', scheduleNameplatePreview);
    });
    document.getElementById('nameplateBatchWakeInterval')
        ?.addEventListener('change', scheduleNameplatePreview);
}

function bindNameplateDispatchModal() {
    const modal = document.getElementById('nameplateDispatchModal');
    if (!modal) return;

    modal.querySelectorAll('[data-close-nameplate-dispatch]').forEach(control => {
        control.addEventListener('click', closeNameplateDispatchModal);
    });

    document.addEventListener('keydown', event => {
        if (event.key === 'Escape' && !modal.hidden) {
            closeNameplateDispatchModal();
        }
    });
}

function openNameplateDispatchModal() {
    const modal = document.getElementById('nameplateDispatchModal');
    const panel = modal?.querySelector('.meeting-modal-panel');
    if (!modal) return;

    if (modal.hidden) {
        nameplateDispatchModalReturnFocus = document.activeElement;
    }
    modal.hidden = false;
    document.body.classList.add('nameplate-modal-open');
    panel?.focus();
}

function closeNameplateDispatchModal() {
    const modal = document.getElementById('nameplateDispatchModal');
    if (!modal) return;

    modal.hidden = true;
    document.body.classList.remove('nameplate-modal-open');
    if (nameplateDispatchModalReturnFocus?.focus) {
        nameplateDispatchModalReturnFocus.focus();
    }
    nameplateDispatchModalReturnFocus = null;
}

function setNameplateDispatchModalState(text, tone) {
    const state = document.getElementById('nameplateDispatchModalState');
    if (!state) return;
    state.textContent = text;
    state.className = `nameplate-dispatch-modal-state is-${tone}`;
}

function showNameplateDispatchLoading(deviceCount, nameCount) {
    const body = document.getElementById('nameplateDispatchModalBody');
    const modal = document.getElementById('nameplateDispatchModal');
    setNameplateDispatchModalState('下发中', 'loading');
    if (modal) modal.setAttribute('aria-busy', 'true');
    if (body) {
        body.innerHTML = `
            <div class="nameplate-dispatch-loading">
                <span class="nameplate-dispatch-spinner" aria-hidden="true"></span>
                <strong>正在生成并下发铭牌</strong>
                <p>${escapeHtml(nameCount)} 个姓名 · ${escapeHtml(deviceCount)} 台目标设备</p>
            </div>
        `;
    }
    openNameplateDispatchModal();
}

function renderNameplateDispatchList(title, items, type) {
    if (!Array.isArray(items) || items.length === 0) return '';
    const visibleItems = items.slice(0, 20);
    const rows = visibleItems.map(item => {
        if (type === 'success') {
            const device = item.deviceName || item.deviceId || '设备';
            const version = item.imageVersion !== undefined ? ` · v${item.imageVersion}` : '';
            return `<li><strong>${escapeHtml(item.name || item.activeContentLabel || '铭牌')}</strong><span>${escapeHtml(device)}${escapeHtml(version)}</span></li>`;
        }
        if (type === 'failed') {
            const device = item.deviceName || item.deviceId || '设备';
            return `<li><strong>${escapeHtml(item.name || device)}</strong><span>${escapeHtml(device)} · ${escapeHtml(item.error || '处理失败')}</span></li>`;
        }
        if (type === 'skipped') {
            return `<li><strong>${escapeHtml(item)}</strong><span>没有可分配的设备</span></li>`;
        }
        const device = item.deviceName || item.deviceId || '设备';
        return `<li><strong>${escapeHtml(device)}</strong><span>没有可分配的姓名</span></li>`;
    }).join('');
    const more = items.length > visibleItems.length
        ? `<p class="nameplate-dispatch-more">另有 ${items.length - visibleItems.length} 条未展开</p>`
        : '';
    return `
        <section class="nameplate-dispatch-section is-${type}">
            <h3>${escapeHtml(title)} <span>${items.length}</span></h3>
            <ul>${rows}</ul>
            ${more}
        </section>
    `;
}

function renderNameplateDispatchModal(result) {
    const modal = document.getElementById('nameplateDispatchModal');
    const body = document.getElementById('nameplateDispatchModalBody');
    if (!body) return;

    const assignments = Array.isArray(result?.assignments) ? result.assignments : [];
    const failed = Array.isArray(result?.failed) ? result.failed : [];
    const skippedNames = Array.isArray(result?.skippedNames) ? result.skippedNames : [];
    const unassignedDevices = Array.isArray(result?.unassignedDevices) ? result.unassignedDevices : [];
    const assignedCount = Number.isFinite(Number(result?.assignedCount))
        ? Number(result.assignedCount)
        : assignments.length;
    const hasExceptions = failed.length > 0 || skippedNames.length > 0 || unassignedDevices.length > 0;

    setNameplateDispatchModalState(hasExceptions ? '部分完成' : '全部完成', hasExceptions ? 'warning' : 'success');
    if (modal) modal.setAttribute('aria-busy', 'false');
    body.innerHTML = `
        <div class="nameplate-dispatch-summary">
            <div class="is-success"><span>成功</span><strong>${assignedCount}</strong></div>
            <div class="is-failed"><span>失败</span><strong>${failed.length}</strong></div>
            <div><span>姓名未分配</span><strong>${skippedNames.length}</strong></div>
            <div><span>设备未分配</span><strong>${unassignedDevices.length}</strong></div>
        </div>
        <div class="nameplate-dispatch-notice ${result?.deadlineReached ? 'is-warning' : ''}">
            ${result?.deadlineReached ? '批量处理达到时间上限，未处理项目可再次下发。' : '下发内容已保存，设备将在下次唤醒时刷新。'}
        </div>
        ${renderNameplateDispatchList('下发成功', assignments, 'success')}
        ${renderNameplateDispatchList('下发失败', failed, 'failed')}
        ${renderNameplateDispatchList('未分配姓名', skippedNames, 'skipped')}
        ${renderNameplateDispatchList('未分配设备', unassignedDevices, 'unassigned')}
    `;
    openNameplateDispatchModal();
}

function renderNameplateDispatchFailure(message, result = {}) {
    const modal = document.getElementById('nameplateDispatchModal');
    const body = document.getElementById('nameplateDispatchModalBody');
    const failed = Array.isArray(result?.failed) ? result.failed : [];
    setNameplateDispatchModalState('下发失败', 'error');
    if (modal) modal.setAttribute('aria-busy', 'false');
    if (body) {
        body.innerHTML = `
            <div class="nameplate-dispatch-notice is-error">${escapeHtml(message || '名单下发失败，请稍后重试')}</div>
            ${renderNameplateDispatchList('失败项目', failed, 'failed')}
        `;
    }
    openNameplateDispatchModal();
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
        const response = await authFetch(`${API_BASE}/api/devices/list`, {
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
    return parsePeopleInput().map(person => person.name);
}

function normalizeNameplatePerson(person) {
    if (typeof person === 'string') person = { name: person };
    const source = person && typeof person === 'object' ? person : {};
    return {
        name: String(source.name || source.fullName || '').trim(),
        title: String(source.title || source.position || '').trim(),
        subtitle: String(source.subtitle || source.company || source.companyName || '').trim(),
    };
}

function parsePeopleInput() {
    return parsedNameplatePeople
        .map(normalizeNameplatePerson)
        .filter(person => person.name);
}

function syncNameplateNamesInput() {
    const namesInput = document.getElementById('nameplateNamesInput');
    if (namesInput) {
        namesInput.value = parsedNameplatePeople.map(person => person.name || '').join('\n');
    }
}

function setParsedNameplatePeople(people, fallbackNames = []) {
    const source = Array.isArray(people) && people.length ? people : fallbackNames;
    parsedNameplatePeople = Array.isArray(source)
        ? source.map(normalizeNameplatePerson).filter(person => person.name)
        : [];
    activeNameplatePreviewIndex = 0;
    syncNameplateNamesInput();
    renderNameplateReviewList();
    updateNameplateDispatchHint();
    scheduleNameplatePreview(true);
}

function bindNameplateReviewList() {
    const container = document.getElementById('nameplateNameReviewList');
    if (!container) return;

    container.addEventListener('input', event => {
        const input = event.target.closest('[data-nameplate-field]');
        if (!input) return;
        const index = Number(input.dataset.nameplateIndex);
        const field = input.dataset.nameplateField;
        if (!Number.isInteger(index) || index < 0 || index >= parsedNameplatePeople.length) return;
        if (!['name', 'title', 'subtitle'].includes(field)) return;

        parsedNameplatePeople[index][field] = input.value;
        activeNameplatePreviewIndex = Math.max(
            0,
            parsedNameplatePeople.slice(0, index + 1)
                .filter(person => String(person.name || '').trim()).length - 1,
        );
        syncNameplateNamesInput();
        updateNameplateDispatchHint();
        updateNameplateCardPreviewState();
        scheduleNameplatePreview();
    });

    container.addEventListener('focusin', event => {
        const input = event.target.closest('[data-nameplate-field]');
        if (!input) return;
        const index = Number(input.dataset.nameplateIndex);
        if (!Number.isInteger(index) || index < 0 || index >= parsedNameplatePeople.length) return;
        const confirmedIndex = Math.max(
            0,
            parsedNameplatePeople.slice(0, index + 1)
                .filter(person => String(person.name || '').trim()).length - 1,
        );
        if (activeNameplatePreviewIndex !== confirmedIndex) {
            activeNameplatePreviewIndex = confirmedIndex;
            updateNameplateCardPreviewState();
            scheduleNameplatePreview(true);
        }
    });

    container.addEventListener('click', event => {
        const removeButton = event.target.closest('[data-remove-nameplate-index]');
        if (!removeButton) return;
        const index = Number(removeButton.dataset.removeNameplateIndex);
        if (!Number.isInteger(index) || index < 0 || index >= parsedNameplatePeople.length) return;

        parsedNameplatePeople.splice(index, 1);
        activeNameplatePreviewIndex = Math.min(
            activeNameplatePreviewIndex,
            Math.max(0, parsePeopleInput().length - 1),
        );
        syncNameplateNamesInput();
        renderNameplateReviewList();
        updateNameplateDispatchHint();
        scheduleNameplatePreview(true);
    });
}

function renderNameplateReviewList() {
    const container = document.getElementById('nameplateNameReviewList');
    if (!container) return;

    if (parsedNameplatePeople.length === 0) {
        container.innerHTML = '<div class="nameplate-review-empty">请先在上方输入内容并解析</div>';
        return;
    }

    container.innerHTML = parsedNameplatePeople.map((person, index) => `
        <div class="nameplate-name-review-row" role="listitem">
            <span class="nameplate-review-order">${index + 1}</span>
            <div class="nameplate-review-fields">
                <label>
                    <span>姓名</span>
                    <input id="nameplateReviewName${index}" type="text" value="${escapeHtml(person.name)}" maxlength="64"
                        data-nameplate-index="${index}" data-nameplate-field="name" autocomplete="off">
                </label>
                <label>
                    <span>职位</span>
                    <input type="text" value="${escapeHtml(person.title)}" maxlength="40" placeholder="使用统一职位"
                        data-nameplate-index="${index}" data-nameplate-field="title" autocomplete="off">
                </label>
                <label>
                    <span>公司</span>
                    <input type="text" value="${escapeHtml(person.subtitle)}" maxlength="40" placeholder="使用统一公司"
                        data-nameplate-index="${index}" data-nameplate-field="subtitle" autocomplete="off">
                </label>
            </div>
            <button type="button" class="nameplate-review-remove" data-remove-nameplate-index="${index}"
                aria-label="删除 ${escapeHtml(person.name || `第 ${index + 1} 个姓名`)}">删除</button>
        </div>
    `).join('');
}

function addNameplateReviewRow() {
    parsedNameplatePeople.push({ name: '', title: '', subtitle: '' });
    activeNameplatePreviewIndex = Math.max(0, parsePeopleInput().length - 1);
    syncNameplateNamesInput();
    renderNameplateReviewList();
    updateNameplateDispatchHint();
    updateNameplateCardPreviewState();

    const input = document.querySelector(`[data-nameplate-index="${parsedNameplatePeople.length - 1}"][data-nameplate-field="name"]`);
    input?.focus();
}

function getCurrentTemplateConfig() {
    return {
        backgroundStyle: activeNameplateBackgroundStyle,
        title: document.getElementById('nameplateBatchTitle')?.value?.trim() || '',
        subtitle: document.getElementById('nameplateBatchSubtitle')?.value?.trim() || '',
        sleepIntervalSeconds: parseInt(document.getElementById('nameplateBatchWakeInterval')?.value || '43200', 10),
        ...activeNameplateDesignConfig,
    };
}

function getEffectiveTemplateConfig(person) {
    const config = getCurrentTemplateConfig();
    const normalized = normalizeNameplatePerson(person);
    return {
        ...config,
        title: normalized.title || config.title || '',
        subtitle: normalized.subtitle || config.subtitle || '',
    };
}

function pickNameplateDesignConfig(config) {
    const source = config && typeof config === 'object' ? config : {};
    const designConfig = {};
    ['logoDataUrl', 'logoFileName', 'logoX', 'logoY', 'companyX'].forEach(key => {
        if (source[key] !== undefined && source[key] !== null && source[key] !== '') {
            designConfig[key] = source[key];
        }
    });
    return designConfig;
}

async function loadSavedNameplateTemplates() {
    try {
        const response = await authFetch(`${API_BASE}/api/nameplate/templates`, {
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
    activeNameplateDesignConfig = pickNameplateDesignConfig(config);
    activeNameplateBackgroundStyle = config.backgroundStyle || 'formal_red';

    const titleInput = document.getElementById('nameplateBatchTitle');
    const subtitleInput = document.getElementById('nameplateBatchSubtitle');
    const wakeSelect = document.getElementById('nameplateBatchWakeInterval');
    const savedTemplateSelect = document.getElementById('nameplateSavedTemplateSelect');

    if (titleInput && config.title !== undefined) titleInput.value = config.title || '';
    if (subtitleInput && config.subtitle !== undefined) subtitleInput.value = config.subtitle || '';
    if (wakeSelect && config.sleepIntervalSeconds) wakeSelect.value = String(config.sleepIntervalSeconds);
    if (savedTemplateSelect) savedTemplateSelect.value = templateId;
    scheduleNameplatePreview(true);
}

function applyParsedDraft(parsed) {
    if (!parsed) return;

    setParsedNameplatePeople(parsed.people, parsed.names);

    const config = parsed.templateConfig || {};
    const titleInput = document.getElementById('nameplateBatchTitle');
    const subtitleInput = document.getElementById('nameplateBatchSubtitle');
    const wakeSelect = document.getElementById('nameplateBatchWakeInterval');

    if (titleInput && config.title !== undefined) titleInput.value = config.title || '';
    if (subtitleInput && config.subtitle !== undefined) subtitleInput.value = config.subtitle || '';
    if (wakeSelect && config.sleepIntervalSeconds) wakeSelect.value = String(config.sleepIntervalSeconds);
    if (config.backgroundStyle) activeNameplateBackgroundStyle = config.backgroundStyle;
    activeNameplateDesignConfig = {
        ...activeNameplateDesignConfig,
        ...pickNameplateDesignConfig(config),
    };

    updateNameplateDispatchHint();
    renderParsedPreview(parsed);
    scheduleNameplatePreview(true);
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
        log('请输入文字或上传图片、Word、表格后再解析', 'error');
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

        const response = await authFetch(`${API_BASE}/api/nameplates/parse`, {
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
            parseButton.textContent = originalText || '解析名单';
        }
    }
}

function renderParsedPreview(parsed) {
    const preview = document.getElementById('nameplateParsedPreview');
    if (!preview) return;

    const people = parsed?.people || parsed?.names || [];
    const warnings = parsed?.warnings || [];
    const sourceText = parsed?.sourceSummary || (parsed?.aiUsed ? 'AI解析结果' : '本地规则解析');
    const warningHtml = warnings.length ? `
        <div class="activity-row warning">
            <strong>注意</strong>
            <span>${warnings.map(escapeHtml).join('；')}</span>
        </div>
    ` : '';

    preview.innerHTML = `
        <div class="activity-row">
            <strong>已解析 ${people.length} 人</strong>
            <span>${escapeHtml(sourceText)} · 请在下方核对姓名、职位、公司和预览效果</span>
        </div>
        ${warningHtml}
    `;
}

function updateNameplateCardPreviewState() {
    const people = parsePeopleInput();
    const position = document.getElementById('nameplatePreviewPosition');
    const nameLabel = document.getElementById('nameplatePreviewName');
    const previousButton = document.getElementById('nameplatePreviewPrev');
    const nextButton = document.getElementById('nameplatePreviewNext');

    if (people.length === 0) {
        activeNameplatePreviewIndex = 0;
        if (position) position.textContent = '尚无名单';
        if (nameLabel) nameLabel.textContent = '等待姓名';
        if (previousButton) previousButton.disabled = true;
        if (nextButton) nextButton.disabled = true;
        return;
    }

    activeNameplatePreviewIndex = Math.min(activeNameplatePreviewIndex, people.length - 1);
    if (position) position.textContent = `${activeNameplatePreviewIndex + 1} / ${people.length}`;
    if (nameLabel) nameLabel.textContent = people[activeNameplatePreviewIndex].name;
    if (previousButton) previousButton.disabled = activeNameplatePreviewIndex <= 0;
    if (nextButton) nextButton.disabled = activeNameplatePreviewIndex >= people.length - 1;
}

function moveNameplatePreview(direction) {
    const people = parsePeopleInput();
    if (people.length === 0) return;

    activeNameplatePreviewIndex = Math.max(
        0,
        Math.min(people.length - 1, activeNameplatePreviewIndex + Number(direction || 0)),
    );
    updateNameplateCardPreviewState();
    scheduleNameplatePreview(true);
}

function scheduleNameplatePreview(immediate = false) {
    if (nameplatePreviewTimer) {
        clearTimeout(nameplatePreviewTimer);
        nameplatePreviewTimer = null;
    }

    updateNameplateCardPreviewState();
    const people = parsePeopleInput();
    const image = document.getElementById('nameplateCardPreviewImage');
    const empty = document.getElementById('nameplateCardPreviewEmpty');
    if (people.length === 0) {
        if (image) {
            image.hidden = true;
            image.removeAttribute('src');
            image.alt = '';
        }
        if (empty) {
            empty.hidden = false;
            empty.textContent = '解析名单后显示实际墨水屏效果';
        }
        return;
    }

    nameplatePreviewTimer = setTimeout(
        refreshNameplateCardPreview,
        immediate ? 0 : 450,
    );
}

async function refreshNameplateCardPreview() {
    nameplatePreviewTimer = null;
    const people = parsePeopleInput();
    if (people.length === 0) return;

    activeNameplatePreviewIndex = Math.min(activeNameplatePreviewIndex, people.length - 1);
    const person = people[activeNameplatePreviewIndex];
    const name = person.name;
    const image = document.getElementById('nameplateCardPreviewImage');
    const empty = document.getElementById('nameplateCardPreviewEmpty');
    const requestId = ++nameplatePreviewRequestId;

    if (image) image.hidden = true;
    if (empty) {
        empty.hidden = false;
        empty.textContent = '正在生成墨水屏预览…';
    }

    try {
        const response = await authFetch(`${API_BASE}/api/nameplates/preview`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders(),
            },
            body: JSON.stringify({
                name,
                person,
                templateConfig: getEffectiveTemplateConfig(person),
            }),
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success || !result.previewImage) {
            throw new Error(result.error || '预览生成失败');
        }
        if (requestId !== nameplatePreviewRequestId) return;

        if (image) {
            image.src = `data:image/png;base64,${result.previewImage}`;
            image.alt = `${name} 的墨水屏铭牌预览`;
            image.hidden = false;
        }
        if (empty) empty.hidden = true;
    } catch (error) {
        if (requestId !== nameplatePreviewRequestId) return;
        if (image) image.hidden = true;
        if (empty) {
            empty.hidden = false;
            empty.textContent = error.message || '预览生成失败';
        }
    }
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
    const people = parsePeopleInput();
    const selectedDeviceIds = getSelectedNameplateDeviceIds();

    if (people.length === 0) {
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
    showNameplateDispatchLoading(selectedDeviceIds.length, people.length);

    try {
        const templateConfig = getCurrentTemplateConfig();

        const response = await authFetch(`${API_BASE}/api/nameplates/dispatch`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({
                people,
                deviceIds: selectedDeviceIds,
                templateConfig
            })
        });

        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            const dispatchError = new Error(result.error || '名单下发失败');
            dispatchError.result = result;
            throw dispatchError;
        }

        renderNameplateResult(result);
        renderNameplateDispatchModal(result);
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
        renderNameplateDispatchFailure(error.message, error.result);
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
