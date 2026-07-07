/**
 * ESP32 E-Paper Editor - 页面管理和模板功能
 * 注意：此文件依赖 app.js，必须在 app.js 之后加载
 */

// 标记 editor.js 已加载
window.editorInitialized = true;

// ==================== 全局变量 ====================
// 以下变量是 editor.js 独有的，不与 app.js 冲突
var deviceId = '';
var pages = [];
var templates = [];
var currentPageId = null;
var currentTemplateId = 'nameplate';
var activeSavedNameplateTemplateId = '';
var savedNameplateTemplates = [];
var imageCropper = null;
var imageFilePond = null;
var filePondPluginsRegistered = false;
var imageEditingAssetsPromise = null;
const EPD_CROP_ASPECT_RATIO = 800 / 480;
const CONTROL_LAZY_ASSET_VERSION = '20260707fit1';
const NAMEPLATE_TEMPLATE_ID = 'nameplate';
const NAMEPLATE_COMPANY_CN = '现象创新（深圳）科技有限公司';
const NAMEPLATE_COMPANY_EN = 'Pheno Innovations Technology Co., Ltd.';
const NAMEPLATE_BRAND_ASSET_PATHS = {
    blackLogo: `assets/nameplate/pheno-logo-black.png?v=${CONTROL_LAZY_ASSET_VERSION}`,
    whiteLogo: `assets/nameplate/pheno-logo-white.png?v=${CONTROL_LAZY_ASSET_VERSION}`,
    mark: `assets/nameplate/pheno-mark-square.png?v=${CONTROL_LAZY_ASSET_VERSION}`,
};
const nameplateBrandAssets = {};
const NAMEPLATE_TEMPLATE_FALLBACK = {
    templateId: NAMEPLATE_TEMPLATE_ID,
    name: '会议名牌',
    icon: 'meeting-nameplate',
    description: 'Pheno 品牌姓名牌',
    preview: '/templates/nameplate.png',
    defaultData: {
        type: 'template',
        template: NAMEPLATE_TEMPLATE_ID,
        name: '',
        backgroundStyle: 'formal_red',
        title: '',
        subtitle: ''
    }
};
const BUILTIN_NAMEPLATE_TEMPLATES = [
    {
        templateId: '__builtin_pheno_red',
        name: 'Pheno 红色底栏',
        builtin: true,
        templateConfig: { backgroundStyle: 'formal_red', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_green',
        name: 'Pheno 绿色底栏',
        builtin: true,
        templateConfig: { backgroundStyle: 'formal_green', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_band',
        name: 'Pheno 绿色横幅',
        builtin: true,
        templateConfig: { backgroundStyle: 'plain', title: '', subtitle: '', sleepIntervalSeconds: 43200 },
    },
    {
        templateId: '__builtin_pheno_profile',
        name: 'Pheno 职务名片',
        builtin: true,
        templateConfig: { backgroundStyle: 'formal_blue', title: 'Technical Expert', subtitle: '', sleepIntervalSeconds: 43200 },
    },
];

function loadNameplateBrandAssets() {
    if (typeof Image === 'undefined') return;
    Object.entries(NAMEPLATE_BRAND_ASSET_PATHS).forEach(([key, src]) => {
        if (nameplateBrandAssets[key]) return;
        const img = new Image();
        img.onload = () => {
            if (currentMode === 'template' && currentTemplateId === NAMEPLATE_TEMPLATE_ID) {
                renderCanvas();
            }
        };
        img.src = src;
        nameplateBrandAssets[key] = img;
    });
}

loadNameplateBrandAssets();

// 注意：以下变量在 app.js 中已定义，这里不再声明
// currentMode, sourceImage, textItems, mixedTextItems,
// selectedTextId, selectedMixedTextId, imageScale, mixedImageScale,
// cropX, cropY, mixedCropX, mixedCropY, processedImageData, redChannelData

// ==================== 初始化 ====================
document.addEventListener('DOMContentLoaded', async () => {
    console.log('[Editor] 开始初始化...');

    try {
        if (typeof requireAuth === 'function') {
            const user = await requireAuth();
            if (!user) return;
        }

        initEditorShell();
        runAfterFirstPaint(initEditorDataAndControls);
    } catch (error) {
        console.error('[Editor] 初始化错误:', error);
        log('初始化错误: ' + error.message, 'error');
    }
});

function runAfterFirstPaint(callback) {
    const raf = typeof window.requestAnimationFrame === 'function'
        ? window.requestAnimationFrame.bind(window)
        : (fn) => window.setTimeout(fn, 0);
    raf(() => window.setTimeout(callback, 0));
}

function initEditorShell() {
    // 从URL获取设备ID
    const params = new URLSearchParams(window.location.search);
    deviceId = params.get('deviceId') || '';

    const deviceIdInput = document.getElementById('deviceId');
    if (deviceIdInput) deviceIdInput.value = deviceId;

    const deviceNameDisplay = document.getElementById('deviceNameDisplay');
    const statusDot = document.getElementById('statusDot');

    if (deviceId) {
        if (deviceNameDisplay) deviceNameDisplay.textContent = deviceId;
        if (statusDot) statusDot.classList.add('online');
    } else {
        if (deviceNameDisplay) deviceNameDisplay.textContent = '模板设计';
        if (statusDot) statusDot.classList.add('online');
    }

    templates = [NAMEPLATE_TEMPLATE_FALLBACK];
    renderTemplateGrid();
    renderModalTemplateGrid();
    updateDeviceBoundControls();
    ensureNameplateOnlyMode({ silent: true });
}

async function initEditorDataAndControls() {
    try {
        initDropZones();
        initProcessOptions();
        updateResolution();
        updateImageStageVisibility();
        initCanvasEvents();  // 绑定画布事件

        await loadTemplates();
        ensureNameplateOnlyMode({ silent: true, skipRender: true });
        await loadSavedNameplateTemplates({ applyFirst: !hasSelectedDevice(), silent: true });
        await loadPages();
        updateDeviceBoundControls();

        console.log('[Editor] 初始化完成');
        log('系统初始化完成');
    } catch (error) {
        console.error('[Editor] 初始化错误:', error);
        log('初始化错误: ' + error.message, 'error');
    }
}

function hasSelectedDevice() {
    const inputValue = document.getElementById('deviceId')?.value?.trim() || '';
    return Boolean((deviceId || inputValue).trim());
}

function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function escapeJsString(value) {
    return String(value ?? '')
        .replace(/\\/g, '\\\\')
        .replace(/'/g, "\\'")
        .replace(/"/g, '\\"')
        .replace(/\n/g, '\\n')
        .replace(/\r/g, '\\r');
}

function updateDeviceBoundControls() {
    const selected = hasSelectedDevice();
    document.body?.classList.toggle('template-design-mode', !selected);

    const deviceNameDisplay = document.getElementById('deviceNameDisplay');
    const statusDot = document.getElementById('statusDot');
    if (!selected) {
        if (deviceNameDisplay) deviceNameDisplay.textContent = '模板设计';
        if (statusDot) {
            statusDot.classList.remove('offline');
            statusDot.classList.add('online');
        }
    }

    document.querySelectorAll('[data-requires-device]').forEach(button => {
        button.disabled = !selected;
        button.title = selected ? '' : '单台保存/发布请从设备页进入';
    });

    document.querySelectorAll('[data-requires-device-group]').forEach(group => {
        group.hidden = !selected;
    });

    const list = document.getElementById('pageList');
    if (!selected && list) {
        list.innerHTML = `
            <div class="empty-state" style="text-align: center; padding: 40px 20px; color: var(--text-light);">
                <div style="font-size: 2em; margin-bottom: 10px;">📝</div>
                <p>模板设计模式</p>
                <p style="font-size: 0.85em;">批量下发在名单下发页，单台编辑从设备页进入</p>
            </div>
        `;
    }
}

// ==================== 模板管理 ====================
async function loadTemplates() {
    try {
        const response = await fetch(`${API_BASE}/api/templates`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json();
        if (result.success) {
            templates = filterNameplateTemplates(result.templates);
            renderTemplateGrid();
            renderModalTemplateGrid();
        }
    } catch (e) {
        console.error('Failed to load templates:', e);
    }

    if (!templates.length) {
        templates = [NAMEPLATE_TEMPLATE_FALLBACK];
        renderTemplateGrid();
        renderModalTemplateGrid();
    }
}

function filterNameplateTemplates(sourceTemplates) {
    return (Array.isArray(sourceTemplates) ? sourceTemplates : [])
        .filter(t => t && t.templateId === NAMEPLATE_TEMPLATE_ID)
        .map(t => ({
            ...NAMEPLATE_TEMPLATE_FALLBACK,
            ...t,
            name: t.name || NAMEPLATE_TEMPLATE_FALLBACK.name,
            description: NAMEPLATE_TEMPLATE_FALLBACK.description
        }));
}

function getNameplateTemplate() {
    return getLoadedTemplate(NAMEPLATE_TEMPLATE_ID) || NAMEPLATE_TEMPLATE_FALLBACK;
}

function ensureNameplateOnlyMode(options = {}) {
    currentMode = 'template';
    currentTemplateId = NAMEPLATE_TEMPLATE_ID;

    document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.mode === 'template');
    });

    ['imageModeControls', 'textModeControls', 'mixedModeControls', 'weatherTemplateConfig', 'quoteTemplateConfig', 'qrcodeTemplateConfig'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.classList.add('hidden');
    });

    const templateControls = document.getElementById('templateModeControls');
    if (templateControls) templateControls.classList.remove('hidden');

    updateTemplateConfigVisibility(NAMEPLATE_TEMPLATE_ID);
    updateImageStageVisibility();

    if (!options.skipRender) {
        renderCanvas();
    }
    if (!options.silent) {
        log('已切换到名牌模板设计', 'success');
    }
}

function renderTemplateGrid() {
    const grid = document.getElementById('templateGrid');
    if (!grid) return;

    grid.innerHTML = templates.map(t => `
        <div class="template-card" onclick="selectTemplate('${t.templateId}')">
            ${renderTemplateIcon(t)}
            <div class="name">${t.name}</div>
            <div class="desc">${t.description}</div>
        </div>
    `).join('');
}

function renderModalTemplateGrid() {
    const grid = document.getElementById('modalTemplateGrid');
    if (!grid) return;

    grid.innerHTML = templates.map(t => `
        <div class="template-card" onclick="createPageFromTemplate('${t.templateId}')">
            ${renderTemplateIcon(t)}
            <div class="name">${t.name}</div>
            <div class="desc">${t.description}</div>
        </div>
    `).join('');
}

function renderTemplateIcon(template) {
    if (template?.templateId === NAMEPLATE_TEMPLATE_ID || template?.icon === 'meeting-nameplate') {
        return `
            <div class="icon nameplate-template-icon" aria-hidden="true">
                <img src="${NAMEPLATE_BRAND_ASSET_PATHS.mark}" alt="">
            </div>
        `;
    }

    return `<div class="icon">${template?.icon || ''}</div>`;
}

function getLoadedTemplate(templateId) {
    return templates.find(t => t.templateId === templateId);
}

function selectTemplate(templateId) {
    if (window.MEETING_NAMEPLATE_ONLY && templateId !== NAMEPLATE_TEMPLATE_ID) {
        templateId = NAMEPLATE_TEMPLATE_ID;
    }
    const template = getLoadedTemplate(templateId);
    if (template) {
        log(`选择模板: ${template.name}`);
        // TODO: 应用模板到画布
        applyTemplate(template);
    }
}

function applyTemplate(template) {
    if (window.MEETING_NAMEPLATE_ONLY) {
        template = getNameplateTemplate();
    }
    // 统一用 renderCanvas 渲染，避免"加载模板页面后白屏/不显示"
    currentMode = 'template';
    currentTemplateId = template.templateId;
    renderCanvas();
    updateTemplateConfigVisibility(template.templateId);
    log(`已应用模板: ${template.name}`, 'success');
}

function getCurrentTemplateConfig() {
    const templateId = currentTemplateId;
    if (!templateId) return {};
    if (templateId === 'weather') {
        return {
            city: document.getElementById('weatherCityInput')?.value?.trim() || '',
            sleepIntervalSeconds: parseInt(document.getElementById('weatherWakeInterval')?.value || '21600', 10),
        };
    }
    if (templateId === 'qrcode') {
        return {
            content: document.getElementById('qrcodeContentInput')?.value?.trim() || '',
            title: document.getElementById('qrcodeTitleInput')?.value?.trim() || '',
            sleepIntervalSeconds: parseInt(document.getElementById('qrcodeWakeInterval')?.value || '43200', 10),
        };
    }
    if (templateId === 'quote') {
        return {
            sleepIntervalSeconds: parseInt(document.getElementById('quoteWakeInterval')?.value || '86400', 10),
        };
    }
    if (templateId === 'nameplate') {
        return {
            name: document.getElementById('nameplateNameInput')?.value?.trim() || '',
            title: document.getElementById('nameplateTitleInput')?.value?.trim() || '',
            subtitle: document.getElementById('nameplateSubtitleInput')?.value?.trim() || '',
            backgroundStyle: document.getElementById('nameplateStyleSelect')?.value || 'formal_red',
            sleepIntervalSeconds: parseInt(document.getElementById('nameplateWakeInterval')?.value || '43200', 10),
        };
    }
    // todo 等模板暂无配置
    return {};
}

function getSavableNameplateTemplateConfig() {
    const config = getCurrentTemplateConfig();
    return {
        backgroundStyle: config.backgroundStyle || 'formal_red',
        title: config.title || '',
        subtitle: config.subtitle || '',
        sleepIntervalSeconds: parseInt(config.sleepIntervalSeconds || '43200', 10) || 43200
    };
}

function getNameplateTemplateName() {
    return document.getElementById('nameplateTemplateNameInput')?.value?.trim() || '会议名牌模板';
}

function applyNameplateTemplateConfig(config, options = {}) {
    if (!config) return false;

    const styleSelect = document.getElementById('nameplateStyleSelect');
    const titleInput = document.getElementById('nameplateTitleInput');
    const subtitleInput = document.getElementById('nameplateSubtitleInput');
    const wakeSelect = document.getElementById('nameplateWakeInterval');

    if (styleSelect && config.backgroundStyle) styleSelect.value = config.backgroundStyle;
    if (titleInput && config.title !== undefined) titleInput.value = config.title || '';
    if (subtitleInput && config.subtitle !== undefined) subtitleInput.value = config.subtitle || '';
    if (wakeSelect && config.sleepIntervalSeconds) wakeSelect.value = String(config.sleepIntervalSeconds);

    if (options.render !== false) {
        renderNameplatePreview();
    }
    return true;
}

function normalizeSavedNameplateTemplate(template) {
    return {
        templateId: template?.templateId || '',
        name: template?.name || '会议名牌模板',
        templateConfig: template?.templateConfig || {},
        createdAt: template?.createdAt || '',
        updatedAt: template?.updatedAt || ''
    };
}

async function loadSavedNameplateTemplates(options = {}) {
    const list = document.getElementById('savedTemplateList');
    if (list) {
        list.innerHTML = '<div class="saved-template-empty">正在加载模板</div>';
    }

    try {
        const response = await fetch(`${API_BASE}/api/nameplate/templates`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || '模板列表加载失败');
        }

        savedNameplateTemplates = (result.templates || []).map(normalizeSavedNameplateTemplate);
        renderSavedNameplateTemplateList();

        if (options.applyFirst && !activeSavedNameplateTemplateId && savedNameplateTemplates.length) {
            selectSavedNameplateTemplate(savedNameplateTemplates[0].templateId, { silent: options.silent });
        }
    } catch (error) {
        savedNameplateTemplates = [];
        renderSavedNameplateTemplateList(error.message);
        if (!options.silent) log('加载保存模板失败: ' + error.message, 'error');
    }
}

