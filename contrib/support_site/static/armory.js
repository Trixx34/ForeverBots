// Armory item tooltips (data from the page: window.ARMORY_ITEMS = { itemId: { name, q, ilvl, icon, lines: [[kind, text, right?]] } }).
(function () {
  var items = window.ARMORY_ITEMS || {};
  var tip = document.createElement('div');
  tip.className = 'wow-tip';
  tip.hidden = true;
  document.body.appendChild(tip);

  function text(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
  }

  function render(item) {
    var html = '<div class="t-name q' + item.q + '">' + text(item.name) + '</div>';
    if (item.ilvl)
      html += '<div class="t-ilvl">Item Level ' + item.ilvl + '</div>';
    item.lines.forEach(function (l) {
      if (l[0] === 'split')
        html += '<div class="t-split"><span>' + text(l[1]) + '</span><span>' + text(l[2] || '') + '</span></div>';
      else
        html += '<div class="t-' + (l[0] || 'line') + '">' + text(l[1]) + '</div>';
    });
    return html;
  }

  function place(x, y) {
    var w = tip.offsetWidth, h = tip.offsetHeight;
    var left = x + 16, top = y + 16;
    if (left + w > window.innerWidth - 8) left = Math.max(8, x - w - 16);
    if (top + h > window.innerHeight - 8) top = Math.max(8, window.innerHeight - h - 8);
    tip.style.left = left + 'px';
    tip.style.top = top + 'px';
  }

  function show(el, x, y) {
    var item = items[el.getAttribute('data-item')];
    if (!item) return;
    tip.innerHTML = render(item);
    tip.hidden = false;
    place(x, y);
  }

  document.addEventListener('mouseover', function (e) {
    var el = e.target.closest('[data-item]');
    if (el) show(el, e.clientX, e.clientY); else tip.hidden = true;
  });
  document.addEventListener('mousemove', function (e) {
    if (!tip.hidden) place(e.clientX, e.clientY);
  });
  document.addEventListener('click', function (e) {       // touch screens: tap a slot to show, tap elsewhere to hide
    var el = e.target.closest('[data-item]');
    if (el) {
      var r = el.getBoundingClientRect();
      show(el, r.right, r.top);
    } else {
      tip.hidden = true;
    }
  });
})();
