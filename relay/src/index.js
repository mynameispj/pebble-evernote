// Cloudflare Worker: receives a note from the Pebble phone app and emails it
// to your Evernote "Email Notes to" address.
//
// Secrets (set with `wrangler secret put NAME`):
//   SHARED_SECRET   - must match SHARED_SECRET in src/pkjs/index.js
//   RESEND_API_KEY  - from resend.com
//   EVERNOTE_EMAIL  - your personal Evernote address (keep this secret!)
//   FROM_EMAIL      - a verified sender on your Resend domain
//
// Optional var (wrangler.toml):
//   DEFAULT_TAGS    - e.g. "#inbox" appended to every subject line

const TITLE_MAX = 60;

function buildSubject(text, defaultTags) {
  // Evernote parses the subject line: "Title @Notebook #tag !YYYY/MM/DD"
  // Omitting @Notebook drops the note in your default notebook.
  let title = text.trim().split(/\s+/).join(' ');
  if (title.length > TITLE_MAX) {
    title = title.slice(0, TITLE_MAX).trimEnd() + '...';
  }
  // Strip characters Evernote would read as routing directives.
  title = title.replace(/[@#!]/g, '');
  if (!title) {
    title = 'Quick Note';
  }
  return defaultTags ? `${title} ${defaultTags}` : title;
}

function buildBody(text) {
  const stamp = new Date().toISOString();
  return `${text}\n\n---\nCaptured from Pebble at ${stamp}`;
}

export default {
  async fetch(request, env) {
    if (request.method !== 'POST') {
      return new Response('Method not allowed', { status: 405 });
    }

    let payload;
    try {
      payload = await request.json();
    } catch (err) {
      return new Response('Bad JSON', { status: 400 });
    }

    if (!payload.secret || payload.secret !== env.SHARED_SECRET) {
      return new Response('Unauthorized', { status: 401 });
    }

    const text = (payload.text || '').trim();
    if (!text) {
      return new Response('Empty note', { status: 400 });
    }

    const res = await fetch('https://api.resend.com/emails', {
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

    if (!res.ok) {
      const detail = await res.text();
      console.log('Mail send failed', res.status, detail);
      return new Response('Upstream mail failure', { status: 502 });
    }

    return new Response('ok', { status: 200 });
  },
};
