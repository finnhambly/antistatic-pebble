// Phone side: reads Antistatic's API (signed in, when the watch is), boils
// each market down to what the watch draws, and sends it over in small
// binary messages.

var config = require('./config');
var auth = require('./auth');
var settings = require('./settings');

var API = config.base + '/api/v1';
var MAX_MARKETS = 24; // aplite holds 4: see maxMarkets
var MAX_GROUPS = 6;
var CHUNK = 80; // bars per detail message (11 bytes each); 40 on aplite's smaller inbox
var CACHE_KEY = 'antistatic.models.v1';
var CHOICES_KEY = 'antistatic.choices.v1'; // what settings offers: {at, markets}
var PICKS_KEY = 'antistatic.picks.v1'; // [{code, title, private}], or none for the time-sensitive order

var MSG = {
  SUMMARY: 1, LIST_DONE: 2, DETAIL: 3, DETAIL_DONE: 4, STATUS: 5, AUTH: 6,
  HELLO: 10, REQUEST_DETAIL: 11, REFRESH: 12, SIGN_IN: 13, SIGN_OUT: 14,
};
var MF = { RESOLVED: 1, CUMULATIVE: 2, PRIVATE: 4, MINE: 8 };
var NONE = 0xffff; // no forecast of yours on this bar
var CADENCE = { day: 0, week: 1, month: 2, quarter: 3, year: 4 };
var DAY_MS = 86400000;
var EPOCH_2000 = Date.UTC(2000, 0, 1);
var Q_NONE = -2147483648;

var models = [];      // in display order
var byCode = {};
var fetching = false;
var refetch = false;  // a fetch was asked for while one ran
var pendingDetail = null; // asked for before the first fetch finished

// --- Messaging -------------------------------------------------------------

var queue = [];
var sending = false;

function send(msg) {
  queue.push({ msg: msg, tries: 0 });
  pump();
}

function pump() {
  if (sending || queue.length === 0) return;
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.msg, function () {
    queue.shift();
    sending = false;
    pump();
  }, function () {
    item.tries += 1;
    if (item.tries > 4) queue.shift();
    sending = false;
    setTimeout(pump, 250 * item.tries);
  });
}

function status(text) {
  send({ Type: MSG.STATUS, Status: text });
}

// --- HTTP ------------------------------------------------------------------

// GET from the API, as the signed-in user when there is one. A refused
// token is refreshed once and the request tried again.
function get(path, cb, retried) {
  auth.token(function (token) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', API + path, true);
    xhr.setRequestHeader('Accept', 'application/json');
    if (token) xhr.setRequestHeader('Authorization', 'Bearer ' + token);
    xhr.onload = function () {
      if (xhr.status === 401 && token && !retried) {
        return auth.refresh(function () { get(path, cb, true); });
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        try { cb(null, JSON.parse(xhr.responseText)); } catch (e) { cb(e); }
      } else {
        cb(new Error('HTTP ' + xhr.status));
      }
    };
    // A request that never answers would hold up every fetch after it.
    xhr.timeout = 30000;
    xhr.onerror = xhr.ontimeout = function () { cb(new Error('Network')); };
    xhr.send();
  });
}

// Run fn over items, a few at a time.
function eachLimit(items, limit, fn, done) {
  var next = 0, active = 0, finished = 0;
  if (items.length === 0) return done();
  function launch() {
    while (active < limit && next < items.length) {
      (function (i) {
        active++;
        fn(items[i], i, function () {
          active--;
          finished++;
          if (finished === items.length) done(); else launch();
        });
      })(next++);
    }
  }
  launch();
}

// --- Model -----------------------------------------------------------------

function dayOf(iso) {
  return Math.floor((Date.parse(iso) - EPOCH_2000) / DAY_MS);
}

function permille(p) {
  if (typeof p !== 'number' || isNaN(p)) return 0;
  return Math.max(0, Math.min(1000, Math.round(p * 1000)));
}

