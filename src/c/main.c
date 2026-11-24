#include <pebble.h>

#define NOTE_MAX 512
#define TRANSCRIPT_MAX 256
#define HINT_MAX 48
#define SEND_TIMEOUT_MS 20000

// The note survives app exit. Persist values are capped at
// PERSIST_DATA_MAX_LENGTH bytes each, so it is stored in chunks.
#define PERSIST_KEY_NOTE_BASE 100
#define PERSIST_CHUNKS ((NOTE_MAX + PERSIST_DATA_MAX_LENGTH - 1) / PERSIST_DATA_MAX_LENGTH)

// STATUS values sent back by PebbleKit JS.
#define STATUS_FAILED 0
#define STATUS_SAVED 1
#define STATUS_NOT_CONFIGURED 2

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_note_layer;
static TextLayer *s_hint_layer;

static char s_note[NOTE_MAX];
static char s_hint[HINT_MAX];

static bool s_sending;
static AppTimer *s_send_timer;

#if defined(PBL_MICROPHONE)
static DictationSession *s_dictation;
#endif

// ---------------------------------------------------------------- UI helpers

static void set_hint(const char *msg) {
  snprintf(s_hint, sizeof(s_hint), "%s", msg);
  text_layer_set_text(s_hint_layer, s_hint);
}

// Re-measure the note text, resize the scroll content to match, and scroll to
// the end. UP and DOWN are taken by send and clear, so the view follows the
// newest text instead of being scrolled by hand.
static void refresh_note_layer(void) {
  Layer *tl = text_layer_get_layer(s_note_layer);
  GRect frame = layer_get_frame(tl);

  // Give the text layer plenty of room so content size measures correctly.
  frame.size.h = 2000;
  layer_set_frame(tl, frame);

  text_layer_set_text(s_note_layer, s_note[0] ? s_note : "Press SELECT to dictate.");

  GSize used = text_layer_get_content_size(s_note_layer);
  frame.size.h = used.h + 8;
  layer_set_frame(tl, frame);

  GSize content = GSize(frame.size.w + 2 * frame.origin.x, frame.size.h + 2 * frame.origin.y);
  scroll_layer_set_content_size(s_scroll_layer, content);

  int visible_h = layer_get_frame(scroll_layer_get_layer(s_scroll_layer)).size.h;
  int overflow = content.h - visible_h;
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, overflow > 0 ? -overflow : 0), false);
}

// ------------------------------------------------------------- note handling

// Returns false if the text did not fit in full.
static bool append_text(const char *text) {
  if (!text || !text[0]) {
    return true;
  }
  size_t len = strlen(s_note);
  size_t needed = strlen(text) + (len > 0 ? 1 : 0);
  bool fits = len + needed < NOTE_MAX;

  if (len > 0 && len < NOTE_MAX - 2) {
    strncat(s_note, " ", NOTE_MAX - len - 1);
    len = strlen(s_note);
  }
  strncat(s_note, text, NOTE_MAX - len - 1);
  return fits;
}

static void save_note(void) {
  size_t total = strlen(s_note) + 1;
  for (int i = 0; i < PERSIST_CHUNKS; i++) {
    size_t offset = (size_t)i * PERSIST_DATA_MAX_LENGTH;
    uint32_t key = PERSIST_KEY_NOTE_BASE + i;
    if (offset >= total || !s_note[0]) {
      persist_delete(key);
      continue;
    }
    size_t n = total - offset;
    if (n > PERSIST_DATA_MAX_LENGTH) {
      n = PERSIST_DATA_MAX_LENGTH;
    }
    persist_write_data(key, s_note + offset, n);
  }
}

static void load_note(void) {
  memset(s_note, 0, sizeof(s_note));
  for (int i = 0; i < PERSIST_CHUNKS; i++) {
    size_t offset = (size_t)i * PERSIST_DATA_MAX_LENGTH;
    uint32_t key = PERSIST_KEY_NOTE_BASE + i;
    if (!persist_exists(key)) {
      break;
    }
    size_t room = NOTE_MAX - offset;
    if (room > PERSIST_DATA_MAX_LENGTH) {
      room = PERSIST_DATA_MAX_LENGTH;
    }
    persist_read_data(key, s_note + offset, room);
  }
  s_note[NOTE_MAX - 1] = '\0';
}

static void end_send(void) {
  s_sending = false;
  if (s_send_timer) {
    app_timer_cancel(s_send_timer);
    s_send_timer = NULL;
  }
}