function renderSavedNameplateTemplateList(errorMessage = '') {
    const list = document.getElementById('savedTemplateList');
    if (!list) return;

    const allTemplates = [
        ...BUILTIN_NAMEPLATE_TEMPLATES,
        ...savedNameplateTemplates.map(template => ({ ...template, builtin: false })),
    ];

    if (!allTemplates.length) {
        list.innerHTML = '<div class="saved-template-empty">暂无模板</div>';
        return;
    }

    const rows = allTemplates.map(template => {
        const config = template.templateConfig || {};
        const active = template.templateId === activeSavedNameplateTemplateId ? ' active' : '';
        const meta = [
            config.title || '无职务',
            config.subtitle || '默认公司'
        ].join(' / ');
        const badge = template.builtin ? '<em>内置</em>' : '<em>已保存</em>';
        const deleteButton = template.builtin
            ? ''
            : `<button type="button" onclick="event.stopPropagation(); deleteSavedNameplateTemplate('${escapeJsString(template.templateId)}')">删除</button>`;
        return `
            <div class="saved-template-row${active}" onclick="selectSavedNameplateTemplate('${escapeJsString(template.templateId)}')">
                <div>
                    <strong>${escapeHtml(template.name)} ${badge}</strong>
                    <span>${escapeHtml(meta)}</span>
                </div>
                ${deleteButton}
            </div>
        `;
    });

    if (errorMessage) {
        rows.push(`<div class="saved-template-empty error">已显示内置模板；保存模板加载失败：${escapeHtml(errorMessage)}</div>`);
    } else if (!savedNameplateTemplates.length) {
        rows.push('<div class="saved-template-empty">暂无用户保存模板</div>');
    }

    list.innerHTML = rows.join('');
}

function selectSavedNameplateTemplate(templateId, options = {}) {
    const template = [...BUILTIN_NAMEPLATE_TEMPLATES, ...savedNameplateTemplates]
        .find(item => item.templateId === templateId);
    if (!template) return false;

    activeSavedNameplateTemplateId = template.templateId;
    const nameInput = document.getElementById('nameplateTemplateNameInput');
    if (nameInput) nameInput.value = template.name || '会议名牌模板';
    applyNameplateTemplateConfig(template.templateConfig, { render: options.render !== false });
    renderSavedNameplateTemplateList();
    if (!options.silent) {
        log(`已载入模板：${template.name}`, 'success');
    }
    return true;
}

function startNewNameplateTemplate() {
    activeSavedNameplateTemplateId = '';
    const nameInput = document.getElementById('nameplateTemplateNameInput');
    const titleInput = document.getElementById('nameplateTitleInput');
    const subtitleInput = document.getElementById('nameplateSubtitleInput');
    const styleSelect = document.getElementById('nameplateStyleSelect');
    const wakeSelect = document.getElementById('nameplateWakeInterval');

    if (nameInput) nameInput.value = '会议名牌模板';
    if (titleInput) titleInput.value = '';
    if (subtitleInput) subtitleInput.value = '';
    if (styleSelect) styleSelect.value = 'formal_red';
    if (wakeSelect) wakeSelect.value = '43200';
    renderNameplatePreview();
    renderSavedNameplateTemplateList();
    log('已新建空白模板，保存后会加入模板列表', 'info');
}

async function saveNameplateTemplate(options = {}) {
    const config = getSavableNameplateTemplateConfig();
    try {
        const templateId = options.asNew ? '' : activeSavedNameplateTemplateId;
        const response = await fetch(`${API_BASE}/api/nameplate/templates`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...(typeof getAuthHeaders === 'function' ? getAuthHeaders() : {})
            },
            body: JSON.stringify({
                templateId,
                name: getNameplateTemplateName(),
                templateConfig: config
            })
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || '保存模板失败');
        }

        const saved = normalizeSavedNameplateTemplate(result.template);
        activeSavedNameplateTemplateId = saved.templateId;
        const index = savedNameplateTemplates.findIndex(item => item.templateId === saved.templateId);
        if (index >= 0) {
            savedNameplateTemplates[index] = saved;
        } else {
            savedNameplateTemplates.unshift(saved);
        }
        renderSavedNameplateTemplateList();
        renderNameplatePreview();
        log(`模板已保存：${saved.name}`, 'success');
    } catch (error) {
        console.error('保存模板失败:', error);
        log('保存模板失败: ' + error.message, 'error');
    }
}

async function saveNameplateTemplateAsNew() {
    activeSavedNameplateTemplateId = '';
    await saveNameplateTemplate({ asNew: true });
}

