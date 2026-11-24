// Cloudflare Worker: receives a note from the Pebble phone app and emails it
// to your Evernote "Email Notes to" address.
//
// Secrets (set with `wrangler secret put NAME`):
//   SHARED_SECRET   - must match the secret on the watchapp's settings page
//   RESEND_API_KEY  - from resend.com
//   EVERNOTE_EMAIL  - your personal Evernote address (keep this secret!)
//   FROM_EMAIL      - a verified sender on your Resend domain
//
// Optional var (wrangler.toml):
//   DEFAULT_TAGS    - e.g. "#inbox" appended to every subject line

const TITLE_MAX = 60;
const TEXT_MAX = 2000; // the watch sends at most ~512 bytes; this bounds abuse
const BODY_MAX_BYTES = 16 * 1024;

export function buildSubject(text, defaultTags) {
  // Evernote parses the subject line: "Title @Notebook #tag !YYYY/MM/DD",
  // and a trailing "+" means "append to the existing note with this title".
  // Omitting @Notebook drops the note in your default notebook.
  // Strip those directive characters so dictated text can't reroute a note.
  let title = text
    .replace(/[@#!+]/g, ' ')
    .trim()
    .split(/\s+/)
    .join(' ');
  if (title.length > TITLE_MAX) {
    title = title.slice(0, TITLE_MAX).trimEnd() + '...';
  }
  if (!title) {
    title = 'Quick Note';
  }
  return defaultTags ? `${title} ${defaultTags}` : title;
}

export function buildBody(text, now = new Date()) {
  return `${text}\n\n---\nCaptured from Pebble at ${now.toISOString()}`;
}

// Compare digests so the comparison time doesn't depend on how many leading
// characters of the secret a caller guessed.
async function secretsMatch(given, expected) {
  if (typeof given !== 'string' || typeof expected !== 'string' || !expected) {
    return false;
  }
  const enc = new TextEncoder();
  const [a, b] = await Promise.all([
    crypto.subtle.digest('SHA-256', enc.encode(given)),
    crypto.subtle.digest('SHA-256', enc.encode(expected)),
  ]);
  const x = new Uint8Array(a);
  const y = new Uint8Array(b);
  let diff = 0;
  for (let i = 0; i < x.length; i++) {
    diff |= x[i] ^ y[i];
  }
  return diff === 0;
}

export default {
  async fetch(request, env) {
    const { pathname } = new URL(request.url);
    if (pathname !== '/note') {
      return new Response('Not found', { status: 404 });
    }
    if (request.method !== 'POST') {
      return new Response('Method not allowed', { status: 405, headers: { Allow: 'POST' } });
    }

    const raw = await request.text();
    if (raw.length > BODY_MAX_BYTES) {
      return new Response('Payload too large', { status: 413 });
    }

    let payload;
    try {
      payload = JSON.parse(raw);
    } catch (err) {
      return new Response('Bad JSON', { status: 400 });
    }
    if (!payload || typeof payload !== 'object') {
      return new Response('Bad JSON', { status: 400 });
    }

    if (!(await secretsMatch(payload.secret, env.SHARED_SECRET))) {
      return new Response('Unauthorized', { status: 401 });
    }

    const text = (typeof payload.text === 'string' ? payload.text : '').trim().slice(0, TEXT_MAX);
    if (!text) {
      return new Response('Empty note', { status: 400 });
    }

    let res;
    try {
      res = await fetch('https://api.resend.com/emails', {
        method: 'POST',
        headers: {
          Authorization: `Bearer ${env.RESEND_API_KEY}`,
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          from: env.FROM_EMAIL,
          to: env.EVERNOTE_EMAIL,
          subject: buildSubject(text, env.DEFAULT_TAGS),
          text: buildBody(text),
        }),
      });
    } catch (err) {
      console.log('Mail send errored', String(err));
      return new Response('Upstream mail failure', { status: 502 });
    }

    if (!res.ok) {
      const detail = await res.text();
      console.log('Mail send failed', res.status, detail);
      return new Response('Upstream mail failure', { status: 502 });
    }

    return new Response('ok', { status: 200 });
  },
};
