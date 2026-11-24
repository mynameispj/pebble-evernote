#include <pebble.h>

#define NOTE_MAX 512
#define TRANSCRIPT_MAX 256
#define HINT_MAX 48

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_note_layer;
static TextLayer *s_hint_layer;

static char s_note[NOTE_MAX];
static char s_hint[HINT_MAX];

#if defined(PBL_MICROPHONE)
static DictationSession *s_dictation;
#endif

// ---------------------------------------------------------------- UI helpers

static void set_hint(const char *msg) {
  snprintf(s_hint, sizeof(s_hint), "%s", msg);
  text_layer_set_text(s_hint_layer, s_hint);
}

// Re-measure the note text and resize the scroll content to match.
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
  scroll_layer_set_content_size(s_scroll_layer, frame.size);
}

// ------------------------------------------------------------- note handling

static void append_text(const char *text) {
  if (!text || !text[0]) {
    return;
  }
  size_t len = strlen(s_note);
  if (len > 0 && len < NOTE_MAX - 2) {
    strncat(s_note, " ", NOTE_MAX - len - 1);
    len = strlen(s_note);
  }
  strncat(s_note, text, NOTE_MAX - len - 1);
}

static void send_note(void) {
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
  set_hint("Sending...");
}

// --------------------------------------------------------------- AppMessage

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *status = dict_find(iter, MESSAGE_KEY_STATUS);
  if (!status) {
    return;
  }

  if (status->value->int32 == 1) {
    s_note[0] = '\0';
    refresh_note_layer();
    set_hint("Saved to Evernote");
    vibes_short_pulse();
  } else {
    set_hint("Failed - UP retries");
    vibes_double_pulse();
  }
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason,
                                  void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)reason);
  set_hint("Phone unreachable");
}

// ---------------------------------------------------------------- dictation

#if defined(PBL_MICROPHONE)
static void dictation_callback(DictationSession *session, DictationSessionStatus status,
                               char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    append_text(transcription);
    refresh_note_layer();
    set_hint("SELECT adds / UP sends");
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Dictation status: %d", (int)status);
    set_hint("Dictation failed");
  }
}
#endif

// ------------------------------------------------------------------ buttons

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
#if defined(PBL_MICROPHONE)
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
  s_note[0] = '\0';
  refresh_note_layer();
  set_hint("Cleared");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
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
#if defined(PBL_COLOR)
  text_layer_set_background_color(s_hint_layer, GColorDarkGray);
  text_layer_set_text_color(s_hint_layer, GColorWhite);
#else
  text_layer_set_background_color(s_hint_layer, GColorBlack);
  text_layer_set_text_color(s_hint_layer, GColorWhite);
#endif
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  refresh_note_layer();
  set_hint("SELECT to dictate");
}

static void window_unload(Window *window) {
  text_layer_destroy(s_hint_layer);
  text_layer_destroy(s_note_layer);
  scroll_layer_destroy(s_scroll_layer);
}

// --------------------------------------------------------------------- main

static void init(void) {
  s_note[0] = '\0';

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(256, NOTE_MAX + 64);

#if defined(PBL_MICROPHONE)
  s_dictation = dictation_session_create(TRANSCRIPT_MAX, dictation_callback, NULL);
  // Skip the confirmation screen? Leave it on - it lets you retry a bad transcription.
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