async function deleteSavedNameplateTemplate(templateId) {
    const template = savedNameplateTemplates.find(item => item.templateId === templateId);
    if (!template) return;
    if (!confirm(`删除模板「${template.name}」？`)) return;

    try {
        const response = await fetch(`${API_BASE}/api/nameplate/templates/${encodeURIComponent(templateId)}`, {
            method: 'DELETE',
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.success) {
            throw new Error(result.error || '删除模板失败');
        }
        savedNameplateTemplates = savedNameplateTemplates.filter(item => item.templateId !== templateId);
        if (activeSavedNameplateTemplateId === templateId) activeSavedNameplateTemplateId = '';
        renderSavedNameplateTemplateList();
        log(`模板已删除：${template.name}`, 'success');
    } catch (error) {
        console.error('删除模板失败:', error);
        log('删除模板失败: ' + error.message, 'error');
    }
}

function updateTemplateConfigVisibility(templateId) {
    // 显示/隐藏各模板的配置面板
    const configs = {
        'weather': 'weatherTemplateConfig',
        'quote': 'quoteTemplateConfig',
        'qrcode': 'qrcodeTemplateConfig',
        'nameplate': 'nameplateTemplateConfig',
    };
    for (const [id, elId] of Object.entries(configs)) {
        const el = document.getElementById(elId);
        if (el) el.classList.toggle('hidden', id !== templateId);
    }
}

// 模板数据缓存（天气、名言等）
var templateWeatherData = null;
var templateQuoteData = null;
var _weatherCacheCity = '';
var _weatherCacheTime = 0;
var _WEATHER_CACHE_TTL = 5 * 60 * 1000; // 5分钟缓存

function renderClockTemplate(ctx, width, height) {
    const now = new Date();
    const timeStr = now.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
    const dateStr = now.toLocaleDateString('zh-CN', { year: 'numeric', month: 'long', day: 'numeric' });
    const weekDay = ['星期日', '星期一', '星期二', '星期三', '星期四', '星期五', '星期六'][now.getDay()];

    // 时间
    ctx.font = epdCanvasFont(120, '700');
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(timeStr, width / 2, height / 2 - 50);

    // 日期
    ctx.font = epdCanvasFont(36, '600');
    ctx.fillText(dateStr, width / 2, height / 2 + 60);

    // 星期
    ctx.font = epdCanvasFont(28, '600');
    ctx.fillStyle = 'red';
    ctx.fillText(weekDay, width / 2, height / 2 + 110);
}

function renderCalendarTemplate(ctx, width, height) {
    const now = new Date();
    const year = now.getFullYear();
    const month = now.getMonth();
    const day = now.getDate();

    // 标题
    ctx.font = epdCanvasFont(48, '700');
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.fillText(`${year}年${month + 1}月`, width / 2, 60);

    // 星期标题
    const weekDays = ['日', '一', '二', '三', '四', '五', '六'];
    ctx.font = epdCanvasFont(24, '600');
    const cellWidth = (width - 80) / 7;
    const startX = 40;

    weekDays.forEach((d, i) => {
        ctx.fillStyle = (i === 0 || i === 6) ? 'red' : 'black';
        ctx.fillText(d, startX + cellWidth * i + cellWidth / 2, 120);
    });

    // 日期网格
    const firstDay = new Date(year, month, 1).getDay();
    const daysInMonth = new Date(year, month + 1, 0).getDate();

    ctx.font = epdCanvasFont(28, '600');
    let row = 0;
    for (let d = 1; d <= daysInMonth; d++) {
        const col = (firstDay + d - 1) % 7;
        if (d > 1 && col === 0) row++;

        const x = startX + cellWidth * col + cellWidth / 2;
        const y = 170 + row * 50;

        if (d === day) {
            ctx.beginPath();
            ctx.arc(x, y, 20, 0, Math.PI * 2);
            ctx.fillStyle = 'red';
            ctx.fill();
            ctx.fillStyle = 'white';
        } else {
            ctx.fillStyle = (col === 0 || col === 6) ? 'red' : 'black';
        }
        ctx.fillText(d.toString(), x, y + 8);
    }
}

function renderQuoteTemplate(ctx, width, height) {
    // 优先使用从 API 获取的数据
    const quote = templateQuoteData;

    if (!quote || !quote.content) {
        // 回退：显示提示
        ctx.font = epdCanvasFont(32, '600');
        ctx.fillStyle = 'black';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('点击「获取一言并预览」加载内容', width / 2, height / 2);
        return;
    }

    // 清空画布
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);

    // 引号装饰（黄色，与白色背景形成对比）
    ctx.font = epdCanvasFont(120, '700');
    ctx.fillStyle = 'yellow';
    ctx.textAlign = 'left';
    ctx.fillText('"', 60, 130);

    // 内容自动换行
    const content = quote.content;
    const maxWidth = width - 80;
    const lineHeight = 60;
    ctx.font = epdCanvasFont(44, '700');
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    const lines = [];
    let currentLine = '';
    for (let i = 0; i < content.length; i++) {
        const testLine = currentLine + content[i];
        const metrics = ctx.measureText(testLine);
        if (metrics.width > maxWidth && currentLine) {
            lines.push(currentLine);
            currentLine = content[i];
        } else {
            currentLine = testLine;
        }
    }
    if (currentLine) lines.push(currentLine);

    const totalHeight = lines.length * lineHeight;
    const startY = Math.max(86, (height - totalHeight) / 2 - 18);

    lines.forEach((line, i) => {
        ctx.fillText(line, width / 2, startY + i * lineHeight + lineHeight / 2);
    });

    // 来源
    const sourceParts = [];
    if (quote.author) sourceParts.push(quote.author);
    if (quote.origin) sourceParts.push(`《${quote.origin}》`);
    if (sourceParts.length > 0) {
        ctx.font = epdCanvasFont(30, '600');
        ctx.fillStyle = 'black';
        ctx.fillText(sourceParts.join('  '), width / 2, startY + totalHeight + 40);
    }
}

function renderQRCodeTemplate(ctx, width, height) {
    const content = document.getElementById('qrcodeContentInput')?.value?.trim() || '';
    const title = document.getElementById('qrcodeTitleInput')?.value?.trim() || '';

    if (!content) {
        ctx.font = epdCanvasFont(32, '600');
        ctx.fillStyle = 'black';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('请在右侧配置二维码内容', width / 2, height / 2);
        return;
    }

    // 使用 qrcode-generator 库生成
    try {
        const typeNumber = 0;
        const errorCorrectionLevel = 'M';
        const qr = qrcode(typeNumber, errorCorrectionLevel);
        qr.addData(content);
        qr.make();

        const cellSize = 8;
        const qrSize = qr.getModuleCount() * cellSize;
        const x = (width - qrSize) / 2;
        const y = title ? (height - qrSize) / 2 - 20 : (height - qrSize) / 2;

        for (let r = 0; r < qr.getModuleCount(); r++) {
            for (let c = 0; c < qr.getModuleCount(); c++) {
                ctx.fillStyle = qr.isDark(r, c) ? 'black' : 'white';
                ctx.fillRect(x + c * cellSize, y + r * cellSize, cellSize, cellSize);
            }
        }

        if (title) {
            ctx.font = epdCanvasFont(32, '600');
            ctx.fillStyle = 'black';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';
            ctx.fillText(title, width / 2, y + qrSize + 20);
        }
    } catch (e) {
        console.error('QR generation error:', e);
        ctx.font = epdCanvasFont(32, '600');
        ctx.fillStyle = 'red';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('二维码生成失败', width / 2, height / 2);
    }
}

function renderNameplatePreview() {
    renderCanvas();
}

function fitCanvasFontSize(ctx, text, maxWidth, startSize, minSize) {
    for (let size = startSize; size >= minSize; size -= 2) {
        ctx.font = epdCanvasFont(size, '700');
        if (ctx.measureText(text).width <= maxWidth) {
            return size;
        }
    }
    return minSize;
}

function fitCanvasFontSizeWithWeight(ctx, text, maxWidth, startSize, minSize, weight = '700') {
    for (let size = startSize; size >= minSize; size -= 2) {
        ctx.font = nameplateCanvasFont(size, weight, text);
        if (ctx.measureText(text).width <= maxWidth) {
            return size;
        }
    }
    return minSize;
}

function nameplateCanvasFont(size, weight = '700', text = '') {
    const family = /[\u4e00-\u9fff]/.test(String(text || ''))
        ? EPD_CANVAS_FONT_FAMILY
        : 'Arial, "Helvetica Neue", "Segoe UI", sans-serif';
    return `${weight} ${size}px ${family}`;
}

function getLoadedNameplateAsset(key) {
    const img = nameplateBrandAssets[key];
    return img && img.complete && img.naturalWidth ? img : null;
}

function drawNameplateAsset(ctx, key, x, y, width, height) {
    const img = getLoadedNameplateAsset(key);
    if (!img) {
        loadNameplateBrandAssets();
        return false;
    }
    ctx.drawImage(img, x, y, width, height);
    return true;
}

function drawPhenoFooterNameplate(ctx, width, height, name, style, roleText, companyText) {
    const accent = style === 'formal_green' ? '#00ff00' : '#ff0000';
    const footerTop = 385;
    const hasRole = Boolean(roleText);

    ctx.fillStyle = accent;
    ctx.fillRect(0, 0, width, footerTop);
    ctx.fillStyle = 'white';
    ctx.fillRect(0, footerTop, width, height - footerTop);

    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const nameSize = fitCanvasFontSizeWithWeight(ctx, name, 590, hasRole ? 118 : 148, hasRole ? 62 : 72, '700');
    ctx.font = nameplateCanvasFont(nameSize, '700', name);
    ctx.fillStyle = 'white';
    ctx.fillText(name, width / 2, hasRole ? 155 : 184);

    if (hasRole) {
        const roleSize = fitCanvasFontSizeWithWeight(ctx, roleText, 590, 48, 28, '400');
        ctx.font = nameplateCanvasFont(roleSize, '400', roleText);
        ctx.fillText(roleText, width / 2, 276);
    }

    drawNameplateAsset(ctx, 'blackLogo', 108, 410, 181, 39);

    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    const companySize = fitCanvasFontSizeWithWeight(ctx, companyText, 390, 25, 18, '700');
    ctx.font = nameplateCanvasFont(companySize, '700', companyText);
    ctx.fillStyle = 'black';
    ctx.fillText(companyText, 326, 433);
}

function drawPhenoGreenBandNameplate(ctx, width, height, name, roleText) {
    const bandTop = 361;
    const hasRole = Boolean(roleText);
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, bandTop);
    ctx.fillStyle = '#00ff00';
    ctx.fillRect(0, bandTop, width, height - bandTop);

    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const nameSize = fitCanvasFontSizeWithWeight(ctx, name, 590, hasRole ? 118 : 150, hasRole ? 62 : 72, '700');
    ctx.font = nameplateCanvasFont(nameSize, '700', name);
    ctx.fillStyle = 'black';
    ctx.fillText(name, width / 2, hasRole ? 155 : 184);

    if (hasRole) {
        const roleSize = fitCanvasFontSizeWithWeight(ctx, roleText, 590, 48, 28, '400');
        ctx.font = nameplateCanvasFont(roleSize, '400', roleText);
        ctx.fillText(roleText, width / 2, 276);
    }

    drawNameplateAsset(ctx, 'whiteLogo', 276, 390, 248, 54);
}