static void send_timeout_handler(void *context) {
  s_send_timer = NULL;
  s_sending = false;
  set_hint("No reply - UP retries");
  vibes_double_pulse();
}

static void send_note(void) {
  if (s_sending) {
    return;
  }
  if (!s_note[0]) {
    set_hint("Nothing to send");
    return;
  }

  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    set_hint("Busy - try again");
    return;
  }

  dict_write_cstring(iter, MESSAGE_KEY_NOTE_TEXT, s_note);
  dict_write_end(iter);

  if (app_message_outbox_send() != APP_MSG_OK) {
    set_hint("Send failed");
    return;
  }

  s_sending = true;
  s_send_timer = app_timer_register(SEND_TIMEOUT_MS, send_timeout_handler, NULL);
  set_hint("Sending...");
}

// --------------------------------------------------------------- AppMessage

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *status = dict_find(iter, MESSAGE_KEY_STATUS);
  if (!status) {
    return;
  }
  end_send();

  switch (status->value->int32) {
    case STATUS_SAVED:
      s_note[0] = '\0';
      refresh_note_layer();
      set_hint("Saved to Evernote");
      vibes_short_pulse();
      break;
    case STATUS_NOT_CONFIGURED:
      set_hint("Set up in phone app");
      vibes_double_pulse();
      break;
    default:
      set_hint("Failed - UP retries");
      vibes_double_pulse();
      break;
  }
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason,
                                  void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)reason);
  end_send();
  set_hint("Phone unreachable");
}

// ---------------------------------------------------------------- dictation

#if defined(PBL_MICROPHONE)
static void dictation_callback(DictationSession *session, DictationSessionStatus status,
                               char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    bool fits = append_text(transcription);
    refresh_note_layer();
    set_hint(fits ? "SELECT adds / UP sends" : "Note full - UP sends");
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Dictation status: %d", (int)status);
    set_hint("Dictation failed");
  }
}
#endif

// ------------------------------------------------------------------ buttons

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
#if defined(PBL_MICROPHONE)
  if (s_sending) {
    return;
  }
  if (strlen(s_note) >= NOTE_MAX - 2) {
    set_hint("Note full - UP sends");
    return;
  }
  if (s_dictation) {
    dictation_session_start(s_dictation);
  } else {
    set_hint("No dictation session");
  }
#else
  set_hint("No microphone");
#endif
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  send_note();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_note[0]) {
    set_hint("Hold DOWN to clear");
  }
}

static void down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_sending) {
    return;
  }
  s_note[0] = '\0';
  refresh_note_layer();
  set_hint("Cleared");
  vibes_short_pulse();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, down_long_click_handler, NULL);
}

// ------------------------------------------------------------------- window

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  const int hint_h = 26;
  GRect scroll_bounds = GRect(0, 0, bounds.size.w, bounds.size.h - hint_h);
  GRect hint_bounds = GRect(0, bounds.size.h - hint_h, bounds.size.w, hint_h);

  s_scroll_layer = scroll_layer_create(scroll_bounds);
  scroll_layer_set_shadow_hidden(s_scroll_layer, true);

  s_note_layer = text_layer_create(GRect(4, 4, scroll_bounds.size.w - 8, 2000));
  text_layer_set_font(s_note_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_note_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_note_layer, GColorClear);
  text_layer_set_text_alignment(s_note_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter,
                                                                GTextAlignmentLeft));
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_note_layer));
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  s_hint_layer = text_layer_create(hint_bounds);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_hint_layer, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  text_layer_set_text_color(s_hint_layer, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  refresh_note_layer();
  set_hint(s_note[0] ? "SELECT adds / UP sends" : "SELECT to dictate");
}

static void window_unload(Window *window) {
  text_layer_destroy(s_hint_layer);
  text_layer_destroy(s_note_layer);
  scroll_layer_destroy(s_scroll_layer);
}

// --------------------------------------------------------------------- main

static void init(void) {
  load_note();

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(256, NOTE_MAX + 64);

#if defined(PBL_MICROPHONE)
  s_dictation = dictation_session_create(TRANSCRIPT_MAX, dictation_callback, NULL);
  // Keep the confirmation screen: it lets you retry a bad transcription.
  dictation_session_enable_confirmation(s_dictation, true);
#endif

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  save_note();
  end_send();
#if defined(PBL_MICROPHONE)
  if (s_dictation) {
    dictation_session_destroy(s_dictation);
  }
#endif
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
