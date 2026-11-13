// The clock in the site's top bar, beside search, as Google TV's bar has one
// beside the name. stylesheets/extra.css shows it on wide screens only.
(function () {
  function two(value) {
    return (value < 10 ? '0' : '') + value;
  }
  function show(clock) {
    var now = new Date();
    clock.textContent = two(now.getHours()) + ':' + two(now.getMinutes());
  }
  function mount() {
    var bar = document.querySelector('.md-header__inner');
    if (!bar || bar.querySelector('.rt-clock')) return;
    var clock = document.createElement('div');
    clock.className = 'rt-clock';
    clock.setAttribute('aria-hidden', 'true');
    bar.insertBefore(clock, bar.querySelector('.md-search'));
    show(clock);
    setInterval(function () { show(clock); }, 15000);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
  else mount();
})();