function drawPhenoProfileNameplate(ctx, width, height, name, roleText, companyText) {
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);

    const markSize = 128;
    const gap = 26;
    const minMargin = 80;
    const maxTextWidth = width - minMargin * 2 - markSize - gap;
    const nameSize = fitCanvasFontSizeWithWeight(ctx, name, maxTextWidth, 96, 58, '700');
    ctx.font = nameplateCanvasFont(nameSize, '700', name);
    const nameWidth = ctx.measureText(name).width;

    let roleWidth = 0;
    let roleSize = 40;
    if (roleText) {
        roleSize = fitCanvasFontSizeWithWeight(ctx, roleText, maxTextWidth, 40, 24, '400');
        ctx.font = nameplateCanvasFont(roleSize, '400', roleText);
        roleWidth = ctx.measureText(roleText).width;
    }

    const textWidth = Math.max(nameWidth, roleWidth);
    const groupWidth = markSize + gap + textWidth;
    const groupLeft = Math.max(minMargin, Math.round((width - groupWidth) / 2));
    const markTop = 153;
    const textLeft = groupLeft + markSize + gap;

    drawNameplateAsset(ctx, 'mark', groupLeft, markTop, markSize, markSize);

    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.font = nameplateCanvasFont(nameSize, '700', name);
    ctx.fillStyle = 'black';
    ctx.fillText(name, textLeft, 151);

    if (roleText) {
        ctx.font = nameplateCanvasFont(roleSize, '400', roleText);
        ctx.fillText(roleText, textLeft, 244);
    }

    const companySize = fitCanvasFontSizeWithWeight(ctx, companyText, 370, 22, 16, '400');
    ctx.font = nameplateCanvasFont(companySize, '400', companyText);
    const companyWidth = ctx.measureText(companyText).width;
    const textX = Math.round((width - companyWidth) / 2);
    const lineGap = 20;
    const lineY = 406;
    const lineHeight = 16;

    ctx.fillStyle = 'black';
    ctx.fillRect(0, lineY, Math.max(0, textX - lineGap), lineHeight);
    ctx.fillRect(Math.min(width, textX + companyWidth + lineGap), lineY,
        Math.max(0, width - (textX + companyWidth + lineGap)), lineHeight);

    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.fillText(companyText, textX, 402);
}

function renderNameplateTemplate(ctx, width, height) {
    const name = document.getElementById('nameplateNameInput')?.value?.trim() || '姓名';
    const title = document.getElementById('nameplateTitleInput')?.value?.trim() || '';
    const subtitle = document.getElementById('nameplateSubtitleInput')?.value?.trim() || '';
    const style = document.getElementById('nameplateStyleSelect')?.value || 'formal_red';

    if (style === 'formal_blue') {
        drawPhenoProfileNameplate(ctx, width, height, name, title, subtitle || NAMEPLATE_COMPANY_EN);
    } else if (style === 'plain') {
        drawPhenoGreenBandNameplate(ctx, width, height, name, title);
    } else {
        drawPhenoFooterNameplate(ctx, width, height, name, style, title, subtitle || NAMEPLATE_COMPANY_CN);
    }
}

function renderWeatherTemplate(ctx, width, height) {
    const data = templateWeatherData;

    if (!data) {
        ctx.font = epdCanvasFont(32, '600');
        ctx.fillStyle = 'black';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('点击「获取天气并预览」加载内容', width / 2, height / 2);
        return;
    }

    // 背景
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);

    // 城市 + 日期
    const dateStr = new Date().toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' });
    ctx.font = epdCanvasFont(36, '600');
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillText(`${data.city || ''}  ${dateStr}`, width / 2, 40);

    // 温度（大号）
    if (data.temperature !== undefined && data.temperature !== null) {
        ctx.font = epdCanvasFont(120, '700');
        ctx.fillStyle = 'blue';
        ctx.textBaseline = 'middle';
        ctx.fillText(`${Math.round(data.temperature)}°C`, width / 2, height / 2 - 30);
    }

    // 天气状况
    ctx.font = epdCanvasFont(40, '700');
    ctx.fillStyle = 'red';
    ctx.fillText(data.weatherText || '', width / 2, height / 2 + 80);

    // 底部信息
    const infoParts = [];
    if (data.humidity !== undefined) infoParts.push(`湿度 ${data.humidity}%`);
    if (data.wind_speed !== undefined) infoParts.push(`风速 ${data.wind_speed}km/h`);
    if (data.temp_min !== undefined && data.temp_max !== undefined) {
        infoParts.push(`${Math.round(data.temp_min)}°C~${Math.round(data.temp_max)}°C`);
    }

    if (infoParts.length > 0) {
        ctx.font = epdCanvasFont(24, '600');
        ctx.fillStyle = 'black';
        ctx.fillText(infoParts.join('  |  '), width / 2, height - 60);
    }
}

// ==================== 模板数据获取与预览 ====================

async function fetchWeatherAndPreview() {
    const city = document.getElementById('weatherCityInput')?.value?.trim();
    if (!city) {
        log('请输入城市名称', 'error');
        return;
    }

    // 5分钟内同一城市不重复请求
    const now = Date.now();
    if (city === _weatherCacheCity && templateWeatherData && (now - _weatherCacheTime) < _WEATHER_CACHE_TTL) {
        log('天气数据来自缓存，5分钟内有效', 'info');
        renderCanvas();
        return;
    }

    log('正在获取天气数据...');
    try {
        const resp = await fetch(`${API_BASE}/api/weather?city=${encodeURIComponent(city)}`);
        const result = await resp.json();
        if (result.success && result.data) {
            templateWeatherData = result.data;
            _weatherCacheCity = city;
            _weatherCacheTime = Date.now();
            // 后端已返回 weatherText（和风天气中文文本），无需前端映射
            renderCanvas();
            log(`天气已更新: ${result.data.city} ${Math.round(result.data.temperature)}°C`, 'success');
        } else {
            log('获取天气失败', 'error');
        }
    } catch (e) {
        console.error(e);
        log('获取天气出错，请检查网络', 'error');
    }
}

async function fetchQuoteAndPreview() {
    log('正在获取每日一言...');
    try {
        const resp = await fetch(`${API_BASE}/api/quote`);
        const result = await resp.json();
        if (result.success && result.data) {
            templateQuoteData = result.data;
            renderCanvas();
            log('每日一言已更新', 'success');
        } else {
            log('获取一言失败', 'error');
        }
    } catch (e) {
        console.error(e);
        log('获取一言出错', 'error');
    }
}

function renderQRCodePreview() {
    renderCanvas();
    log('二维码已更新', 'success');
}

// ==================== 页面管理 ====================
async function loadPages() {
    if (!deviceId) return;

    try {
        // 限制列表数量，避免历史数据过多时卡顿
        const response = await fetch(`${API_BASE}/api/pages/list/${deviceId}?limit=200`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json();
        if (result.success) {
            pages = (Array.isArray(result.pages) ? result.pages : []).filter(isNameplatePage);
            renderPageList();
        }
    } catch (e) {
        console.error('Failed to load pages:', e);
    }
}

function isNameplatePage(page) {
    const data = page && page.data ? page.data : {};
    return page && page.type === 'template' && data.template === NAMEPLATE_TEMPLATE_ID;
}

function renderPageList() {
    const list = document.getElementById('pageList');
    if (!list) return;

    if (pages.length === 0) {
        list.innerHTML = `
            <div class="empty-state" style="text-align: center; padding: 40px 20px; color: var(--text-light);">
                <div style="font-size: 2em; margin-bottom: 10px;">📝</div>
                <p>暂无页面</p>
                <p style="font-size: 0.85em;">点击上方"+ 新建"创建第一个页面</p>
            </div>
        `;
        return;
    }

    list.innerHTML = pages.map(page => `
        <div class="page-item ${page.pageId === currentPageId ? 'active' : ''}"
             onclick="selectPage('${page.pageId}')" data-page-id="${page.pageId}">
            <div class="page-thumb">
                ${page.thumbnail ?
                    `<img src="${page.thumbnail}" alt="">` :
                    `<span class="icon">${getPageIcon(page.type)}</span>`
                }
            </div>
            <div class="page-info">
                <div class="page-name">${page.name}</div>
                <div class="page-type">${getPageTypeName(page.type)}</div>
            </div>
            <div class="page-actions">
                <button onclick="event.stopPropagation(); duplicatePage('${page.pageId}')" title="复制">📋</button>
                <button class="delete" onclick="event.stopPropagation(); deletePage('${page.pageId}')" title="删除">🗑️</button>
            </div>
        </div>
    `).join('');
}

function getPageIcon(type) {
    const icons = {
        'image': '🖼️',
        'text': '📝',
        'mixed': '🎨',
        'template': '桌牌',
        'custom': '⬜'
    };
    return icons[type] || '📄';
}

function getPageTypeName(type) {
    const names = {
        'image': '图片',
        'text': '文字',
        'mixed': '图文',
        'template': '会议名牌',
        'custom': '自定义'
    };
    return names[type] || '页面';
}

async function selectPage(pageId) {
    try {
        const response = await fetch(`${API_BASE}/api/pages/${pageId}`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json();
        if (result.success) {
            currentPageId = pageId;
            loadPageToCanvas(result.page);
            renderPageList();
            log(`已加载页面: ${result.page.name}`, 'success');
        }
    } catch (e) {
        log('加载页面失败', 'error');
    }
}

function loadPageToCanvas(page) {
    // 根据页面类型加载到画布
    const data = page.data || {};

    if (window.MEETING_NAMEPLATE_ONLY && !isNameplatePage(page)) {
        applyTemplate(getNameplateTemplate());
        log('当前只支持会议名牌，已切换到名牌模板', 'info');
        return;
    }

    // 切换到对应模式
    if (page.type && page.type !== currentMode) {
        // app.js 的 switchMode 仅支持 image/text/mixed；模板模式在 editor.js 内渲染
        if (page.type === 'template') {
            currentMode = 'template';
        } else {
            switchMode(page.type);
        }
    }

    // 模板页面：根据 data.template 渲染（否则会被 renderCanvas 清空导致白屏）
    if ((page.type === 'template' || currentMode === 'template') && data.template) {
        const template = getLoadedTemplate(data.template);
        if (!template) {
            currentTemplateId = null;
            currentMode = 'image';
            sourceImage = null;
            textItems = [];
            mixedTextItems = [];
            renderCanvas();
            log('历史模板已移除，已按自定义内容打开', 'info');
            return;
        }

        currentTemplateId = template.templateId;
        // 模板页默认不加载图片/文字叠加
        sourceImage = null;
        textItems = [];
        mixedTextItems = [];
        renderCanvas();
        return;
    }

    // 加载图片数据到画布
    if (data.imageData) {
        // 加载图片数据
        const img = new Image();
        img.onload = () => {
            sourceImage = img;
            drawImageCoverToMainCanvas();
            if (currentMode === 'image') {
                ensureImageEditingAssets().then((ok) => {
                    if (ok && initImageCropper(data.imageData)) {
                        return;
                    }
                    renderCanvas();
                });
            } else {
                renderCanvas();
            }
        };
        img.src = data.imageData;
        return;
    }

    // 加载文字数据
    if (data.textItems) {
        textItems = data.textItems;
    } else {
        textItems = [];
    }

    if (data.mixedTextItems) {
        mixedTextItems = data.mixedTextItems;
    } else {
        mixedTextItems = [];
    }

    renderCanvas();
    if (typeof updateTextItemsList === 'function') updateTextItemsList();
}

async function savePage() {
    if (!deviceId) {
        updateDeviceBoundControls();
        log('模板设计模式不绑定单台设备；如需保存单台草稿，请从设备页进入', 'info');
        return;
    }

    // 获取画布缩略图和完整画布数据
    if (currentMode === 'image') {
        syncImageCanvasFromCropper();
    }
    const canvas = document.getElementById('mainCanvas');
    const thumbnail = canvas.toDataURL('image/jpeg', 0.5);

    // 收集页面数据
    // - 非模板：保存 imageData（base64）+ 文字叠加
    // - 模板：只保存 templateId（避免大 base64 导致“保存很慢/请求很大”）
    let pageData;
    if (currentMode === 'template') {
        pageData = {
            mode: currentMode,
            template: currentTemplateId || null,
            templateConfig: getCurrentTemplateConfig()
        };
    } else {
        // PNG base64 体积很大（800x480也可能上 MB），会导致“保存半天/加载很慢”
        // 这里改用 JPEG（有损但足够编辑预览），显著减小体积
        const imageDataUrl = canvas.toDataURL('image/jpeg', 0.85);
        pageData = {
            mode: currentMode,
            imageData: imageDataUrl,
            textItems: textItems || [],
            mixedTextItems: mixedTextItems || []
        };
    }

    const pageName = currentPageId ?
        (pages.find(p => p.pageId === currentPageId)?.name || '未命名页面') :
        prompt('请输入页面名称:', '未命名页面');

    if (!pageName) return;

    try {
        log('正在保存页面...', 'info');
        const response = await fetch(`${API_BASE}/api/pages/save`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...(typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}) },
            body: JSON.stringify({
                deviceId,
                pageId: currentPageId,
                name: pageName,
                type: currentMode,
                data: pageData,
                thumbnail
            })
        });

        const result = await response.json();
        if (result.success) {
            currentPageId = result.pageId;
            await loadPages();
            log('页面保存成功', 'success');
        } else {
            log('保存失败: ' + result.error, 'error');
        }
    } catch (e) {
        log('保存失败', 'error');
    }
}

