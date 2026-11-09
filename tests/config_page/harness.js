// Loads the REAL bundled phone JS (build/pebble-js-app.js, i.e. exactly what
// ships in the .pbw) into a Node vm with a stubbed Pebble/localStorage, so a
// test can fire showConfiguration (to get the generated Clay page), then feed
// the page's Save response back into webviewclosed and inspect the
// AppMessages sent to the watch. Requires a prior `pebble build`.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const BUNDLE = path.join(__dirname, '..', '..', 'build', 'pebble-js-app.js');
const CLOSE_URL = 'http://harness.invalid/close#';

function loadPhone(initialStorage) {
  if (!fs.existsSync(BUNDLE)) {
    throw new Error('build/pebble-js-app.js missing -- run `pebble build` first');
  }
  const store = Object.assign({}, initialStorage || {});
  const handlers = {};
  const sent = [];
  const phone = { handlers, sent, store, opened: [], failSends: false };
  const sandbox = {
    console: { log() {}, error() {}, warn() {}, info() {} },
    setTimeout, clearTimeout,
    localStorage: {
      getItem: (k) => (k in store ? store[k] : null),
      setItem: (k, v) => { store[k] = String(v); },
      removeItem: (k) => { delete store[k]; },
    },
    Pebble: {
      addEventListener: (name, fn) => { (handlers[name] = handlers[name] || []).push(fn); },
      openURL: (u) => phone.opened.push(u),
      sendAppMessage: (dict, ok, fail) => {
        sent.push(dict);
        if (phone.failSends) { if (fail) { fail(); } } else if (ok) { ok(); }
      },
      getActiveWatchInfo: () => ({ platform: 'emery', model: 'emery', language: 'en' }),
      getAccountToken: () => 'acct',
      getWatchToken: () => 'watch',
    },
  };
  vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(BUNDLE, 'utf8'), sandbox, { filename: 'pebble-js-app.js' });
  phone.fire = (name, arg) => (handlers[name] || []).forEach((fn) => fn(arg));
  return phone;
}

// The Clay page HTML the user would see on this phone.
function openConfigPage(phone) {
  phone.opened.length = 0;
  phone.fire('showConfiguration');
  const url = phone.opened[phone.opened.length - 1];
  const prefix = 'data:text/html;charset=utf-8,';
  if (!url || !url.startsWith(prefix)) { throw new Error('unexpected config URL: ' + String(url).slice(0, 60)); }
  // Make Save navigate to a URL the test can intercept instead of pebblejs://.
  return decodeURIComponent(url.slice(prefix.length)).split('pebblejs://close#').join(CLOSE_URL);
}

module.exports = { loadPhone, openConfigPage, CLOSE_URL };