// What goes before the number, after it, or (a word unit) beside the dial.
// The watch's fonts have no ₩, € or ¥.
function splitUnit(unit) {
  var u = (unit || '').replace(/₩/g, 'W').replace(/€/g, 'EUR').replace(/¥/g, 'Y');
  if (!u) return ['', '', ''];
  var at = u.indexOf('X');
  if (at >= 0) {
    var post = u.slice(at + 1);
    if (/^\s/.test(post)) return ['', '', post.trim().slice(0, 9)];
    return [u.slice(0, at), post, ''];
  }
  var m = u.match(/^([A-Z]*[$£]|W|EUR|Y)(.*)$/);
  if (m) return [m[1], m[2], ''];
  if (/^(%|pp|°C|°F|‰|x|bp|bps)$/.test(u)) return ['', u, ''];
  return ['', '', u.slice(0, 9)];
}

function cadenceOf(market) {
  var c = CADENCE[market.x_unit];
  return c === undefined ? CADENCE.month : c;
}

// Percentile of a falling P(value >= threshold) curve, as the homepage does:
// linear between the thresholds either side.
function countPercentile(pct, pairs) {
  var pts = pairs.map(function (p) { return [p[0], 100 - p[1] * 100]; })
    .sort(function (a, b) { return a[1] - b[1]; });
  if (pts.length === 0) return 0;
  for (var i = 0; i < pts.length; i++) {
    if (Math.abs(pts[i][1] - pct) < 0.001) return pts[i][0];
    if (pts[i][1] > pct) {
      if (i === 0) return pts[0][0];
      var a = pts[i - 1], b = pts[i];
      var t = (pct - a[1]) / Math.max(b[1] - a[1], 0.0001);
      return a[0] + t * (b[0] - a[0]);
    }
  }
  return pts[pts.length - 1][0];
}

// First date a rising curve reaches q, linear in days; null if it never does.
function datePercentile(q, points) {
  for (var i = 0; i < points.length; i++) {
    if (points[i].crowd >= q * 1000) {
      if (i === 0) return points[0].x;
      var a = points[i - 1], b = points[i];
      var t = (q * 1000 - a.crowd) / Math.max(b.crowd - a.crowd, 1);
      return Math.round(a.x + t * (b.x - a.x));
    }
  }
  return null;
}

function niceStep(raw) {
  if (!(raw > 0)) return 100;
  var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
  var f = raw / mag;
  var nice = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10;
  return Math.max(1, Math.round(nice * mag));
}

var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function byLabel(day, cadence) {
  var d = new Date(EPOCH_2000 + day * DAY_MS);
  var y = d.getUTCFullYear(), m = d.getUTCMonth();
  if (cadence === CADENCE.year || (m === 11 && d.getUTCDate() === 31)) return 'By end of ' + y;
  if (cadence === CADENCE.quarter) return 'By Q' + (Math.floor(m / 3) + 1) + ' ' + y;
  if (cadence === CADENCE.month) return 'By ' + MONTHS[m] + ' ' + y;
  return 'By ' + d.getUTCDate() + ' ' + MONTHS[m] + ' ' + y;
}