async function deletePage(pageId) {
    if (!confirm('确定要删除这个页面吗？')) return;

    try {
        // 先本地移除，提升交互体验（避免必须刷新才消失）
        pages = (pages || []).filter(p => p.pageId !== pageId);
        if (currentPageId === pageId) currentPageId = null;
        renderPageList();

        const response = await fetch(`${API_BASE}/api/pages/${pageId}`, {
            method: 'DELETE',
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });

        const result = await response.json();
        if (result.success) {
            await loadPages();
            log('页面已删除', 'success');
        } else {
            // 删除失败则回滚刷新
            await loadPages();
            log('删除失败: ' + (result.error || 'unknown'), 'error');
        }
    } catch (e) {
        // 出错也刷新一次，确保 UI 一致
        await loadPages();
        log('删除失败', 'error');
    }
}

async function duplicatePage(pageId) {
    const page = pages.find(p => p.pageId === pageId);
    if (!page) return;

    try {
        // 列表接口已做轻量化（不再返回 page.data），复制前先拉取完整页面
        const detailResp = await fetch(`${API_BASE}/api/pages/${pageId}`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const detail = await detailResp.json();
        if (!detail.success) {
            log('复制失败: 无法获取页面详情', 'error');
            return;
        }
        const src = detail.page;

        const response = await fetch(`${API_BASE}/api/pages/save`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...(typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}) },
            body: JSON.stringify({
                deviceId,
                name: (src.name || page.name || '未命名页面') + ' (副本)',
                type: src.type || page.type,
                data: src.data || {},
                thumbnail: src.thumbnail || page.thumbnail || ''
            })
        });

        const result = await response.json();
        if (result.success) {
            await loadPages();
            log('页面已复制', 'success');
        }
    } catch (e) {
        log('复制失败', 'error');
    }
}

// ==================== 新建页面 ====================
function showNewPageModal() {
    if (!hasSelectedDevice()) {
        updateDeviceBoundControls();
        log('模板设计模式不新建单台页面草稿；如需单台编辑，请从设备页进入', 'info');
        return;
    }
    document.getElementById('newPageModal').classList.add('show');
    document.getElementById('newPageName').value = '';
    document.getElementById('newPageName').focus();
}

function hideNewPageModal() {
    document.getElementById('newPageModal').classList.remove('show');
}

async function createPageFromTemplate(templateId) {
    if (!hasSelectedDevice()) {
        updateDeviceBoundControls();
        log('模板设计模式不新建单台页面草稿；如需单台编辑，请从设备页进入', 'info');
        return;
    }
    if (window.MEETING_NAMEPLATE_ONLY) {
        templateId = NAMEPLATE_TEMPLATE_ID;
    }
    const template = getLoadedTemplate(templateId);
    if (!template) {
        log('模板不存在或已移除', 'error');
        return;
    }
    const pageName = document.getElementById('newPageName').value.trim() || template.name;

    hideNewPageModal();

    // 创建新页面
    currentPageId = null;

    // 切换到对应模式
    switchMode('template');
    applyTemplate(template);

    // 自动保存
    try {
        const canvas = document.getElementById('mainCanvas');
        const thumbnail = canvas.toDataURL('image/jpeg', 0.5);

        const response = await fetch(`${API_BASE}/api/pages/save`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...(typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}) },
            body: JSON.stringify({
                deviceId,
                name: pageName,
                type: 'template',
                data: { template: templateId },
                thumbnail
            })
        });

        const result = await response.json();
        if (result.success) {
            currentPageId = result.pageId;
            await loadPages();
            log(`已创建页面: ${pageName}`, 'success');
        }
    } catch (e) {
        log('创建页面失败', 'error');
    }
}

// ==================== 部署 ====================
async function deployToDevice() {
    if (!deviceId) {
        updateDeviceBoundControls();
        log('模板设计模式不直接发布；批量下发请到名单下发，单台发布请从设备页进入', 'info');
        return;
    }

    log('开始部署到设备...');

    // 处理并上传当前页面
    await processImage();
    await uploadToDevice();
}

// ==================== 面板切换 ====================
function switchPanel(panelId) {
    document.querySelectorAll('.panel-tab').forEach(tab => {
        tab.classList.toggle('active', tab.dataset.panel === panelId);
    });

    document.getElementById('editPanel').classList.toggle('hidden', panelId !== 'edit');
    document.getElementById('processPanel').classList.toggle('hidden', panelId !== 'process');
}

// ==================== Cropper / FilePond 图片入口 ====================
function updateScaleControls(scale) {
    const percent = Math.round((scale || 1) * 100);
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    if (slider) slider.value = Math.min(300, Math.max(10, percent));
    if (input) input.value = percent;
}

function loadLazyStyle(href, id) {
    if (document.getElementById(id)) return Promise.resolve();
    return new Promise((resolve, reject) => {
        const link = document.createElement('link');
        link.id = id;
        link.rel = 'stylesheet';
        link.href = href;
        link.onload = resolve;
        link.onerror = () => reject(new Error(`加载样式失败: ${href}`));
        document.head.appendChild(link);
    });
}

function loadLazyScript(src, id) {
    if (document.getElementById(id)) return Promise.resolve();
    return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.id = id;
        script.src = src;
        script.onload = resolve;
        script.onerror = () => reject(new Error(`加载脚本失败: ${src}`));
        document.body.appendChild(script);
    });
}

function ensureImageEditingAssets() {
    if (imageEditingAssetsPromise) return imageEditingAssetsPromise;

    const v = CONTROL_LAZY_ASSET_VERSION;
    imageEditingAssetsPromise = Promise.all([
        loadLazyStyle(`vendor/filepond.min.css?v=${v}`, 'lazy-filepond-css'),
        loadLazyStyle(`vendor/filepond-plugin-image-preview.min.css?v=${v}`, 'lazy-filepond-preview-css'),
        loadLazyStyle(`vendor/cropper.min.css?v=${v}`, 'lazy-cropper-css'),
    ])
        .then(() => loadLazyScript(`vendor/filepond.min.js?v=${v}`, 'lazy-filepond-js'))
        .then(() => Promise.all([
            loadLazyScript(`vendor/filepond-plugin-file-validate-type.min.js?v=${v}`, 'lazy-filepond-validate-js'),
            loadLazyScript(`vendor/filepond-plugin-image-exif-orientation.min.js?v=${v}`, 'lazy-filepond-exif-js'),
            loadLazyScript(`vendor/filepond-plugin-image-preview.min.js?v=${v}`, 'lazy-filepond-preview-js'),
            loadLazyScript(`vendor/cropper.min.js?v=${v}`, 'lazy-cropper-js'),
        ]))
        .then(() => true)
        .catch((error) => {
            imageEditingAssetsPromise = null;
            console.error('[Editor] 图片编辑组件加载失败:', error);
            log('图片编辑组件加载失败，已使用基础上传模式', 'error');
            return false;
        });

    return imageEditingAssetsPromise;
}

async function prepareImageEditingTools() {
    const ok = await ensureImageEditingAssets();
    if (!ok) return false;
    const fileInput = document.getElementById('fileInput');
    return initImageFilePond(fileInput);
}

function isImageCropperReady() {
    return !!(imageCropper && imageCropper.ready && sourceImage && currentMode === 'image');
}

function updateImageStageVisibility() {
    const stage = document.getElementById('cropperStage');
    const canvas = document.getElementById('mainCanvas');
    const showCropper = !!(imageCropper && sourceImage && currentMode === 'image');
    if (stage) stage.classList.toggle('hidden', !showCropper);
    if (canvas) canvas.classList.toggle('hidden', showCropper);
}

function destroyImageCropper() {
    if (imageCropper) {
        imageCropper.destroy();
        imageCropper = null;
    }
    updateImageStageVisibility();
}

function syncCropperToLegacyState(updateInputs = false) {
    if (!isImageCropperReady()) return;
    const data = imageCropper.getData(true);
    if (!data || data.width <= 0 || data.height <= 0) return;

    cropX = Math.max(0, data.x || 0);
    cropY = Math.max(0, data.y || 0);
    imageScale = 800 / data.width;

    if (updateInputs) {
        updateScaleControls(imageScale);
    }
}

function centerCropBoxToImage() {
    if (!imageCropper || !imageCropper.ready) return;
    const canvasData = imageCropper.getCanvasData();
    if (!canvasData || canvasData.width <= 0 || canvasData.height <= 0) return;

    let cropWidth = canvasData.width;
    let cropHeight = cropWidth / EPD_CROP_ASPECT_RATIO;
    if (cropHeight > canvasData.height) {
        cropHeight = canvasData.height;
        cropWidth = cropHeight * EPD_CROP_ASPECT_RATIO;
    }

    imageCropper.setCropBoxData({
        left: canvasData.left + (canvasData.width - cropWidth) / 2,
        top: canvasData.top + (canvasData.height - cropHeight) / 2,
        width: cropWidth,
        height: cropHeight,
    });
}

