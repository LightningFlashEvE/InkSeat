(function () {
    const STORAGE_KEY = 'meetingThemeMode';
    function normalizeTheme(value) {
        return value === 'light' ? 'light' : 'dark';
    }

    function readTheme() {
        try {
            return localStorage.getItem(STORAGE_KEY) === 'light' ? 'light' : 'dark';
        } catch (e) {
            return 'dark';
        }
    }

    function storeTheme(theme) {
        try {
            if (theme === 'light') {
                localStorage.setItem(STORAGE_KEY, 'light');
            } else {
                localStorage.removeItem(STORAGE_KEY);
            }
        } catch (e) {
            // Theme persistence is a local preference; keep the UI usable if storage is unavailable.
        }
    }

    function updateControls(theme) {
        const isLight = theme === 'light';

        document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
            button.setAttribute('aria-pressed', String(isLight));
            button.setAttribute('aria-label', isLight ? '切换到暗色主题' : '切换到明亮主题');
            button.title = isLight ? '切换到暗色主题' : '切换到明亮主题';
        });

        document.querySelectorAll('[data-theme-current]').forEach((node) => {
            node.textContent = isLight ? '明亮' : '暗色';
        });

        document.querySelectorAll('[data-theme-set]').forEach((control) => {
            const selected = normalizeTheme(control.dataset.themeSet) === theme;
            control.classList.toggle('is-active', selected);
            control.setAttribute('aria-pressed', String(selected));
        });
    }

    function applyTheme(theme) {
        const nextTheme = normalizeTheme(theme);
        const isLight = nextTheme === 'light';

        document.documentElement.classList.toggle('theme-light', isLight);
        document.documentElement.style.colorScheme = isLight ? 'light' : '';

        if (isLight) {
            document.documentElement.dataset.theme = 'light';
        } else {
            delete document.documentElement.dataset.theme;
        }

        if (document.body) {
            document.body.classList.toggle('theme-light', isLight);
            document.body.style.colorScheme = isLight ? 'light' : '';

            if (isLight) {
                document.body.dataset.theme = 'light';
            } else {
                delete document.body.dataset.theme;
            }
        }

        updateControls(nextTheme);
    }

    function setTheme(theme) {
        const nextTheme = normalizeTheme(theme);
        storeTheme(nextTheme);
        applyTheme(nextTheme);
    }

    function toggleTheme() {
        const currentTheme = document.documentElement.dataset.theme === 'light' ? 'light' : 'dark';
        setTheme(currentTheme === 'light' ? 'dark' : 'light');
    }

    function bindControls() {
        applyTheme(readTheme());

        document.addEventListener('click', (event) => {
            const themeCard = event.target.closest('[data-theme-set]');
            if (themeCard) {
                setTheme(themeCard.dataset.themeSet);
                return;
            }

            const toggle = event.target.closest('[data-theme-toggle]');
            if (!toggle) {
                return;
            }

            toggleTheme();
        });
    }

    applyTheme(readTheme());

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', bindControls);
    } else {
        bindControls();
    }

    window.addEventListener('storage', (event) => {
        if (event.key === STORAGE_KEY) {
            applyTheme(readTheme());
        }
    });

    window.addEventListener('pageshow', () => {
        applyTheme(readTheme());
    });

    window.EpdTheme = {
        get: readTheme,
        set: setTheme,
        toggle: toggleTheme
    };
}());