function buildModel(market, forecast, subs) {
  var kind = forecast.multidate ? 2 : (market.type === 'count' ? 0 : 1);
  var count = kind === 0;
  var dates = {};
  (subs || []).forEach(function (s) { if (s.threshold_date) dates[s.id] = dayOf(s.threshold_date); });

  // Curves in the order the API lists them.
  var order = (forecast.curves || []).map(function (c) { return c.curve; });
  var groups = {};
  var hasMine = false;
  (forecast.submarkets || []).forEach(function (s) {
    var key = s.curve || s.projection_group || 'main';
    if (!groups[key]) {
      groups[key] = { label: s.group_label || null, points: [] };
      if (order.indexOf(key) < 0) order.push(key);
    }
    var x = count ? Math.round(Number(s.threshold) * 100) : dates[s.id];
    if (x === undefined || x === null || isNaN(x)) return;
    var flags = 0;
    if (s.status === 'resolved') flags |= 1;
    if (s.resolved_yes) flags |= 2;
    if (s.status === 'ended' || s.status === 'closed') flags |= 4;
    var house = s.chart_starting_probability !== undefined ? s.chart_starting_probability : s.starting_probability;
    var mine = typeof s.my_probability === 'number' ? permille(s.my_probability) : NONE;
    if (mine !== NONE) hasMine = true;
    groups[key].points.push({ x: x, crowd: permille(s.community_probability), house: permille(house), mine: mine, flags: flags });
  });

  var eventLabels = {};
  if (forecast.multidate && forecast.multidate.event_details) {
    forecast.multidate.event_details.forEach(function (e) { eventLabels[e.key] = e.label; });
  }

  var list = order.filter(function (k) { return groups[k] && groups[k].points.length; })
    .slice(0, MAX_GROUPS)
    .map(function (k) {
      var g = groups[k];
      g.points.sort(function (a, b) { return a.x - b.x; });
      var label = eventLabels[k] || g.label || (k === 'main' ? '' : k);
      return { label: String(label).slice(0, 27), points: g.points };
    });

  var model = {
    code: market.code,
    title: market.title,
    kind: kind,
    flags: (market.status === 'resolved' ? MF.RESOLVED : 0) | (market.cumulative ? MF.CUMULATIVE : 0) |
      (market.visibility && market.visibility !== 'public' ? MF.PRIVATE : 0) | (hasMine ? MF.MINE : 0),
    cadence: count ? 0 : cadenceOf(market),
    unit: count ? splitUnit(market.x_unit) : ['', '', ''],
    groups: list,
    lo: 0, hi: 0, sub: '',
  };

  if (count) {
    var lo = Infinity, hi = -Infinity;
    list.forEach(function (g) {
      var pairs = g.points.map(function (p) { return [p.x, p.crowd / 1000]; });
      g.q = [5, 25, 50, 75, 95].map(function (pct) { return Math.round(countPercentile(pct, pairs)); });
      g.prob = 0;
      lo = Math.min(lo, g.q[0]);
      hi = Math.max(hi, g.q[4]);
    });
    if (!isFinite(lo)) { lo = 0; hi = 100; }
    // Round the axis out to a tidy step, so the dial's ends read cleanly.
    var step = niceStep((hi - lo) / 4);
    model.lo = Math.floor(lo / step) * step;
    model.hi = Math.ceil(hi / step) * step;
    if (model.hi === model.lo) model.hi = model.lo + step;
  } else {
    // Home date: the end of next year, or the last bar before it.
    var now = new Date();
    var home = Math.floor((Date.UTC(now.getUTCFullYear() + 1, 11, 31) - EPOCH_2000) / DAY_MS);
    var ladder = list.reduce(function (best, g) { return !best || g.points.length > best.points.length ? g : best; }, null);
    var pick = ladder ? ladder.points[0].x : home;
    if (ladder) ladder.points.forEach(function (p) { if (p.x <= home + 1) pick = p.x; });
    model.lo = pick;
    model.hi = ladder ? ladder.points[ladder.points.length - 1].x : pick;
    model.sub = byLabel(pick, model.cadence);
    list.forEach(function (g) {
      var prob = 0, flags = 0;
      g.points.forEach(function (p) { if (p.x <= pick) { prob = p.crowd; flags = p.flags; } });
      g.prob = prob;
      g.flags = flags;
      g.q = [0.05, 0.25, 0.5, 0.75, 0.95].map(function (q) {
        var d = datePercentile(q, g.points);
        return d === null ? Q_NONE : d;
      });
    });
  }
  return model;
}

// --- Encoding --------------------------------------------------------------

function i32(out, v) {
  v = Math.max(-2147483648, Math.min(2147483647, Math.round(v)));
  out.push(v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255);
}

function u16(out, v) { out.push(v & 255, (v >> 8) & 255); }

function sendSummaries(statusText) {
  var total = Math.min(models.length, maxMarkets());
  for (var i = 0; i < total; i++) {
    var m = models[i];
    var data = [];
    m.groups.forEach(function (g) {
      for (var k = 0; k < 5; k++) i32(data, g.q[k]);
      u16(data, g.prob || 0);
      data.push(g.flags || 0);
    });
    send({
      Type: MSG.SUMMARY, Index: i, Total: total,
      Code: m.code, Title: m.title.slice(0, 71), Sub: m.sub,
      Unit: m.unit.join('\t'),
      Labels: m.groups.map(function (g) { return g.label; }).join('\n'),
      Kind: m.kind, Flags: m.flags, Cadence: m.cadence, Lo: m.lo, Hi: m.hi,
      Data: data,
    });
  }
  send({ Type: MSG.LIST_DONE, Total: total, Status: statusText });
}