function fitImageCropperToStage() {
    if (!imageCropper || !imageCropper.ready || !sourceImage) return false;
    const stage = document.getElementById('cropperStage');
    if (!stage) return false;

    const rect = stage.getBoundingClientRect();
    const stageWidth = rect.width;
    const stageHeight = rect.height;
    if (stageWidth < 10 || stageHeight < 10) return false;

    if (typeof imageCropper.resize === 'function') {
        imageCropper.resize();
    }

    const imageAspect = sourceImage.width / sourceImage.height;
    let canvasWidth = stageWidth;
    let canvasHeight = canvasWidth / imageAspect;
    if (canvasHeight < stageHeight) {
        canvasHeight = stageHeight;
        canvasWidth = canvasHeight * imageAspect;
    }

    imageCropper.setCanvasData({
        left: (stageWidth - canvasWidth) / 2,
        top: (stageHeight - canvasHeight) / 2,
        width: canvasWidth,
        height: canvasHeight,
    });

    let cropWidth = stageWidth;
    let cropHeight = cropWidth / EPD_CROP_ASPECT_RATIO;
    if (cropHeight > stageHeight) {
        cropHeight = stageHeight;
        cropWidth = cropHeight * EPD_CROP_ASPECT_RATIO;
    }

    imageCropper.setCropBoxData({
        left: (stageWidth - cropWidth) / 2,
        top: (stageHeight - cropHeight) / 2,
        width: cropWidth,
        height: cropHeight,
    });

    return true;
}

function isImageCropperStageFitted() {
    if (!imageCropper || !imageCropper.ready) return false;
    const stage = document.getElementById('cropperStage');
    if (!stage) return false;
    const rect = stage.getBoundingClientRect();
    const canvasData = imageCropper.getCanvasData();
    const cropBoxData = imageCropper.getCropBoxData();
    return rect.width > 10 &&
        canvasData.width >= rect.width * 0.95 &&
        canvasData.height >= rect.height * 0.95 &&
        cropBoxData.width >= rect.width * 0.9 &&
        cropBoxData.height >= rect.height * 0.9;
}

function scheduleImageCropperFit(attempt = 0) {
    if (!imageCropper || !sourceImage) return;
    const run = () => {
        if (!imageCropper || !sourceImage) return;
        updateImageStageVisibility();
        const fitted = fitImageCropperToStage();
        if (fitted) {
            syncCropperToLegacyState(true);
            syncImageCanvasFromCropper();
            updateImageStageVisibility();
        }
        if (!isImageCropperStageFitted() && attempt < 10) {
            window.setTimeout(() => scheduleImageCropperFit(attempt + 1), 60);
        }
    };

    if (typeof window.requestAnimationFrame === 'function') {
        window.requestAnimationFrame(run);
    } else {
        window.setTimeout(run, 0);
    }
}

function initImageCropper(imageSrc) {
    const imageEl = document.getElementById('cropperImage');
    if (!imageEl || !imageSrc || typeof Cropper === 'undefined') {
        updateImageStageVisibility();
        return false;
    }

    destroyImageCropper();
    imageEl.onload = () => {
        imageCropper = new Cropper(imageEl, {
            aspectRatio: EPD_CROP_ASPECT_RATIO,
            viewMode: 1,
            dragMode: 'move',
            autoCropArea: 1,
            background: false,
            responsive: true,
            restore: false,
            guides: true,
            center: true,
            cropBoxMovable: true,
            cropBoxResizable: true,
            toggleDragModeOnDblclick: false,
            ready() {
                scheduleImageCropperFit();
            },
            crop() {
                syncCropperToLegacyState(true);
            },
        });
        updateImageStageVisibility();
        scheduleImageCropperFit();
    };
    imageEl.src = imageSrc;
    window.setTimeout(() => scheduleImageCropperFit(), 120);
    return true;
}

function drawImageCoverToMainCanvas() {
    const canvas = document.getElementById('mainCanvas');
    if (!canvas || !sourceImage) return false;

    const width = parseInt(document.getElementById('width')?.value || '800', 10);
    const height = parseInt(document.getElementById('height')?.value || '480', 10);
    const scale = Math.max(width / sourceImage.width, height / sourceImage.height);
    const srcWidth = width / scale;
    const srcHeight = height / scale;
    const srcX = Math.max(0, (sourceImage.width - srcWidth) / 2);
    const srcY = Math.max(0, (sourceImage.height - srcHeight) / 2);

    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(sourceImage, srcX, srcY, srcWidth, srcHeight, 0, 0, width, height);

    imageScale = scale;
    cropX = srcX;
    cropY = srcY;
    updateScaleControls(scale);
    return true;
}

function getImageModeRenderCanvas(width = 800, height = 480) {
    if (!isImageCropperReady()) return null;
    const cropped = imageCropper.getCroppedCanvas({
        width,
        height,
        fillColor: '#ffffff',
        imageSmoothingEnabled: true,
        imageSmoothingQuality: 'high',
    });
    return cropped || null;
}

function syncImageCanvasFromCropper() {
    const canvas = document.getElementById('mainCanvas');
    if (!canvas || !isImageCropperReady()) return false;

    const width = parseInt(document.getElementById('width')?.value || '800', 10);
    const height = parseInt(document.getElementById('height')?.value || '480', 10);
    const cropped = getImageModeRenderCanvas(width, height);
    if (!cropped) return false;

    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(cropped, 0, 0, width, height);
    return true;
}

function registerFilePondPlugins() {
    if (filePondPluginsRegistered || typeof FilePond === 'undefined') return;
    const plugins = [
        window.FilePondPluginFileValidateType,
        window.FilePondPluginImageExifOrientation,
        window.FilePondPluginImagePreview,
    ].filter(Boolean);
    if (plugins.length > 0) {
        FilePond.registerPlugin(...plugins);
    }
    filePondPluginsRegistered = true;
}

function initImageFilePond(fileInput) {
    if (!fileInput || typeof FilePond === 'undefined') return false;
    registerFilePondPlugins();

    if (imageFilePond) {
        return true;
    }

    imageFilePond = FilePond.create(fileInput, {
        allowMultiple: false,
        allowReorder: false,
        allowProcess: false,
        allowRevert: false,
        credits: false,
        maxFiles: 1,
        acceptedFileTypes: ['image/png', 'image/jpeg', 'image/webp', 'image/gif', 'image/bmp'],
        labelIdle: '拖拽图片到这里或 <span class="filepond--label-action">选择文件</span>',
        labelFileTypeNotAllowed: '仅支持图片文件',
        fileValidateTypeLabelExpectedTypes: '支持 PNG、JPG、WebP、GIF、BMP',
    });

    imageFilePond.on('addfile', (error, fileItem) => {
        if (error) {
            log('图片加载失败: ' + (error.main || error.message || '文件无效'), 'error');
            return;
        }
        if (fileItem && fileItem.file) {
            handleImageFile(fileItem.file);
        }
    });

    return true;
}

// ==================== 模式切换 ====================
function switchMode(mode) {
    if (window.MEETING_NAMEPLATE_ONLY && mode !== 'template') {
        mode = 'template';
    }
    currentMode = mode;

    // 更新按钮状态
    document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.mode === mode);
    });

    // 显示对应控件
    const imageControls = document.getElementById('imageModeControls');
    const textControls = document.getElementById('textModeControls');
    const mixedControls = document.getElementById('mixedModeControls');
    const templateControls = document.getElementById('templateModeControls');

    if (imageControls) imageControls.classList.toggle('hidden', mode !== 'image');
    if (textControls) textControls.classList.toggle('hidden', mode !== 'text');
    if (mixedControls) mixedControls.classList.toggle('hidden', mode !== 'mixed');
    if (templateControls) templateControls.classList.toggle('hidden', mode !== 'template');
    if (mode === 'template' && window.MEETING_NAMEPLATE_ONLY) {
        currentTemplateId = NAMEPLATE_TEMPLATE_ID;
        updateTemplateConfigVisibility(NAMEPLATE_TEMPLATE_ID);
    }

    // 初始化画布
    if (mode === 'text') {
        if (typeof initTextCanvas === 'function') initTextCanvas();
    } else if (mode === 'mixed') {
        if (typeof initMixedCanvas === 'function') initMixedCanvas();
    } else if (mode === 'image') {
        prepareImageEditingTools();
    }

    // 确保画布样式在所有模式下一致
    const canvas = document.getElementById('mainCanvas');
    if (canvas) {
        canvas.style.maxWidth = '100%';
        canvas.style.height = 'auto';
        canvas.style.width = 'auto';
        canvas.style.aspectRatio = '800 / 480';
    }

    renderCanvas();
    updateImageStageVisibility();
    log(`切换到${mode === 'image' ? '图片' : mode === 'text' ? '文字' : mode === 'mixed' ? '图文' : '会议名牌'}模式`);
}

// ==================== 拖拽区域初始化 ====================
function initDropZones() {
    // 图片模式拖拽区
    const dropZone = document.getElementById('dropZone');
    const fileInput = document.getElementById('fileInput');

    if (dropZone && fileInput) {
        // 点击选择文件。增强上传组件只在用户真的进入图片入口时加载。
        dropZone.onclick = async () => {
            if (!imageFilePond) {
                const enhanced = await prepareImageEditingTools();
                if (enhanced && imageFilePond && typeof imageFilePond.browse === 'function') {
                    imageFilePond.browse();
                    return;
                }
            }
            fileInput.click();
        };

        // 拖拽事件保留原生路径，避免首屏加载 FilePond。
        dropZone.ondragover = (e) => {
            e.preventDefault();
            dropZone.classList.add('dragover');
        };
        dropZone.ondragleave = () => dropZone.classList.remove('dragover');
        dropZone.ondrop = (e) => {
            e.preventDefault();
            dropZone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) {
                handleImageFile(e.dataTransfer.files[0]);
            }
        };

        // 文件选择
        fileInput.onchange = (e) => {
            if (e.target.files.length > 0) {
                handleImageFile(e.target.files[0]);
            }
        };
    }

    // 混合模式拖拽区
    const mixedDropZone = document.getElementById('mixedDropZone');
    const mixedFileInput = document.getElementById('mixedFileInput');

    if (mixedDropZone && mixedFileInput) {
        mixedDropZone.onclick = () => mixedFileInput.click();

        mixedDropZone.ondragover = (e) => {
            e.preventDefault();
            mixedDropZone.classList.add('dragover');
        };
        mixedDropZone.ondragleave = () => mixedDropZone.classList.remove('dragover');
        mixedDropZone.ondrop = (e) => {
            e.preventDefault();
            mixedDropZone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) {
                handleMixedFile(e.dataTransfer.files[0]);
            }
        };

        mixedFileInput.onchange = (e) => {
            if (e.target.files.length > 0) {
                handleMixedFile(e.target.files[0]);
            }
        };
    }
}

// 处理图片文件（新版界面用）
async function handleImageFile(file) {
    if (!file.type.startsWith('image/')) {
        log('请选择图片文件', 'error');
        return;
    }

    log(`加载图片: ${file.name}`);
    const hasImageTools = await ensureImageEditingAssets();

    const reader = new FileReader();
    reader.onload = (e) => {
        const img = new Image();
        img.onload = () => {
            sourceImage = img;
            window.e6Data4bit = null;
            processedImageData = null;
            drawImageCoverToMainCanvas();
            updateImageStageVisibility();

            if (!hasImageTools || !initImageCropper(e.target.result)) {
                // 降级：没有 Cropper 时仍使用原 Canvas 裁剪逻辑。
                if (typeof fitToScreen === 'function') {
                    fitToScreen();
                } else {
                    renderCanvas();
                }
            } else if (currentMode !== 'image') {
                renderCanvas();
            }

            log(`图片加载成功: ${img.width}×${img.height}`, 'success');
        };
        img.src = e.target.result;
    };
    reader.readAsDataURL(file);
}

