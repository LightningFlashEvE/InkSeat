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
var pageLists = [];
var templates = [];
var currentPageId = null;
var currentTemplateId = null;

// 注意：以下变量在 app.js 中已定义，这里不再声明
// currentMode, sourceImage, textItems, mixedTextItems, 
// selectedTextId, selectedMixedTextId, imageScale, mixedImageScale,
// cropX, cropY, mixedCropX, mixedCropY, processedImageData, redChannelData

// ==================== 初始化 ====================
document.addEventListener('DOMContentLoaded', async () => {
    console.log('[Editor] 开始初始化...');
    
    try {
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
            if (deviceNameDisplay) deviceNameDisplay.textContent = '未选择设备';
            if (statusDot) statusDot.classList.add('offline');
        }
        
        // 初始化
        await loadTemplates();
        await loadPages();
        initDropZones();
        initProcessOptions();
        updateResolution();
        initCanvasEvents();  // 绑定画布事件
        
        console.log('[Editor] 初始化完成');
        log('系统初始化完成');
    } catch (error) {
        console.error('[Editor] 初始化错误:', error);
        log('初始化错误: ' + error.message, 'error');
    }
});

// ==================== 模板管理 ====================
async function loadTemplates() {
    try {
        const response = await fetch(`${API_BASE}/api/templates`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json();
        if (result.success) {
            templates = result.templates;
            renderTemplateGrid();
            renderModalTemplateGrid();
        }
    } catch (e) {
        console.error('Failed to load templates:', e);
    }
}

function renderTemplateGrid() {
    const grid = document.getElementById('templateGrid');
    if (!grid) return;
    
    grid.innerHTML = templates.map(t => `
        <div class="template-card" onclick="selectTemplate('${t.templateId}')">
            <div class="icon">${t.icon}</div>
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
            <div class="icon">${t.icon}</div>
            <div class="name">${t.name}</div>
            <div class="desc">${t.description}</div>
        </div>
    `).join('');
}

function getLoadedTemplate(templateId) {
    return templates.find(t => t.templateId === templateId);
}

function selectTemplate(templateId) {
    const template = getLoadedTemplate(templateId);
    if (template) {
        log(`选择模板: ${template.name}`);
        // TODO: 应用模板到画布
        applyTemplate(template);
    }
}

function applyTemplate(template) {
    // 统一用 renderCanvas 渲染，避免“加载模板页面后白屏/不显示”
    currentMode = 'template';
    currentTemplateId = template.templateId;
    renderCanvas();
    log(`已应用模板: ${template.name}`, 'success');
}

function renderClockTemplate(ctx, width, height) {
    const now = new Date();
    const timeStr = now.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
    const dateStr = now.toLocaleDateString('zh-CN', { year: 'numeric', month: 'long', day: 'numeric' });
    const weekDay = ['星期日', '星期一', '星期二', '星期三', '星期四', '星期五', '星期六'][now.getDay()];
    
    // 时间
    ctx.font = 'bold 120px Arial';
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(timeStr, width / 2, height / 2 - 50);
    
    // 日期
    ctx.font = '36px Arial';
    ctx.fillText(dateStr, width / 2, height / 2 + 60);
    
    // 星期
    ctx.font = '28px Arial';
    ctx.fillStyle = 'red';
    ctx.fillText(weekDay, width / 2, height / 2 + 110);
}

function renderCalendarTemplate(ctx, width, height) {
    const now = new Date();
    const year = now.getFullYear();
    const month = now.getMonth();
    const day = now.getDate();
    
    // 标题
    ctx.font = 'bold 48px Arial';
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.fillText(`${year}年${month + 1}月`, width / 2, 60);
    
    // 星期标题
    const weekDays = ['日', '一', '二', '三', '四', '五', '六'];
    ctx.font = '24px Arial';
    const cellWidth = (width - 80) / 7;
    const startX = 40;
    
    weekDays.forEach((d, i) => {
        ctx.fillStyle = (i === 0 || i === 6) ? 'red' : 'black';
        ctx.fillText(d, startX + cellWidth * i + cellWidth / 2, 120);
    });
    
    // 日期网格
    const firstDay = new Date(year, month, 1).getDay();
    const daysInMonth = new Date(year, month + 1, 0).getDate();
    
    ctx.font = '28px Arial';
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
    const quotes = [
        { text: '千里之行，始于足下', author: '老子' },
        { text: '学而不思则罔，思而不学则殆', author: '孔子' },
        { text: '天行健，君子以自强不息', author: '周易' },
        { text: '不积跬步，无以至千里', author: '荀子' },
        { text: '知之者不如好之者，好之者不如乐之者', author: '孔子' }
    ];
    
    const quote = quotes[Math.floor(Math.random() * quotes.length)];
    
    // 引号装饰
    ctx.font = '120px Georgia';
    ctx.fillStyle = '#ddd';
    ctx.textAlign = 'left';
    ctx.fillText('"', 60, 150);
    
    // 引文
    ctx.font = '48px Arial';
    ctx.fillStyle = 'black';
    ctx.textAlign = 'center';
    ctx.fillText(quote.text, width / 2, height / 2);
    
    // 作者
    ctx.font = '28px Arial';
    ctx.fillStyle = 'red';
    ctx.fillText(`—— ${quote.author}`, width / 2, height / 2 + 80);
}

function renderQRCodeTemplate(ctx, width, height) {
    ctx.font = '36px Arial';
    ctx.fillStyle = '#888';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('请在设置中配置二维码内容', width / 2, height / 2);
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
            pages = result.pages;
            renderPageList();
        }
    } catch (e) {
        console.error('Failed to load pages:', e);
    }
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
        'template': '📋',
        'custom': '⬜'
    };
    return icons[type] || '📄';
}