function sendDetail(m) {
  m.groups.forEach(function (g, gi) {
    var chunk = maxMarkets() < MAX_MARKETS ? 40 : CHUNK;
    for (var off = 0; off < g.points.length; off += chunk) {
      var data = [];
      g.points.slice(off, off + chunk).forEach(function (p) {
        i32(data, p.x); u16(data, p.crowd); u16(data, p.house); u16(data, p.mine === undefined ? NONE : p.mine);
        data.push(p.flags);
      });
      send({ Type: MSG.DETAIL, Code: m.code, Group: gi, Offset: off, GroupTotal: g.points.length, Data: data });
    }
  });
  send({ Type: MSG.DETAIL_DONE, Code: m.code });
}

// --- Fetching --------------------------------------------------------------

function setModels(list) {
  models = list;
  byCode = {};
  list.forEach(function (m) { byCode[m.code] = m; });
}

function loadCache() {
  try {
    var raw = localStorage.getItem(CACHE_KEY);
    if (raw) setModels(JSON.parse(raw));
  } catch (e) { setModels([]); }
}

function saveCache() {
  try { localStorage.setItem(CACHE_KEY, JSON.stringify(models)); } catch (e) { /* too big: skip */ }
}

function maxMarkets() {
  var info = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
  return info && info.platform === 'aplite' ? 4 : MAX_MARKETS;
}

function loadPicks() {
  try { return JSON.parse(localStorage.getItem(PICKS_KEY) || 'null'); } catch (e) { return null; }
}

function savePicks(picks) {
  if (picks) localStorage.setItem(PICKS_KEY, JSON.stringify(picks));
  else localStorage.removeItem(PICKS_KEY);
}

// The markets to show, in order: the ones picked in settings, or the
// homepage's time-sensitive order.
function listMarkets(cb) {
  var picks = loadPicks();
  if (!picks) {
    return get('/markets?sort=time&limit=' + maxMarkets(), function (err, res) { cb(err, err ? [] : res.data || []); });
  }
  var out = [], failed = 0, gone = 0;
  eachLimit(picks.slice(0, maxMarkets()), 3, function (pick, i, next) {
    get('/markets/' + encodeURIComponent(pick.code), function (err, res) {
      if (!err) out[i] = res.data;
      else if (err.message === 'HTTP 404' || err.message === 'HTTP 403') gone++;
      else failed++;
      next();
    });
  }, function () {
    var markets = out.filter(Boolean);
    if (markets.length) return cb(null, markets);
    if (failed) return cb(new Error('Network'), []);
    // None of the picks is there any more (deleted, or not yours to see):
    // the default order rather than nothing.
    savePicks(null);
    listMarkets(cb);
  });
}

function fetchAll() {
  if (fetching) { refetch = true; return; }
  fetching = true;
  listMarkets(function (err, markets) {
    if (err) {
      fetching = false;
      if (models.length) sendSummaries('Offline');
      else status('No signal');
      return;
    }
    var built = [];
    eachLimit(markets, 3, function (market, i, next) {
      var code = encodeURIComponent(market.code);
      get('/markets/' + code + '/forecast?limit=0', function (e1, f) {
        if (e1) return next();
        var finish = function (subs) {
          try { built[i] = buildModel(market, f.data || {}, subs); } catch (e) { console.log('model ' + market.code + ': ' + e); }
          next();
        };
        if (market.type === 'count') return finish([]);
        // The forecast has no dates on its bars; the submarket list does.
        get('/markets/' + code + '/submarkets', function (e2, s) { finish(e2 ? [] : (s.data || [])); });
      });
    }, function () {
      fetching = false;
      if (refetch) { refetch = false; return fetchAll(); }
      setModels(built.filter(function (m) { return m && m.groups.length; }));
      saveCache();
      sendSummaries('Live');
      if (pendingDetail && byCode[pendingDetail]) sendDetail(byCode[pendingDetail]);
      pendingDetail = null;
      fetchChoices(false);
    });
  });
}

// --- Account ---------------------------------------------------------------

function sendAuth(state) {
  send({ Type: MSG.AUTH, Status: String(state.state), Code: state.code || '', Title: state.uri || state.name || '' });
}

function signedIn() {
  // What was cached was fetched as someone else (or no one).
  setModels([]);
  saveCache();
  localStorage.removeItem(CHOICES_KEY);
  fetchAll();
}

auth.onChange(sendAuth);

