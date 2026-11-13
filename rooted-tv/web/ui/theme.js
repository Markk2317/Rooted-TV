// The theme toggle: dark by default, as Google TV is; or follow the system, or stay light.

// The toggle's mark, from the sprite in the page.
const THEME_MARKS = { auto: 'contrast', light: 'light_mode', dark: 'dark_mode' };

(function initTheme() {
  const html = document.documentElement;
  const mq = matchMedia('(prefers-color-scheme: dark)');
  const urlTheme = new URLSearchParams(location.search).get('theme');
  let stored = null;
  try { stored = localStorage.getItem('theme'); } catch (e) { /* private window */ }
  let setting = urlTheme || stored || 'dark';
  if (setting !== 'auto' && setting !== 'dark' && setting !== 'light') setting = 'dark';
  apply();

  function themeName(s) {
    return s === 'light' ? t('theme.light', 'Light') : s === 'dark' ? t('theme.dark', 'Dark') : t('theme.auto', 'Auto');
  }

  // On Auto the sheet's dark roles stand unless the system asks for light; browsers without
  // prefers-color-scheme report the query as "not all" and stay dark.
  function resolved() {
    if (setting !== 'auto') return setting;
    return mq.media !== 'not all' && !mq.matches ? 'light' : 'dark';
  }

  // The sheet follows the system with no attribute, and a choice sets one.
  function apply() {
    if (setting === 'auto') html.removeAttribute('data-theme');
    else html.setAttribute('data-theme', setting);
    const btn = q('theme-toggle');
    if (btn) {
      btn.innerHTML = '<svg class="ic" aria-hidden="true"><use href="#i-' + THEME_MARKS[setting] + '"/></svg>';
      const label = t('theme.toggle', 'Theme: {setting} (click for {next})',
                      { setting: themeName(setting), next: themeName(nextSetting()) });
      btn.title = label;
      btn.setAttribute('aria-label', label);
    }
    const mc = document.querySelector('meta[name="theme-color"]');
    if (mc) mc.content = resolved() === 'light' ? '#f8fafd' : '#0e0f11';
  }

  function nextSetting() {
    const order = ['auto', 'dark', 'light'];
    return order[(order.indexOf(setting) + 1) % order.length];
  }

  function onMediaChange() { if (setting === 'auto') apply(); }
  if (mq.addEventListener) mq.addEventListener('change', onMediaChange);
  else if (mq.addListener) mq.addListener(onMediaChange);   // Chrome <= 68 (webOS 5.x)

  document.addEventListener('click', function(e) {
    if (!e.target.closest('#theme-toggle')) return;
    e.preventDefault();
    html.classList.add('theming');
    setting = nextSetting();
    try { localStorage.setItem('theme', setting); } catch (err) { /* private window */ }
    apply();
    setTimeout(function () { html.classList.remove('theming'); }, 260);
  });
})();