function getPageTypeName(type) {
    const names = {
        'image': '图片',
        'text': '文字',
        'mixed': '图文',
        'template': '模板',
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
            renderCanvas();
        };
        img.src = data.imageData;
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
        log('请先选择设备', 'error');
        return;
    }
    
    // 获取画布缩略图和完整画布数据
    const canvas = document.getElementById('mainCanvas');
    const thumbnail = canvas.toDataURL('image/jpeg', 0.5);
    
    // 收集页面数据
    // - 非模板：保存 imageData（base64）+ 文字叠加
    // - 模板：只保存 templateId（避免大 base64 导致“保存很慢/请求很大”）
    let pageData;
    if (currentMode === 'template') {
        pageData = {
            mode: currentMode,
            template: currentTemplateId || null
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
    document.getElementById('newPageModal').classList.add('show');
    document.getElementById('newPageName').value = '';
    document.getElementById('newPageName').focus();
}

function hideNewPageModal() {
    document.getElementById('newPageModal').classList.remove('show');
}

async function createPageFromTemplate(templateId) {
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

// ==================== 页面列表管理 ====================
function showPageListModal() {
    document.getElementById('pageListModal').classList.add('show');
    loadPageLists();
}

function hidePageListModal() {
    document.getElementById('pageListModal').classList.remove('show');
}

async function loadPageLists() {
    if (!deviceId) return;
    
    try {
        const response = await fetch(`${API_BASE}/api/page-lists/list/${deviceId}`, {
            headers: typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}
        });
        const result = await response.json();
        if (result.success) {
            pageLists = result.pageLists;
            renderPageListsModal();
        }
    } catch (e) {
        console.error('Failed to load page lists:', e);
    }
}

function renderPageListsModal() {
    const container = document.getElementById('pageListsContainer');
    const allPagesContainer = document.getElementById('allPagesContainer');
    
    if (pageLists.length === 0) {
        container.innerHTML = `
            <div style="padding: 30px; text-align: center; color: var(--text-light);">
                暂无页面列表
            </div>
        `;
    } else {
        container.innerHTML = pageLists.map(pl => `
            <div style="padding: 12px; border-bottom: 1px solid var(--border); cursor: pointer; 
                        ${pl.isActive ? 'background: #ebf4ff;' : ''}"
                 onclick="selectPageList('${pl.listId}')">
                <div style="display: flex; align-items: center; gap: 10px;">
                    ${pl.isActive ? '⭐' : '📋'}
                    <span style="flex: 1; font-weight: 500;">${pl.name}</span>
                    <span style="font-size: 0.85em; color: var(--text-light);">${pl.pages?.length || 0} 页</span>
                </div>
                <div style="font-size: 0.8em; color: var(--text-light); margin-top: 5px;">
                    间隔: ${pl.interval} 分钟
                </div>
            </div>
        `).join('');
    }
    
    // 所有页面
    allPagesContainer.innerHTML = pages.map(page => `
        <div style="padding: 10px; border-bottom: 1px solid var(--border); display: flex; align-items: center;">
            <span style="margin-right: 10px;">${getPageIcon(page.type)}</span>
            <span style="flex: 1;">${page.name}</span>
            <button onclick="addPageToList('${page.pageId}')" 
                    style="background: var(--primary); color: white; border: none; 
                           padding: 4px 10px; border-radius: 4px; cursor: pointer;">+</button>
        </div>
    `).join('') || '<div style="padding: 30px; text-align: center; color: var(--text-light);">暂无页面</div>';
}

async function createPageList() {
    const name = prompt('请输入页面列表名称:', '新页面列表');
    if (!name) return;
    
    try {
        const response = await fetch(`${API_BASE}/api/page-lists/save`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...(typeof getAuthHeaders === 'function' ? getAuthHeaders() : {}) },
            body: JSON.stringify({
                deviceId,
                name,
                pages: [],
                interval: 60,
                isActive: pageLists.length === 0
            })
        });
        
        const result = await response.json();
        if (result.success) {
            await loadPageLists();
            log('页面列表已创建', 'success');
        }
    } catch (e) {
        log('创建失败', 'error');
    }
}

