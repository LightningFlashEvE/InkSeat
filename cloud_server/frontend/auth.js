// 简单用户认证工具
const AUTH_API_BASE = '';

function getAuthToken() {
    return localStorage.getItem('authToken') || '';
}

function setAuth(token, user) {
    if (token) {
        localStorage.setItem('authToken', token);
    }
    if (user) {
        localStorage.setItem('authUser', JSON.stringify(user));
    }
}

function clearAuth() {
    localStorage.removeItem('authToken');
    localStorage.removeItem('authUser');
}

function getAuthHeaders() {
    const token = getAuthToken();
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

async function requireAuth() {
    const token = getAuthToken();
    if (!token) {
        window.location.href = 'login.html?v=20260707auth1';
        return null;
    }

    try {
        const res = await fetch(`${AUTH_API_BASE}/api/auth/me`, {
            headers: {
                ...getAuthHeaders()
            }
        });
        if (res.ok) {
            const data = await res.json();
            return data.user || null;
        } else {
            clearAuth();
            window.location.href = 'login.html?v=20260707auth1';
            return null;
        }
    } catch (e) {
        console.error('auth check error', e);
        clearAuth();
        window.location.href = 'login.html?v=20260707auth1';
        return null;
    }
}

