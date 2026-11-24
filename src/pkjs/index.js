// PebbleKit JS - runs inside the Pebble phone app, has internet access.
//
// EDIT THESE TWO LINES before building.
// The relay URL is your deployed Cloudflare Worker. The secret is a random
// string you also set as a Worker secret - it stops randos POSTing notes
// into your Evernote if they ever find the URL.
var RELAY_URL = 'https://quick-note.YOUR-SUBDOMAIN.workers.dev/note';
var SHARED_SECRET = 'CHANGE-ME-to-a-long-random-string';

var REQUEST_TIMEOUT_MS = 15000;

function reply(ok) {
  Pebble.sendAppMessage(
    { STATUS: ok ? 1 : 0 },
    function () {},
    function (e) {
      console.log('Failed to reply to watch: ' + JSON.stringify(e));
    }
  );
}

function sendNote(text) {
  var req = new XMLHttpRequest();
  req.open('POST', RELAY_URL, true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.timeout = REQUEST_TIMEOUT_MS;

  req.onload = function () {
    console.log('Relay responded ' + req.status + ': ' + req.responseText);
    reply(req.status >= 200 && req.status < 300);
  };
  req.onerror = function () {
    console.log('Relay request errored');
    reply(false);
  };
  req.ontimeout = function () {
    console.log('Relay request timed out');
    reply(false);
  };

  req.send(JSON.stringify({ text: text, secret: SHARED_SECRET }));
}

Pebble.addEventListener('ready', function () {
  console.log('Quick Note PebbleKit JS ready');
});

Pebble.addEventListener('appmessage', function (e) {
  var text = e.payload.NOTE_TEXT;
  if (!text) {
    return;
  }
  console.log('Note from watch: ' + text);
  sendNote(text);
});
