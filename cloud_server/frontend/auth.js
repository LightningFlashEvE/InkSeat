// 简单用户认证工具
const AUTH_API_BASE = '';
const AUTH_LOGIN_URL = 'login.html?v=20260803people1';
let authRedirectInProgress = false;

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

class AuthCheckError extends Error {
    constructor(message, code, status = 0) {
        super(message);
        this.name = 'AuthCheckError';
        this.code = code;
        this.status = status;
        this.transient = true;
    }
}

function expireAuthSession() {
    clearAuth();

    if (authRedirectInProgress) {
        return;
    }
    authRedirectInProgress = true;
    window.dispatchEvent(new Event('auth:expired'));

    if (!/(?:^|\/)login\.html$/i.test(window.location.pathname)) {
        window.location.replace(AUTH_LOGIN_URL);
    }
}

async function authFetch(input, init = {}) {
    const headers = new Headers(init.headers || {});
    const token = getAuthToken();
    if (token && !headers.has('Authorization')) {
        headers.set('Authorization', `Bearer ${token}`);
    }

    // Network failures and 5xx responses must not destroy a valid local
    // session. Callers keep their existing error/empty-state handling.
    const response = await fetch(input, {
        ...init,
        headers
    });

    if (response.status !== 401) {
        return response;
    }

    expireAuthSession();
    const error = new AuthCheckError('登录已失效，请重新登录', 'auth_expired', 401);
    error.transient = false;
    throw error;
}

async function requireAuth() {
    const token = getAuthToken();
    if (!token) {
        expireAuthSession();
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
            if (!data.user) {
                throw new AuthCheckError('认证服务返回了无效响应，请稍后重试', 'auth_invalid_response', res.status);
            }
            return data.user;
        }

        if (res.status === 401 || res.status === 403) {
            expireAuthSession();
            return null;
        }

        throw new AuthCheckError(
            `认证服务暂不可用（HTTP ${res.status}），登录状态已保留，请稍后重试`,
            'auth_service_unavailable',
            res.status
        );
    } catch (e) {
        console.error('auth check error', e);
        if (e instanceof AuthCheckError) {
            throw e;
        }
        throw new AuthCheckError('无法连接认证服务，登录状态已保留，请检查网络后重试', 'auth_network_error');
    }
}

async function logout() {
    if (!window.confirm('确定要退出登录吗？')) {
        return;
    }

    try {
        if (getAuthToken()) {
            await fetch(`${AUTH_API_BASE}/api/auth/logout`, {
                method: 'POST',
                headers: {
                    ...getAuthHeaders()
                },
                keepalive: true
            });
        }
    } catch (e) {
        console.warn('logout request failed', e);
    } finally {
        clearAuth();
        window.location.replace(AUTH_LOGIN_URL);
    }
}

