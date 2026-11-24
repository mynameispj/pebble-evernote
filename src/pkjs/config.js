// Clay settings page shown in the Pebble phone app.
// Values stay in the phone's localStorage; they are never sent to the watch.
module.exports = [
  {
    type: 'heading',
    defaultValue: 'Quick Note',
  },
  {
    type: 'text',
    defaultValue:
      'Notes are posted to your relay Worker, which emails them to Evernote. ' +
      'Both values come from the relay setup in the README.',
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Relay',
      },
      {
        type: 'input',
        messageKey: 'RELAY_URL',
        label: 'Relay URL',
        defaultValue: '',
        description: 'For example https://quick-note.your-subdomain.workers.dev/note',
        attributes: {
          type: 'url',
          placeholder: 'https://…/note',
          autocapitalize: 'off',
          autocorrect: 'off',
          spellcheck: 'false',
        },
      },
      {
        type: 'input',
        messageKey: 'SHARED_SECRET',
        label: 'Shared secret',
        defaultValue: '',
        description: 'Must match the SHARED_SECRET set on the Worker.',
        attributes: {
          type: 'password',
          autocapitalize: 'off',
          autocorrect: 'off',
          spellcheck: 'false',
        },
      },
    ],
  },
  {
    type: 'submit',
    defaultValue: 'Save',
  },
];
