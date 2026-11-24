# Quick Note — Pebble → Evernote

Dictate a note on your Pebble Time 2, press UP, and it lands in your Evernote
default notebook a couple of seconds later.

```
Watch (C)  ──AppMessage──>  Phone (PebbleKit JS)  ──HTTPS──>  Worker  ──email──>  Evernote
```

The watch has no internet. PebbleKit JS runs inside the Pebble phone app and
does have it. The Worker exists so your Evernote address and mail API key never
live on the phone — anyone who has that address can write into your account.

The phone only holds the Worker's URL and a shared secret, which you enter on
the app's settings page. Nothing secret is compiled into the app or committed
here.

## Controls

| Button | Action |
| --- | --- |
| SELECT | Dictate. Transcription is appended to the current note. |
| UP | Send the note to Evernote. |
| Hold DOWN | Clear the note. |
| BACK | Exit. The unsent note is kept and restored next time. |

Dictation cuts off when it detects you've stopped or after ~6 seconds, whichever
comes first — that's why SELECT appends rather than sends. Tap it a few times to
build up a longer note, then UP. The view follows the end of the note as it
grows.

The bar at the bottom shows what happened: `Saved to Evernote`, `Failed - UP
retries` (the relay or mail service refused it), `No reply - UP retries` (the
phone didn't answer within 20 seconds), `Phone unreachable`, or `Set up in
phone app` (the settings page is empty). The note is only cleared once it has
been saved.

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
curl -i -X POST https://quick-note.YOUR-SUBDOMAIN.workers.dev/note \
  -H 'Content-Type: application/json' \
  -d '{"secret":"YOUR-SECRET","text":"hello from curl"}'
```

You should get `200 ok` and a note in Evernote. If it doesn't work, stop here —
nothing downstream will either. `401` means the secret is wrong, `502` means
Resend refused the mail (`wrangler tail` shows why: usually an unverified
`FROM_EMAIL` or a bad API key), and `404` means the URL is missing `/note`.

To run the relay's tests:

```bash
cd relay
npm install
npm test
```

### 3. Build and install the watchapp

```bash
export PATH="$HOME/.local/bin:$PATH"
npm install
pebble build
pebble install --phone <YOUR_PHONE_IP>
```

Or open `build/pebble-evernote.pbw` on your phone with the Pebble app.

`pebble install --emulator emery` lets you try the UI (dictation won't work in
the emulator — it needs a real mic and the phone app).

### 4. Point the app at your relay

In the Pebble phone app, open Quick Note's settings and fill in:

- **Relay URL** — `https://quick-note.YOUR-SUBDOMAIN.workers.dev/note`
- **Shared secret** — the same string you gave `wrangler secret put SHARED_SECRET`

Then dictate a note and press UP.

## Known rough edges

- **"Dictation is not available"** on Pebble Time 2 is a common complaint. Check
  the Speech Recognition setting in the Core Devices phone app (Cloud Only vs
  Local Only) and confirm voice replies to a notification work before you blame
  this app.
- Targets `basalt`, `chalk`, `diorite`, `emery`. Add `gabbro` (Time 2 Round) or
  `flint` (Pebble 2 Duo) to `targetPlatforms` if your SDK build supports them.
- Note buffer is 512 bytes. Raise `NOTE_MAX` in `src/c/main.c` if you want more;
  the AppMessage outbox and saved-note storage are sized from it.
- The app's JavaScript is allowed to reach any host (`"usesPermissions":
  ["Internet"]` in `package.json`) because the relay URL is only known once you
  enter it.
- Evernote's free tier caps you at 50 notes total, so this assumes a paid plan.

## Ideas if you want to keep going

- A second screen with canned notes for when dictation fails or you're in a
  meeting.
- Tag prefixes: say "work" as the first word and have the Worker convert it to
  `#work` in the subject line.
- Route to a specific notebook with another field on the settings page.
