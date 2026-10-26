// Signing in with a code (the OAuth device flow): the watch shows an
// eight-digit code, the person approves it at /link on any browser they're
// signed in on, and the tokens stay here on the phone.

var config = require('./config');

var KEY = 'antistatic.auth.v1';
var CLIENT_ID = config.base + '/oauth/clients/pebble.json';
var SCOPE = 'read offline_access';
var DEVICE_GRANT = 'urn:ietf:params:oauth:grant-type:device_code';

var STATE = { OUT: 0, CODE: 1, IN: 2, EXPIRED: 3, DENIED: 4, ERROR: 5, STARTING: 6 };

var auth = load();   // { access, refresh, expiresAt, name }
var flow = null;     // the sign-in in progress
var refreshing = null; // callbacks waiting on one refresh
var listener = function () {};

function load() {
  try { return JSON.parse(localStorage.getItem(KEY)) || null; } catch (e) { return null; }
}

function save() {
  try {
    if (auth) localStorage.setItem(KEY, JSON.stringify(auth));
    else localStorage.removeItem(KEY);
  } catch (e) { /* storage unavailable */ }
}

function form(obj) {
  return Object.keys(obj).map(function (k) {
    return encodeURIComponent(k) + '=' + encodeURIComponent(obj[k]);
  }).join('&');
}

// POST a form; cb(status, body). Status 0 is a network failure.
function post(path, fields, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', config.base + path, true);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  xhr.setRequestHeader('Accept', 'application/json');
  xhr.onload = function () {
    var body = {};
    try { body = JSON.parse(xhr.responseText); } catch (e) { /* not JSON */ }
    cb(xhr.status, body);
  };
  xhr.onerror = function () { cb(0, {}); };
  xhr.send(form(fields));
}

function store(tokens) {
  auth = {
    access: tokens.access_token,
    refresh: tokens.refresh_token || (auth && auth.refresh),
    expiresAt: Date.now() + (tokens.expires_in || 3600) * 1000,
    // Sign-in's tokens say whose account it is; refreshes don't.
    name: tokens.username ? '@' + tokens.username : auth && auth.name,
  };
  save();
}

function current() {
  if (flow) return { state: flow.state, code: flow.userCode || '', uri: flow.uri || '' };
  if (auth) return { state: STATE.IN, name: auth.name || '' };
  return { state: STATE.OUT };
}

function changed() { listener(current()); }

// One refresh at a time: refresh tokens rotate, and a second use of the old
// one would be refused.
function refresh(cb) {
  if (!auth || !auth.refresh) return cb(false);
  if (refreshing) return refreshing.push(cb);
  refreshing = [cb];
  post('/oauth/token', { grant_type: 'refresh_token', refresh_token: auth.refresh, client_id: CLIENT_ID }, function (status, body) {
    var ok = status === 200 && !!body.access_token;
    if (ok) store(body);
    else if (status === 400 || status === 401) { auth = null; save(); changed(); }
    var waiting = refreshing;
    refreshing = null;
    waiting.forEach(function (fn) { fn(ok); });
  });
}

// cb(token) with a usable access token, or null when signed out.
function token(cb) {
  if (!auth) return cb(null);
  if (auth.expiresAt - 60000 > Date.now()) return cb(auth.access);
  refresh(function (ok) { cb(ok ? auth.access : null); });
}

function cancelFlow() {
  if (flow && flow.timer) clearTimeout(flow.timer);
  flow = null;
}

function poll() {
  var f = flow;
  if (!f || f.state !== STATE.CODE) return;
  if (Date.now() > f.expiresAt) { f.state = STATE.EXPIRED; return changed(); }
  post('/oauth/token', { grant_type: DEVICE_GRANT, device_code: f.deviceCode, client_id: CLIENT_ID }, function (status, body) {
    if (flow !== f) return;
    if (status === 200 && body.access_token) {
      store(body);
      cancelFlow();
      if (f.onSignedIn) f.onSignedIn();
      return changed();
    }
    var error = body.error;
    if (error === 'slow_down') f.interval += 5;
    if (error === 'access_denied') { f.state = STATE.DENIED; return changed(); }
    if (error === 'expired_token') { f.state = STATE.EXPIRED; return changed(); }
    if (error && error !== 'authorization_pending' && error !== 'slow_down') { f.state = STATE.ERROR; return changed(); }
    f.timer = setTimeout(poll, f.interval * 1000);
  });
}

// Starts a sign-in and reports each step through the listener. onSignedIn
// runs once the code is approved.
function startSignIn(onSignedIn, onCode) {
  cancelFlow();
  var f = flow = { state: STATE.STARTING, onSignedIn: onSignedIn };
  changed();
  post('/oauth/device_authorization', { client_id: CLIENT_ID, scope: SCOPE }, function (status, body) {
    if (flow !== f) return;
    if (status !== 200 || !body.user_code) { f.state = STATE.ERROR; return changed(); }
    f.state = STATE.CODE;
    f.deviceCode = body.device_code;
    f.userCode = body.user_code;
    f.uri = (body.verification_uri || '').replace(/^https?:\/\//, '');
    f.complete = body.verification_uri_complete;
    f.interval = body.interval || 5;
    f.expiresAt = Date.now() + (body.expires_in || 600) * 1000;
    f.timer = setTimeout(poll, f.interval * 1000);
    changed();
    if (onCode) onCode(f.complete);
  });
}

function signOut() {
  cancelFlow();
  auth = null;
  save();
  changed();
}

module.exports = {
  STATE: STATE,
  token: token,
  refresh: refresh,
  current: current,
  startSignIn: startSignIn,
  signOut: signOut,
  signedIn: function () { return !!auth; },
  pendingUrl: function () { return flow && flow.complete; },
  onChange: function (fn) { listener = fn; },
};
