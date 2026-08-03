(function () {
    const STORAGE_KEY = 'meetingThemeMode';
    const GLASS_FILTER_ID = 'meetingLiquidGlassFilters';
    const SVG_NS = 'http://www.w3.org/2000/svg';
    const REDUCED_GLASS_QUERY = '(max-width: 900px), (prefers-reduced-motion: reduce), (prefers-reduced-transparency: reduce), (update: slow), (hover: none) and (pointer: coarse)';

    function shouldReduceGlassEffects() {
        return typeof window.matchMedia === 'function' && window.matchMedia(REDUCED_GLASS_QUERY).matches;
    }

    function ensureLiquidGlassFilters() {
        if (!document.body || document.getElementById(GLASS_FILTER_ID) || shouldReduceGlassEffects()) {
            return;
        }

        const svg = document.createElementNS(SVG_NS, 'svg');
        svg.id = GLASS_FILTER_ID;
        svg.setAttribute('aria-hidden', 'true');
        svg.setAttribute('focusable', 'false');
        svg.style.cssText = 'position:absolute;width:0;height:0;overflow:hidden;pointer-events:none;';
        svg.innerHTML = `
            <defs>
                <filter id="meetingGlassRefractStrong" x="-18%" y="-18%" width="136%" height="136%" color-interpolation-filters="sRGB">
                    <feTurbulence type="fractalNoise" baseFrequency="0.018 0.024" numOctaves="2" seed="23" result="noise"/>
                    <feGaussianBlur in="SourceGraphic" stdDeviation="0.35" result="softSource"/>
                    <feDisplacementMap in="softSource" in2="noise" scale="13" xChannelSelector="R" yChannelSelector="G" result="refracted"/>
                    <feColorMatrix in="refracted" type="matrix" values="1.06 0 0 0 0  0 1.06 0 0 0  0 0 1.08 0 0  0 0 0 1 0"/>
                </filter>
                <filter id="meetingGlassRefractSoft" x="-12%" y="-12%" width="124%" height="124%" color-interpolation-filters="sRGB">
                    <feTurbulence type="fractalNoise" baseFrequency="0.014 0.020" numOctaves="1" seed="31" result="noise"/>
                    <feDisplacementMap in="SourceGraphic" in2="noise" scale="7" xChannelSelector="R" yChannelSelector="G" result="refracted"/>
                    <feColorMatrix in="refracted" type="matrix" values="1.035 0 0 0 0  0 1.035 0 0 0  0 0 1.05 0 0  0 0 0 1 0"/>
                </filter>
            </defs>
        `;
        document.body.prepend(svg);
    }

    function normalizeTheme(value) {
        return value === 'dark' ? 'dark' : 'light';
    }

    function readTheme() {
        try {
            return localStorage.getItem(STORAGE_KEY) === 'dark' ? 'dark' : 'light';
        } catch (e) {
            return 'light';
        }
    }

    function storeTheme(theme) {
        try {
            if (theme === 'dark') {
                localStorage.setItem(STORAGE_KEY, 'dark');
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
        document.documentElement.style.colorScheme = isLight ? 'light' : 'dark';

        if (isLight) {
            document.documentElement.dataset.theme = 'light';
        } else {
            delete document.documentElement.dataset.theme;
        }

        if (document.body) {
            document.body.classList.toggle('theme-light', isLight);
            document.body.style.colorScheme = isLight ? 'light' : 'dark';

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
        ensureLiquidGlassFilters();
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

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', ensureLiquidGlassFilters);
    } else {
        ensureLiquidGlassFilters();
    }

    if (typeof window.matchMedia === 'function') {
        const glassPreference = window.matchMedia(REDUCED_GLASS_QUERY);
        const handleGlassPreference = (event) => {
            if (!event.matches) ensureLiquidGlassFilters();
        };
        if (typeof glassPreference.addEventListener === 'function') {
            glassPreference.addEventListener('change', handleGlassPreference);
        } else if (typeof glassPreference.addListener === 'function') {
            glassPreference.addListener(handleGlassPreference);
        }
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