// ==================== 部署 ====================
async function deployToDevice() {
    if (!deviceId) {
        log('请先选择设备', 'error');
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

// ==================== 模式切换 ====================
function switchMode(mode) {
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
    
    // 初始化画布
    if (mode === 'text') {
        if (typeof initTextCanvas === 'function') initTextCanvas();
    } else if (mode === 'mixed') {
        if (typeof initMixedCanvas === 'function') initMixedCanvas();
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
    log(`切换到${mode === 'image' ? '图片' : mode === 'text' ? '文字' : mode === 'mixed' ? '图文' : '模板'}模式`);
}

// ==================== 拖拽区域初始化 ====================
function initDropZones() {
    // 图片模式拖拽区
    const dropZone = document.getElementById('dropZone');
    const fileInput = document.getElementById('fileInput');
    
    if (dropZone && fileInput) {
        // 点击选择文件
        dropZone.onclick = () => fileInput.click();
        
        // 拖拽事件
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
function handleImageFile(file) {
    if (!file.type.startsWith('image/')) {
        log('请选择图片文件', 'error');
        return;
    }
    
    log(`加载图片: ${file.name}`);
    
    const reader = new FileReader();
    reader.onload = (e) => {
        const img = new Image();
        img.onload = () => {
            sourceImage = img;
            
            // 新版界面：自动适应屏幕
            if (typeof fitToScreen === 'function') {
                fitToScreen();
            } else {
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
            ctx.font = `${item.size}px Arial, sans-serif`;
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
            ctx.font = `${item.size}px Arial, sans-serif`;
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
            ctx.font = `${item.size}px Arial, sans-serif`;
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

    const templateId = currentTemplateId;
    if (!templateId) {
        // 未选择模板，保持空白
        return;
    }

    switch (templateId) {
        case 'clock':
            renderClockTemplate(ctx, width, height);
            break;
        case 'calendar':
            renderCalendarTemplate(ctx, width, height);
            break;
        case 'quote':
            renderQuoteTemplate(ctx, width, height);
            break;
        case 'qrcode':
            renderQRCodeTemplate(ctx, width, height);
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