function handleMixedFile(file) {
    if (!file.type.startsWith('image/')) {
        log('请选择图片文件', 'error');
        return;
    }

    const reader = new FileReader();
    reader.onload = (e) => {
        const img = new Image();
        img.onload = () => {
            sourceImage = img;
            fitMixedToScreen();
            log(`图片加载成功: ${img.width}×${img.height}`, 'success');
        };
        img.src = e.target.result;
    };
    reader.readAsDataURL(file);
}

// ==================== 处理选项初始化 ====================
function initProcessOptions() {
    document.querySelectorAll('.process-option input').forEach(input => {
        input.addEventListener('change', () => {
            document.querySelectorAll('.process-option').forEach(opt => {
                opt.classList.toggle('active', opt.querySelector('input').checked);
            });
        });
    });
}

// ==================== 分辨率更新 ====================
// 覆盖 app.js 的 updateResolution，添加新UI支持
function updateResolution() {
    const epdTypeEl = document.getElementById('epdType');
    if (!epdTypeEl) return;

    const epdType = parseInt(epdTypeEl.value);
    // 固定为7.3寸E6：800×480
    const [width, height] = [800, 480];

    const widthEl = document.getElementById('width');
    const heightEl = document.getElementById('height');
    if (widthEl) widthEl.value = width;
    if (heightEl) heightEl.value = height;

    // 新版UI元素
    const resDisplay = document.getElementById('resolutionDisplay');
    const canvasInfo = document.getElementById('canvasInfo');
    if (resDisplay) resDisplay.textContent = `${width}×${height}`;
    if (canvasInfo) canvasInfo.textContent = `画布: ${width}×${height}`;

    // 更新画布大小
    const mainCanvas = document.getElementById('mainCanvas');
    if (mainCanvas) {
        mainCanvas.width = width;
        mainCanvas.height = height;
    }

    const processedCanvas = document.getElementById('processedCanvas');
    if (processedCanvas) {
        processedCanvas.width = width;
        processedCanvas.height = height;
    }

    renderCanvas();
    log(`分辨率已设置为: ${width}×${height}`);
}

// ==================== 缩放控制 ====================
function updateScale() {
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    if (slider && input) {
        imageScale = parseInt(slider.value) / 100;
        input.value = slider.value;
        if (isImageCropperReady()) {
            imageCropper.zoomTo(imageScale);
            syncCropperToLegacyState(true);
            syncImageCanvasFromCropper();
            return;
        }
        renderCanvas();
    }
}

function updateScaleFromInput() {
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    if (slider && input) {
        let value = parseInt(input.value) || 100;
        value = Math.max(10, Math.min(500, value));
        input.value = value;
        slider.value = Math.min(300, value);
        imageScale = value / 100;
        if (isImageCropperReady()) {
            imageCropper.zoomTo(imageScale);
            syncCropperToLegacyState(true);
            syncImageCanvasFromCropper();
            return;
        }
        renderCanvas();
    }
}

function updateMixedScale() {
    const slider = document.getElementById('mixedScaleSlider');
    const input = document.getElementById('mixedScaleInput');
    if (slider && input) {
        mixedImageScale = parseInt(slider.value) / 100;
        input.value = slider.value;
        renderCanvas();
    }
}

function fitToScreen() {
    if (!sourceImage) {
        log('请先选择图片', 'error');
        return;
    }

    if (isImageCropperReady()) {
        imageCropper.reset();
        imageCropper.setAspectRatio(EPD_CROP_ASPECT_RATIO);
        fitImageCropperToStage();
        syncCropperToLegacyState(true);
        syncImageCanvasFromCropper();
        log(`已适应屏幕，裁剪比例: 800×480`, 'success');
        return;
    }

    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);

    // 计算缩放比例
    const scaleX = width / sourceImage.width;
    const scaleY = height / sourceImage.height;
    imageScale = Math.max(scaleX, scaleY);

    // 更新UI
    const sliderValue = Math.round(imageScale * 100);
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    if (slider) slider.value = Math.min(300, Math.max(10, sliderValue));
    if (input) input.value = sliderValue;

    // 居中
    const srcWidth = width / imageScale;
    const srcHeight = height / imageScale;
    cropX = Math.max(0, (sourceImage.width - srcWidth) / 2);
    cropY = Math.max(0, (sourceImage.height - srcHeight) / 2);

    renderCanvas();
    log(`已适应屏幕，缩放: ${sliderValue}%`, 'success');
}

function fitMixedToScreen() {
    if (!sourceImage) {
        log('请先选择图片', 'error');
        return;
    }

    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);

    const scaleX = width / sourceImage.width;
    const scaleY = height / sourceImage.height;
    mixedImageScale = Math.max(scaleX, scaleY);

    const sliderValue = Math.round(mixedImageScale * 100);
    const slider = document.getElementById('mixedScaleSlider');
    const input = document.getElementById('mixedScaleInput');
    if (slider) slider.value = Math.min(300, Math.max(10, sliderValue));
    if (input) input.value = sliderValue;

    const srcWidth = width / mixedImageScale;
    const srcHeight = height / mixedImageScale;
    mixedCropX = Math.max(0, (sourceImage.width - srcWidth) / 2);
    mixedCropY = Math.max(0, (sourceImage.height - srcHeight) / 2);

    renderCanvas();
    log(`已适应屏幕，缩放: ${sliderValue}%`, 'success');
}

function resetCrop() {
    if (isImageCropperReady()) {
        imageCropper.reset();
        fitImageCropperToStage();
        syncCropperToLegacyState(true);
        syncImageCanvasFromCropper();
        log('已重置裁剪');
        return;
    }

    imageScale = 1;
    cropX = 0;
    cropY = 0;

    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    if (slider) slider.value = 100;
    if (input) input.value = 100;

    renderCanvas();
    log('已重置裁剪');
}

// ==================== 画布事件绑定 ====================
// 使用全局变量来跟踪拖动状态，确保即使鼠标移出画布也能继续拖动
var canvasDragState = {
    isDragging: false,
    dragStartX: 0,
    dragStartY: 0,
    itemOffsetX: 0,
    itemOffsetY: 0
};

