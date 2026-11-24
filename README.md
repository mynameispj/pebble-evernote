# Quick Note — Pebble → Evernote

Dictate a note on your Pebble Time 2, press UP, and it lands in your Evernote
default notebook a couple of seconds later.

```
Watch (C)  ──AppMessage──>  Phone (PebbleKit JS)  ──HTTPS──>  Worker  ──email──>  Evernote
```

The watch has no internet. PebbleKit JS runs inside the Pebble phone app and
does have it. The Worker exists so your Evernote address and mail API key never
live on the phone — anyone who has that address can write into your account.

## Controls

| Button | Action |
| --- | --- |
| SELECT | Dictate. Transcription is appended to the current note. |
| UP | Send the note to Evernote. |
| DOWN | Clear the buffer. |
| BACK | Exit. |

Dictation cuts off when it detects you've stopped or after ~6 seconds, whichever
comes first — that's why SELECT appends rather than sends. Tap it a few times to
build up a longer note, then UP.

## Setup

### 1. Get your Evernote email address

Desktop app: **Tools → Settings → Personal Info → Go to your profile**, look for
the "Email Notes to" section. It's also under Account Info on the web, or
Profile icon → Account → Evernote email on mobile.

Subject line syntax, if you want to route notes later:
`Title @Notebook #tag !YYYY/MM/DD`. Omit `@Notebook` and it goes to your
default notebook. Ending the subject with `+` appends to an existing note of
that title.

### 2. Deploy the relay

```bash
cd relay
npm install -g wrangler        # if you don't have it
wrangler login
wrangler secret put SHARED_SECRET     # any long random string
wrangler secret put RESEND_API_KEY    # from resend.com
wrangler secret put EVERNOTE_EMAIL    # from step 1
wrangler secret put FROM_EMAIL        # verified sender on your Resend domain
wrangler deploy
```

Resend's free tier is plenty for this. Postmark or Mailgun work identically —
swap the `fetch` call in `src/index.js`.

Test it before touching the watch:

```bash
curl -X POST https://quick-note.YOUR-SUBDOMAIN.workers.dev/note \
  -H 'Content-Type: application/json' \
  -d '{"secret":"YOUR-SECRET","text":"hello from curl"}'
```

A note should appear in Evernote. If it doesn't, stop here — nothing downstream
will work either.

### 3. Build and install the watchapp

Edit `RELAY_URL` and `SHARED_SECRET` at the top of `src/pkjs/index.js` first.

```bash
export PATH="$HOME/.local/bin:$PATH"
pebble build
pebble install --phone <YOUR_PHONE_IP>
```

Or `pebble install --emulator emery` to try the UI (dictation won't work in the
emulator — it needs a real mic and the phone app).

## Known rough edges

- **"Dictation is not available"** on Pebble Time 2 is a common complaint. Check
  the Speech Recognition setting in the Core Devices phone app (Cloud Only vs
  Local Only) and confirm voice replies to a notification work before you blame
  this app.
- Targets `basalt`, `chalk`, `diorite`, `emery`. Add `gabbro` (Time 2 Round) or
  `flint` (Pebble 2 Duo) to `targetPlatforms` if your SDK build supports them.
- Note buffer is 512 bytes. Raise `NOTE_MAX` in `src/c/main.c` if you want more,
  and bump the AppMessage outbox size to match.
- Evernote's free tier caps you at 50 notes total, so this assumes a paid plan.

## Ideas if you want to keep going

- A second screen with canned notes for when dictation fails or you're in a
  meeting.
- Tag prefixes: say "work" as the first word and have the Worker convert it to
  `#work` in the subject line.
- Route to a specific notebook per app config using the Pebble settings page.
