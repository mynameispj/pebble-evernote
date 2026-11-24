import { afterEach, describe, expect, it, vi } from 'vitest';
import worker, { buildBody, buildSubject } from '../src/index.js';

const env = {
  SHARED_SECRET: 'test-secret',
  RESEND_API_KEY: 're_test',
  EVERNOTE_EMAIL: 'user.abc123@m.evernote.com',
  FROM_EMAIL: 'notes@example.com',
  DEFAULT_TAGS: '#inbox',
};

function post(body, { path = '/note', method = 'POST' } = {}) {
  return new Request(`https://relay.example${path}`, {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: method === 'POST' ? (typeof body === 'string' ? body : JSON.stringify(body)) : undefined,
  });
}

function mockResend(status = 200) {
  const fn = vi.fn(async () => new Response(status === 200 ? '{"id":"1"}' : 'nope', { status }));
  vi.stubGlobal('fetch', fn);
  return fn;
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('buildSubject', () => {
  it('collapses whitespace and appends default tags', () => {
    expect(buildSubject('  buy   milk\n today ', '#inbox')).toBe('buy milk today #inbox');
  });

  it('omits tags when none are configured', () => {
    expect(buildSubject('buy milk', '')).toBe('buy milk');
    expect(buildSubject('buy milk', undefined)).toBe('buy milk');
  });

  it('strips Evernote routing directives from dictated text', () => {
    expect(buildSubject('email @Work about #taxes !2026/01/01', '')).toBe(
      'email Work about taxes 2026/01/01'
    );
  });

  it('strips a trailing + so a note is never appended to an existing one', () => {
    expect(buildSubject('Groceries +', '')).toBe('Groceries');
  });

  it('truncates long titles', () => {
    const subject = buildSubject('word '.repeat(40), '');
    expect(subject.endsWith('...')).toBe(true);
    expect(subject.length).toBeLessThanOrEqual(63);
  });

  it('falls back to a default title', () => {
    expect(buildSubject('@#!+', '#inbox')).toBe('Quick Note #inbox');
  });
});

describe('buildBody', () => {
  it('keeps the full text and adds a timestamp', () => {
    const body = buildBody('hello', new Date('2026-01-02T03:04:05Z'));
    expect(body).toBe('hello\n\n---\nCaptured from Pebble at 2026-01-02T03:04:05.000Z');
  });
});

describe('fetch handler', () => {
  it('404s on other paths', async () => {
    const fetchMock = mockResend();
    const res = await worker.fetch(post({ secret: 'test-secret', text: 'hi' }, { path: '/' }), env);
    expect(res.status).toBe(404);
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it('405s on non-POST', async () => {
    const res = await worker.fetch(post(null, { method: 'GET' }), env);
    expect(res.status).toBe(405);
  });

  it('400s on bad JSON', async () => {
    expect((await worker.fetch(post('{not json'), env)).status).toBe(400);
    expect((await worker.fetch(post('null'), env)).status).toBe(400);
  });

  it('401s on a wrong or missing secret without sending mail', async () => {
    const fetchMock = mockResend();
    expect((await worker.fetch(post({ secret: 'wrong', text: 'hi' }), env)).status).toBe(401);
    expect((await worker.fetch(post({ text: 'hi' }), env)).status).toBe(401);
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it('401s when the Worker has no secret configured', async () => {
    const res = await worker.fetch(post({ secret: '', text: 'hi' }), { ...env, SHARED_SECRET: '' });
    expect(res.status).toBe(401);
  });

  it('400s on an empty note', async () => {
    const res = await worker.fetch(post({ secret: 'test-secret', text: '   ' }), env);
    expect(res.status).toBe(400);
  });

  it('413s on an oversized payload', async () => {
    const res = await worker.fetch(post({ secret: 'test-secret', text: 'x'.repeat(20000) }), env);
    expect(res.status).toBe(413);
  });

  it('emails the note to Evernote through Resend', async () => {
    const fetchMock = mockResend();
    const res = await worker.fetch(post({ secret: 'test-secret', text: 'call the dentist' }), env);
    expect(res.status).toBe(200);

    expect(fetchMock).toHaveBeenCalledTimes(1);
    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe('https://api.resend.com/emails');
    expect(init.headers.Authorization).toBe('Bearer re_test');
    const mail = JSON.parse(init.body);
    expect(mail.to).toBe(env.EVERNOTE_EMAIL);
    expect(mail.from).toBe(env.FROM_EMAIL);
    expect(mail.subject).toBe('call the dentist #inbox');
    expect(mail.text.startsWith('call the dentist\n\n---\n')).toBe(true);
  });

  it('502s when Resend rejects the mail', async () => {
    mockResend(422);
    const res = await worker.fetch(post({ secret: 'test-secret', text: 'hi' }), env);
    expect(res.status).toBe(502);
  });

  it('502s when Resend is unreachable', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => { throw new Error('network down'); }));
    const res = await worker.fetch(post({ secret: 'test-secret', text: 'hi' }), env);
    expect(res.status).toBe(502);
  });
});
