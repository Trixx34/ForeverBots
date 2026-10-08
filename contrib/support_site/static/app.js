// Forever web theme: light/dark and text size. Saved in a cookie because the in-game browser has local storage turned off.
(function () {
  var root = document.documentElement;

  function save(name, value) {
    document.cookie = name + '=' + value + '; Path=/; Max-Age=31536000; SameSite=Lax';
  }

  document.addEventListener('click', function (e) {
    var el = e.target.closest('[data-set-size],[data-toggle-theme]');
    if (!el)
      return;
    if (el.hasAttribute('data-toggle-theme')) {
      var theme = root.getAttribute('data-theme') === 'light' ? 'dark' : 'light';
      root.setAttribute('data-theme', theme);
      save('fs_theme', theme);
    } else {
      var size = el.getAttribute('data-set-size');
      root.setAttribute('data-size', size);
      save('fs_size', size);
      var buttons = document.querySelectorAll('[data-set-size]');
      for (var i = 0; i < buttons.length; ++i)
        buttons[i].classList.toggle('on', buttons[i] === el);
    }
  });
})();
