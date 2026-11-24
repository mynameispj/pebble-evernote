// PebbleKit JS - runs inside the Pebble phone app, has internet access.
//
// The relay URL and shared secret are entered on the app's settings page in
// the Pebble phone app (see config.js) and kept in localStorage on the phone.

var Clay = require('pebble-clay');
var clayConfig = require('./config');

// autoHandleEvents is off so the settings are stored on the phone only,
// instead of being forwarded to the watch.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var SETTINGS_KEY = 'clay-settings'; // where Clay persists the page's values
var REQUEST_TIMEOUT_MS = 15000;

var STATUS_FAILED = 0;
var STATUS_SAVED = 1;
var STATUS_NOT_CONFIGURED = 2;

function loadSettings() {
  var stored;
  try {
    stored = JSON.parse(localStorage.getItem(SETTINGS_KEY)) || {};
  } catch (e) {
    stored = {};
  }
  return {
    url: String(stored.RELAY_URL || '').trim(),
    secret: String(stored.SHARED_SECRET || '').trim(),
  };
}

function reply(status) {
  Pebble.sendAppMessage(
    { STATUS: status },
    function () {},
    function (e) {
      console.log('Failed to reply to watch: ' + JSON.stringify(e));
    }
  );
}

function sendNote(text) {
  var settings = loadSettings();
  if (!/^https:\/\//i.test(settings.url) || !settings.secret) {
    console.log('Relay URL or secret not configured');
    reply(STATUS_NOT_CONFIGURED);
    return;
  }

  var req = new XMLHttpRequest();
  req.open('POST', settings.url, true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.timeout = REQUEST_TIMEOUT_MS;

  req.onload = function () {
    console.log('Relay responded ' + req.status + ': ' + req.responseText);
    reply(req.status >= 200 && req.status < 300 ? STATUS_SAVED : STATUS_FAILED);
  };
  req.onerror = function () {
    console.log('Relay request errored');
    reply(STATUS_FAILED);
  };
  req.ontimeout = function () {
    console.log('Relay request timed out');
    reply(STATUS_FAILED);
  };

  req.send(JSON.stringify({ text: text, secret: settings.secret }));
}

Pebble.addEventListener('ready', function () {
  console.log('Quick Note PebbleKit JS ready');
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) {
    return;
  }
  // Parses the response and persists it to localStorage[SETTINGS_KEY].
  clay.getSettings(e.response, false);
  console.log('Settings saved');
});

Pebble.addEventListener('appmessage', function (e) {
  var text = e.payload.NOTE_TEXT;
  if (!text) {
    return;
  }
  console.log('Note from watch (' + text.length + ' chars)');
  sendNote(text);
});
