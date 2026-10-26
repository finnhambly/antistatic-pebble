// The settings page in the phone's Pebble app: the account, and which markets
// the watch shows in what order. Built here as a data: page, since it needs
// the phone's market list and sign-in; it closes with its choices as JSON.

function page(state) {
  var data = JSON.stringify(state).replace(/</g, '\\u003c');
  return '<!doctype html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Antistatic</title><style>' + CSS + '</style></head><body>' +
    '<header>antistatic</header><main id="app"></main>' +
    '<script>var STATE=' + data + ';' + APP + '</script></body></html>';
}

var CSS = [
  '*{box-sizing:border-box;border-radius:0}',
  'body{margin:0;background:#000;color:#fff;font:16px/1.35 -apple-system,system-ui,sans-serif}',
  'header{padding:16px;font:600 22px/1 ui-monospace,Menlo,monospace;letter-spacing:.02em}',
    'main{padding:0 16px 96px}',
  'h2{font-size:13px;font-weight:600;color:#68a6cb;margin:24px 0 8px}',
  '.row{display:flex;align-items:center;gap:8px;min-height:48px;padding:4px 0;border-bottom:1px solid #1d2a36}',
  '.row .t{flex:1;min-width:0;overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;font-size:15px}',
  '.n{width:22px;color:#68a6cb;font:13px ui-monospace,Menlo,monospace}',
  '.tag{font-size:11px;color:#68a6cb;border:1px solid #68a6cb;padding:0 4px}',
  'button{flex:none;font:inherit;color:#fff;background:#000;border:1px solid #3b5b75;min-width:40px;height:36px;padding:0 10px}',
  'button:disabled{opacity:.3}',
  'a.btn{flex:none;display:inline-flex;align-items:center;color:#000;background:#68a6cb;height:36px;padding:0 10px;text-decoration:none}',
  'button.on{background:#68a6cb;border-color:#68a6cb;color:#000}',
  '.seg{display:flex}.seg button{flex:1}.seg button+button{border-left:0}',
  'input{width:100%;font:inherit;color:#fff;background:#000;border:1px solid #3b5b75;height:40px;padding:0 10px;margin-bottom:4px}',
  '.dim{color:#7f97ab}',
  '.save{position:fixed;left:0;right:0;bottom:0;padding:12px 16px;background:#000;border-top:1px solid #1d2a36}',
  '.save button{width:100%;height:48px;background:#68a6cb;border-color:#68a6cb;color:#000;font-weight:600}',
].join('');

// Runs in the page. STATE: { auth: {signedIn, name, link}, max, picks: [market] or
// null for the time-sensitive order, markets: [market] in that order }, where
// a market is { code, title, private }.
var APP = '(' + function () {
  var picks = STATE.picks ? STATE.picks.slice() : null;
  var filter = '';
  var app = document.getElementById('app');

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; });
  }
  function shown() { return picks || STATE.markets.slice(0, STATE.max); }
  function edit() { if (!picks) picks = shown().slice(); }
  function close(extra) {
    var out = { markets: picks ? picks : null };
    for (var k in extra) out[k] = extra[k];
    // The emulator passes where to go back to; the phone app takes pebblejs://.
    var back = location.href.match(/[?&]return_to=([^&]*)/);
    location.href = (back ? decodeURIComponent(back[1]) : 'pebblejs://close#') + encodeURIComponent(JSON.stringify(out));
  }
  function tag(m) { return m.private ? ' <span class="tag">Private</span>' : ''; }

  function render() {
    var list = shown();
    var codes = {};
    list.forEach(function (m) { codes[m.code] = true; });
    var h = '<h2>Account</h2><div class="row"><span class="t">' +
      (STATE.auth.signedIn ? esc(STATE.auth.name || 'Signed in') : '<span class="dim">Not signed in</span>') +
      '</span>' + (STATE.auth.signedIn ? '<button data-a="signout">Sign out</button>'
        // The approval page, with the watch's code already in it.
        : STATE.auth.link ? '<a class="btn" href="' + esc(STATE.auth.link) + '">Sign in</a>'
        : '<button data-a="signin">Sign in</button>') + '</div>';
    h += '<h2>Markets</h2><div class="seg">' +
      '<button data-a="time" class="' + (picks ? '' : 'on') + '">Time sensitive</button>' +
      '<button data-a="pick" class="' + (picks ? 'on' : '') + '">My pick</button></div>';
    h += '<h2>On the watch ' + list.length + '/' + STATE.max + '</h2>';
    if (!list.length) h += '<div class="row dim">None yet: add some below</div>';
    list.forEach(function (m, i) {
      h += '<div class="row"><span class="n">' + (i + 1) + '</span><span class="t">' + esc(m.title) + '</span>' + tag(m) +
        '<button data-a="up" data-i="' + i + '"' + (i ? '' : ' disabled') + '>&uarr;</button>' +
        '<button data-a="down" data-i="' + i + '"' + (i < list.length - 1 ? '' : ' disabled') + '>&darr;</button>' +
        '<button data-a="drop" data-i="' + i + '">&times;</button></div>';
    });
    var rest = STATE.markets.filter(function (m) {
      return !codes[m.code] && (!filter || m.title.toLowerCase().indexOf(filter) >= 0);
    });
    h += '<h2>Add</h2><input id="q" placeholder="Search" value="' + esc(filter) + '">';
    rest.forEach(function (m) {
      h += '<div class="row"><span class="t">' + esc(m.title) + '</span>' + tag(m) +
        '<button data-a="add" data-c="' + esc(m.code) + '"' + (list.length < STATE.max ? '' : ' disabled') + '>+</button></div>';
    });
    h += '<div class="save"><button data-a="save">Save</button></div>';
    app.innerHTML = h;
    var q = document.getElementById('q');
    q.oninput = function () {
      filter = q.value.toLowerCase();
      render();
      var again = document.getElementById('q');
      again.focus();
      again.setSelectionRange(again.value.length, again.value.length);
    };
  }

  app.onclick = function (e) {
    var b = e.target.closest('button');
    if (!b || b.disabled) return;
    var a = b.getAttribute('data-a'), i = Number(b.getAttribute('data-i'));
    if (a === 'save') return close({});
    if (a === 'signin' || a === 'signout') return close({ action: a });
    if (a === 'time') picks = null;
    else if (a === 'pick') edit();
    else {
      edit();
      if (a === 'up') picks.splice(i - 1, 0, picks.splice(i, 1)[0]);
      if (a === 'down') picks.splice(i + 1, 0, picks.splice(i, 1)[0]);
      if (a === 'drop') picks.splice(i, 1);
      if (a === 'add') {
        var c = b.getAttribute('data-c');
        STATE.markets.forEach(function (m) { if (m.code === c) picks.push(m); });
      }
    }
    render();
  };

  render();
}.toString() + ')();';

module.exports = {
  // The trailing comment keeps a return_to the emulator appends out of the
  // page, and tells Android the data is a page.
  url: function (state) { return 'data:text/html;charset=utf-8,' + encodeURIComponent(page(state) + '<!--.html'); },
  // The page's JSON, or null if it was closed without saving.
  parse: function (response) {
    if (!response) return null;
    try { return JSON.parse(decodeURIComponent(response)); } catch (e) {
      try { return JSON.parse(response); } catch (e2) { return null; }
    }
  },
};