function initCanvasEvents() {
    const canvas = document.getElementById('mainCanvas');
    if (!canvas) {
        console.warn('[Editor] mainCanvas not found');
        return;
    }

    // 获取画布坐标的辅助函数
    function getCanvasCoords(e) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        return {
            x: (e.clientX - rect.left) * scaleX,
            y: (e.clientY - rect.top) * scaleY
        };
    }

    // 检测点击的文字项
    function findClickedTextItem(x, y, items, selectedIdVar) {
        const ctx = canvas.getContext('2d');
        for (let i = items.length - 1; i >= 0; i--) {
            const item = items[i];
            ctx.font = epdCanvasFont(item.size);
            const metrics = ctx.measureText(item.text);

            // 改进检测区域：文字的实际渲染区域
            const textWidth = metrics.width;
            const textHeight = item.size;
            const padding = 5; // 增加点击区域

            if (x >= item.x - padding && x <= item.x + textWidth + padding &&
                y >= item.y - padding && y <= item.y + textHeight + padding) {
                return item;
            }
        }
        return null;
    }

    canvas.onmousedown = function(e) {
        e.preventDefault();
        e.stopPropagation();

        const coords = getCanvasCoords(e);
        const x = coords.x;
        const y = coords.y;

        if (currentMode === 'text') {
            // 文字模式：检查点击了哪个文字
            const clickedItem = findClickedTextItem(x, y, textItems);

            if (clickedItem) {
                selectedTextId = clickedItem.id;
                canvasDragState.isDragging = true;
                canvasDragState.itemOffsetX = x - clickedItem.x;
                canvasDragState.itemOffsetY = y - clickedItem.y;
                canvasDragState.dragStartX = x;
                canvasDragState.dragStartY = y;
                renderCanvas();
                if (typeof updateTextItemsList === 'function') updateTextItemsList();
            } else {
                selectedTextId = null;
                renderCanvas();
                if (typeof updateTextItemsList === 'function') updateTextItemsList();
            }

        } else if (currentMode === 'mixed') {
            // 图文模式：检查点击了哪个文字
            const clickedItem = findClickedTextItem(x, y, mixedTextItems);

            if (clickedItem) {
                selectedMixedTextId = clickedItem.id;
                canvasDragState.isDragging = true;
                canvasDragState.itemOffsetX = x - clickedItem.x;
                canvasDragState.itemOffsetY = y - clickedItem.y;
                canvasDragState.dragStartX = x;
                canvasDragState.dragStartY = y;
                renderCanvas();
                if (typeof updateMixedTextItemsList === 'function') updateMixedTextItemsList();
            } else {
                selectedMixedTextId = null;
                // 如果没有点击文字，且存在图片，则允许拖动图片
                if (sourceImage) {
                    canvasDragState.isDragging = true;
                    canvasDragState.dragStartX = x;
                    canvasDragState.dragStartY = y;
                    // 确保变量已初始化
                    if (typeof mixedImageScale === 'undefined') {
                        window.mixedImageScale = 1;
                    }
                    if (typeof mixedCropX === 'undefined') {
                        window.mixedCropX = 0;
                    }
                    if (typeof mixedCropY === 'undefined') {
                        window.mixedCropY = 0;
                    }
                    console.log('[Editor] 图文模式：开始拖动图片，mixedImageScale=', window.mixedImageScale);
                }
                renderCanvas();
                if (typeof updateMixedTextItemsList === 'function') updateMixedTextItemsList();
            }

        } else if (currentMode === 'image' && sourceImage) {
            // 图片模式：拖动图片
            canvasDragState.isDragging = true;
            canvasDragState.dragStartX = x;
            canvasDragState.dragStartY = y;
        }
    };

    canvas.onmousemove = function(e) {
        if (!canvasDragState.isDragging) return;
        e.preventDefault();

        const coords = getCanvasCoords(e);
        const x = coords.x;
        const y = coords.y;

        if (currentMode === 'text' && selectedTextId) {
            const item = textItems.find(t => t.id === selectedTextId);
            if (item) {
                const newX = x - canvasDragState.itemOffsetX;
                const newY = y - canvasDragState.itemOffsetY;
                item.x = Math.max(0, Math.min(canvas.width - 10, newX));
                item.y = Math.max(0, Math.min(canvas.height - item.size, newY));
                renderCanvas();
            }
        } else if (currentMode === 'mixed') {
            if (selectedMixedTextId) {
                // 拖动文字
                const item = mixedTextItems.find(t => t.id === selectedMixedTextId);
                if (item) {
                    const newX = x - canvasDragState.itemOffsetX;
                    const newY = y - canvasDragState.itemOffsetY;
                    item.x = Math.max(0, Math.min(canvas.width - 10, newX));
                    item.y = Math.max(0, Math.min(canvas.height - item.size, newY));
                    renderCanvas();
                }
            } else if (sourceImage) {
                // 拖动图片
                const scale = (typeof window.mixedImageScale !== 'undefined' && window.mixedImageScale > 0) ? window.mixedImageScale :
                              (typeof mixedImageScale !== 'undefined' && mixedImageScale > 0) ? mixedImageScale : 1;
                const dx = (x - canvasDragState.dragStartX) / scale;
                const dy = (y - canvasDragState.dragStartY) / scale;

                // 使用全局变量或局部变量
                if (typeof window.mixedCropX !== 'undefined') {
                    window.mixedCropX = Math.max(0, Math.min(sourceImage.width - canvas.width / scale, window.mixedCropX - dx));
                    window.mixedCropY = Math.max(0, Math.min(sourceImage.height - canvas.height / scale, window.mixedCropY - dy));
                } else if (typeof mixedCropX !== 'undefined') {
                    mixedCropX = Math.max(0, Math.min(sourceImage.width - canvas.width / scale, mixedCropX - dx));
                    mixedCropY = Math.max(0, Math.min(sourceImage.height - canvas.height / scale, mixedCropY - dy));
                }

                canvasDragState.dragStartX = x;
                canvasDragState.dragStartY = y;
                renderCanvas();
            }
        } else if (currentMode === 'image' && sourceImage) {
            // 拖动图片裁剪区域
            const dx = (x - canvasDragState.dragStartX) / imageScale;
            const dy = (y - canvasDragState.dragStartY) / imageScale;
            cropX = Math.max(0, Math.min(sourceImage.width - canvas.width / imageScale, cropX - dx));
            cropY = Math.max(0, Math.min(sourceImage.height - canvas.height / imageScale, cropY - dy));
            canvasDragState.dragStartX = x;
            canvasDragState.dragStartY = y;
            renderCanvas();
        }
    };

    canvas.onmouseup = function(e) {
        e.preventDefault();
        canvasDragState.isDragging = false;
    };

    canvas.onmouseleave = function(e) {
        // 不在这里停止拖动，允许鼠标移出画布后继续拖动
    };

    // 全局鼠标事件，确保即使鼠标移出画布也能继续拖动
    document.addEventListener('mousemove', function(e) {
        if (!canvasDragState.isDragging) return;

        const canvas = document.getElementById('mainCanvas');
        if (!canvas) return;

        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;

        // 限制在画布范围内
        if (x < 0 || x > canvas.width || y < 0 || y > canvas.height) return;

        if (currentMode === 'text' && selectedTextId) {
            const item = textItems.find(t => t.id === selectedTextId);
            if (item) {
                const newX = x - canvasDragState.itemOffsetX;
                const newY = y - canvasDragState.itemOffsetY;
                item.x = Math.max(0, Math.min(canvas.width - 10, newX));
                item.y = Math.max(0, Math.min(canvas.height - item.size, newY));
                renderCanvas();
            }
        } else if (currentMode === 'mixed') {
            if (selectedMixedTextId) {
                // 拖动文字
                const item = mixedTextItems.find(t => t.id === selectedMixedTextId);
                if (item) {
                    const newX = x - canvasDragState.itemOffsetX;
                    const newY = y - canvasDragState.itemOffsetY;
                    item.x = Math.max(0, Math.min(canvas.width - 10, newX));
                    item.y = Math.max(0, Math.min(canvas.height - item.size, newY));
                    renderCanvas();
                }
            } else if (sourceImage) {
                // 拖动图片
                const scale = (typeof window.mixedImageScale !== 'undefined' && window.mixedImageScale > 0) ? window.mixedImageScale :
                              (typeof mixedImageScale !== 'undefined' && mixedImageScale > 0) ? mixedImageScale : 1;
                const dx = (x - canvasDragState.dragStartX) / scale;
                const dy = (y - canvasDragState.dragStartY) / scale;

                // 使用全局变量或局部变量
                if (typeof window.mixedCropX !== 'undefined') {
                    window.mixedCropX = Math.max(0, Math.min(sourceImage.width - canvas.width / scale, window.mixedCropX - dx));
                    window.mixedCropY = Math.max(0, Math.min(sourceImage.height - canvas.height / scale, window.mixedCropY - dy));
                } else if (typeof mixedCropX !== 'undefined') {
                    mixedCropX = Math.max(0, Math.min(sourceImage.width - canvas.width / scale, mixedCropX - dx));
                    mixedCropY = Math.max(0, Math.min(sourceImage.height - canvas.height / scale, mixedCropY - dy));
                }

                canvasDragState.dragStartX = x;
                canvasDragState.dragStartY = y;
                renderCanvas();
            }
        }
    });

    document.addEventListener('mouseup', function(e) {
        canvasDragState.isDragging = false;
    });

    // 设置鼠标样式
    canvas.style.cursor = 'grab';
    console.log('[Editor] 画布事件已绑定');
}

// ==================== 渲染画布 ====================
function renderCanvas() {
    const canvas = document.getElementById('mainCanvas');
    if (!canvas) return;

    updateImageStageVisibility();
    if (currentMode === 'image' && sourceImage && imageCropper && syncImageCanvasFromCropper()) {
        return;
    }

    const ctx = canvas.getContext('2d');

    // 清空画布
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    if (currentMode === 'image' && sourceImage) {
        // 绘制图片
        const srcWidth = canvas.width / imageScale;
        const srcHeight = canvas.height / imageScale;
        ctx.drawImage(sourceImage, cropX, cropY, srcWidth, srcHeight, 0, 0, canvas.width, canvas.height);
    } else if (currentMode === 'text') {
        // 绘制文字
        const bgColor = document.getElementById('textBgColor')?.value || 'white';
        ctx.fillStyle = bgColor;
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        textItems.forEach(item => {
            ctx.font = epdCanvasFont(item.size);
            ctx.fillStyle = item.color;
            ctx.textBaseline = 'top';
            ctx.fillText(item.text, item.x, item.y);

            if (item.id === selectedTextId) {
                const metrics = ctx.measureText(item.text);
                ctx.strokeStyle = '#667eea';
                ctx.lineWidth = 2;
                ctx.setLineDash([5, 5]);
                ctx.strokeRect(item.x - 5, item.y - 5, metrics.width + 10, item.size + 10);
                ctx.setLineDash([]);
            }
        });
    } else if (currentMode === 'mixed') {
        // 绘制图片和文字
        if (sourceImage) {
            // 优先使用全局变量，然后是局部变量
            const scale = (typeof window.mixedImageScale !== 'undefined' && window.mixedImageScale > 0) ? window.mixedImageScale :
                          (typeof mixedImageScale !== 'undefined' && mixedImageScale > 0) ? mixedImageScale : 1;
            const cropX = (typeof window.mixedCropX !== 'undefined') ? window.mixedCropX :
                          (typeof mixedCropX !== 'undefined') ? mixedCropX : 0;
            const cropY = (typeof window.mixedCropY !== 'undefined') ? window.mixedCropY :
                          (typeof mixedCropY !== 'undefined') ? mixedCropY : 0;
            const srcWidth = canvas.width / scale;
            const srcHeight = canvas.height / scale;
            ctx.drawImage(sourceImage, cropX, cropY, srcWidth, srcHeight, 0, 0, canvas.width, canvas.height);
        }

        mixedTextItems.forEach(item => {
            ctx.font = epdCanvasFont(item.size);
            ctx.fillStyle = item.color;
            ctx.textBaseline = 'top';
            ctx.fillText(item.text, item.x, item.y);

            if (item.id === selectedMixedTextId) {
                const metrics = ctx.measureText(item.text);
                ctx.strokeStyle = '#667eea';
                ctx.lineWidth = 2;
                ctx.setLineDash([5, 5]);
                ctx.strokeRect(item.x - 5, item.y - 5, metrics.width + 10, item.size + 10);
                ctx.setLineDash([]);
            }
        });
    } else if (currentMode === 'template') {
        renderTemplateCanvas(ctx, canvas.width, canvas.height);
    }
}

function renderTemplateCanvas(ctx, width, height) {
    // 清空画布
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);

    const templateId = currentTemplateId || (window.MEETING_NAMEPLATE_ONLY ? NAMEPLATE_TEMPLATE_ID : null);
    if (!templateId) {
        // 未选择模板，保持空白
        return;
    }

    switch (templateId) {
        case 'calendar':
            renderCalendarTemplate(ctx, width, height);
            break;
        case 'weather':
            renderWeatherTemplate(ctx, width, height);
            break;
        case 'quote':
            renderQuoteTemplate(ctx, width, height);
            break;
        case 'qrcode':
            renderQRCodeTemplate(ctx, width, height);
            break;
        case 'nameplate':
            renderNameplateTemplate(ctx, width, height);
            break;
        case 'todo':
            // 代办事项占位
            ctx.font = epdCanvasFont(48, '700');
            ctx.fillStyle = 'black';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('待办事项', width / 2, height / 2 - 40);
            ctx.font = epdCanvasFont(28, '600');
            ctx.fillStyle = 'black';
            ctx.fillText('功能开发中...', width / 2, height / 2 + 30);
            break;
        default:
            break;
    }
}

// ==================== 预览 ====================
function previewPage() {
    try {
        // 检查当前模式
        if (currentMode === 'image') {
            // 图片模式：检查是否有图片
            if (!sourceImage) {
                log('请先选择图片', 'error');
                return;
            }
        } else if (currentMode === 'text') {
            // 文字模式：检查是否有文字
            if (!textItems || textItems.length === 0) {
                log('请先添加文字', 'error');
                return;
            }
        } else if (currentMode === 'mixed') {
            // 图文模式：检查是否有内容
            if (!sourceImage && (!mixedTextItems || mixedTextItems.length === 0)) {
                log('请先添加图片或文字', 'error');
                return;
            }
        }

        // 先处理图片/内容
        if (typeof processImage === 'function') {
            processImage();
        } else {
            log('处理函数未找到', 'error');
            return;
        }

        // 检查处理是否成功（检查 processedCanvas 是否有内容）
        const processedCanvas = document.getElementById('processedCanvas');
        if (!processedCanvas) {
            log('找不到预览画布', 'error');
            return;
        }

        // 切换到处理面板显示预览
        switchPanel('process');

        // 确保画布可见
        if (processedCanvas.width > 0 && processedCanvas.height > 0) {
            log('预览已生成', 'success');
        } else {
            log('预览生成失败，请检查内容是否已加载', 'error');
        }
    } catch (error) {
        console.error('预览生成错误:', error);
        log('预览生成失败: ' + error.message, 'error');
    }
}

// ==================== 日志 ====================
function log(message, type = 'info') {
    const statusText = document.getElementById('statusText');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    statusText.textContent = `${emoji} ${message}`;
    console.log(`[${timestamp}] ${message}`);
}