Pebble.addEventListener('ready', function () {
  loadCache();
  sendAuth(auth.current());
  // The watch's hello usually goes out before this side is up, so start here.
  if (models.length) sendSummaries('Cached');
  fetchAll();
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Type === MSG.HELLO) {
    sendAuth(auth.current());
    if (models.length) sendSummaries('Cached');
    fetchAll();
  } else if (p.Type === MSG.REFRESH) {
    fetchAll();
  } else if (p.Type === MSG.REQUEST_DETAIL) {
    // Bars still queued for a market the watch has moved off are dropped.
    queue = queue.filter(function (item, i) {
      var t = item.msg.Type;
      return (sending && i === 0) || (t !== MSG.DETAIL && t !== MSG.DETAIL_DONE) || item.msg.Code === p.Code;
    });
    var m = byCode[p.Code];
    if (m) sendDetail(m);
    else if (fetching) pendingDetail = p.Code;
    else status('Not found');
  } else if (p.Type === MSG.SIGN_IN) {
    auth.startSignIn(signedIn);
  } else if (p.Type === MSG.SIGN_OUT) {
    signOut();
  }
});

function signOut() {
  auth.signOut();
  setModels([]);
  saveCache();
  localStorage.removeItem(CHOICES_KEY);
  fetchAll();
}

function brief(m) {
  return { code: m.code, title: m.title, private: !!(m.visibility && m.visibility !== 'public') };
}

function loadChoices() {
  try { return JSON.parse(localStorage.getItem(CHOICES_KEY) || 'null'); } catch (e) { return null; }
}

// The markets settings offers (most time-sensitive first), kept so the page
// opens at once. Fetched again after an hour, or when you sign in or out.
function fetchChoices(force, cb) {
  var kept = loadChoices();
  if (!force && kept && Date.now() - kept.at < 3600000) return cb && cb(kept.markets);
  // Up to 300, a page of 100 at a time.
  var markets = [];
  (function page(offset) {
    get('/markets?sort=time&limit=100&offset=' + offset, function (err, res) {
      if (err && !markets.length) return cb && cb(kept ? kept.markets : null);
      if (!err) markets = markets.concat((res.data || []).map(brief));
      if (!err && res.paging && res.paging.has_more && offset < 200) return page(offset + 100);
      try { localStorage.setItem(CHOICES_KEY, JSON.stringify({ at: Date.now(), markets: markets })); } catch (e) { /* full */ }
      if (cb) cb(markets);
    });
  })(0);
}

function openSettings(markets) {
  var cur = auth.current();
  Pebble.openURL(settings.url({
    auth: { signedIn: cur.state === auth.STATE.IN, name: cur.name || '', link: auth.pendingUrl() || '' },
    max: maxMarkets(),
    picks: loadPicks(),
    markets: markets || models.map(function (m) { return { code: m.code, title: m.title, private: !!(m.flags & MF.PRIVATE) }; }),
  }));
}

// Settings in the phone's Pebble app: the account, and which markets show in
// what order.
Pebble.addEventListener('showConfiguration', function () {
  var open = function () {
    var kept = loadChoices();
    if (kept) return openSettings(kept.markets);
    fetchChoices(true, openSettings);
  };
  // Signed out: get a code first, so Sign in goes straight to the approval
  // page inside settings (the phone app won't open a second page after this
  // one closes). Waits at most a few seconds for it.
  if (auth.signedIn() || auth.pendingUrl()) return open();
  var opened = false;
  var once = function () { if (!opened) { opened = true; open(); } };
  auth.startSignIn(signedIn, once);
  setTimeout(once, 3000);
});

Pebble.addEventListener('webviewclosed', function (e) {
  var out = settings.parse(e && e.response);
  if (!out) return;
  var before = JSON.stringify(loadPicks());
  savePicks(out.markets && out.markets.map(function (m) { return { code: m.code, title: m.title, private: !!m.private }; }));
  if (out.action === 'signout') return signOut();
  if (out.action === 'signin') {
    // Straight on to the approval page, the code already filled in.
    auth.startSignIn(signedIn, function (complete) { setTimeout(function () { Pebble.openURL(complete); }, 300); });
  }
  if (JSON.stringify(loadPicks()) !== before) fetchAll();
});

if (typeof module !== 'undefined') {
  module.exports = { buildModel: buildModel, splitUnit: splitUnit, countPercentile: countPercentile, byLabel: byLabel };
}
