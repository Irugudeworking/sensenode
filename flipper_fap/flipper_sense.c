/*
 * Flipper Sense -- a local display/control companion for an ESP32 CSI node.
 *
 * It deliberately displays only room-level activity and presence confidence.
 * UART protocol v1 (115200 8N1):
 *   ESP32 -> Flipper: SENSE,online,armed,calibrating,presence,motion,quality\n
 *   Flipper -> ESP32: PING\n | ARM 0\n | ARM 1\n | CAL\n
 */
#include <furi.h>
#include <furi_hal.h>

#include <expansion/expansion.h>
#include <gui/canvas.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#define SENSE_BAUD 115200
#define SENSE_RX_BUFFER_BYTES 256
#define SENSE_LINE_BYTES 64

typedef struct {
  bool uart_ready;
  bool uart_busy;
  bool online;
  bool armed;
  bool calibrating;
  uint8_t presence;
  uint8_t motion;
  uint8_t quality;
  uint32_t rx_bytes;
} SenseModel;

typedef struct {
  Gui *gui;
  ViewDispatcher *dispatcher;
  View *view;
  FuriThread *worker;
  FuriStreamBuffer *rx_stream;
  FuriHalSerialHandle *serial;
  Expansion *expansion;
  uint32_t last_status_tick;
  uint32_t last_diagnostic_tick;
  volatile uint32_t rx_bytes;
} SenseApp;

enum { WorkerStop = 1 << 0, WorkerRx = 1 << 1 };

static void sense_draw_bar(Canvas *canvas, int16_t y, const char *label,
                           uint8_t value) {
  canvas_draw_str(canvas, 2, y + 7, label);
  canvas_draw_frame(canvas, 48, y, 76, 8);
  uint8_t width = (uint8_t)((value * 74U) / 100U);
  if (width)
    canvas_draw_box(canvas, 49, y + 1, width, 6);
}

static void sense_view_draw(Canvas *canvas, void *_model) {
  SenseModel *model = _model;
  canvas_clear(canvas);
  canvas_set_color(canvas, ColorBlack);
  canvas_set_font(canvas, FontPrimary);
  canvas_draw_str(canvas, 2, 10, "SENSE NODE");
  canvas_set_font(canvas, FontSecondary);

  if (!model->uart_ready) {
    canvas_draw_str(canvas, 2, 28,
                    model->uart_busy ? "UART is busy" : "UART unavailable");
    canvas_draw_str(canvas, 2, 42, "OK: retry connection");
  } else if (!model->online) {
    char rx_text[32];
    snprintf(rx_text, sizeof(rx_text), "RX bytes: %lu",
             (unsigned long)model->rx_bytes);
    canvas_draw_str(canvas, 2, 28, "ESP32 not connected");
    canvas_draw_str(canvas, 2, 42, rx_text);
  } else if (model->calibrating) {
    canvas_draw_str(canvas, 2, 28, "Calibrating room...");
    canvas_draw_str(canvas, 2, 42, "Keep area empty");
  } else if (!model->armed) {
    canvas_draw_str(canvas, 2, 28, "Sensor paused");
    canvas_draw_str(canvas, 2, 42, "OK: start sensing");
  } else {
    sense_draw_bar(canvas, 17, "Presence", model->presence);
    sense_draw_bar(canvas, 29, "Motion", model->motion);
    sense_draw_bar(canvas, 41, "Signal", model->quality);
  }

  canvas_draw_line(canvas, 0, 52, 127, 52);
  canvas_draw_str(canvas, 2, 63, "< Cal   OK Start   > Ping");
}

static void sense_set_status(SenseApp *app, const char *line) {
  unsigned online, armed, calibrating, presence, motion, quality;
  if (sscanf(line, "SENSE,%u,%u,%u,%u,%u,%u", &online, &armed, &calibrating,
             &presence, &motion, &quality) != 6) {
    return;
  }
  with_view_model(
      app->view, SenseModel * model,
      {
        model->online = online != 0;
        model->armed = armed != 0;
        model->calibrating = calibrating != 0;
        model->presence = MIN(presence, 100U);
        model->motion = MIN(motion, 100U);
        model->quality = MIN(quality, 100U);
        model->rx_bytes = app->rx_bytes;
      },
      true);
  uint32_t now = furi_get_tick();
  app->last_status_tick = now;
  if ((now - app->last_diagnostic_tick) >= furi_ms_to_ticks(5000)) {
    FURI_LOG_I("Sense", "ESP32 status received over USART");
    app->last_diagnostic_tick = now;
  }
}

static int32_t sense_worker(void *context) {
  SenseApp *app = context;
  char line[SENSE_LINE_BYTES] = {0};
  size_t position = 0;
  while (true) {
    uint32_t flags =
        furi_thread_flags_wait(WorkerStop | WorkerRx, FuriFlagWaitAny, 250);
    if (flags == (unsigned)FuriFlagErrorTimeout) {
      uint32_t now = furi_get_tick();
      with_view_model(
          app->view, SenseModel * model,
          { model->rx_bytes = app->rx_bytes; }, true);
      if ((now - app->last_status_tick) > furi_ms_to_ticks(3000)) {
        with_view_model(
            app->view, SenseModel * model, { model->online = false; }, true);
        if ((now - app->last_diagnostic_tick) >= furi_ms_to_ticks(5000)) {
          FURI_LOG_W("Sense", "Waiting for ESP32 status on USART; RX bytes: %lu",
                     (unsigned long)app->rx_bytes);
          app->last_diagnostic_tick = now;
        }
      }
      continue;
    }
    if (flags & FuriFlagError)
      break;
    if (flags & WorkerStop)
      break;
    uint8_t byte;
    while (furi_stream_buffer_receive(app->rx_stream, &byte, 1, 0) == 1) {
      if (byte == '\n' || byte == '\r') {
        if (position) {
          line[position] = '\0';
          sense_set_status(app, line);
          position = 0;
        }
      } else if (position + 1 < sizeof(line)) {
        line[position++] = (char)byte;
      } else {
        position = 0;
      }
    }
  }
  return 0;
}

static void sense_rx_irq(FuriHalSerialHandle *handle,
                         FuriHalSerialRxEvent event, void *context) {
  SenseApp *app = context;
  if (app && app->rx_stream && app->worker &&
      (event & FuriHalSerialRxEventData)) {
    uint8_t byte = furi_hal_serial_async_rx(handle);
    app->rx_bytes++;
    furi_stream_buffer_send(app->rx_stream, &byte, 1, 0);
    furi_thread_flags_set(furi_thread_get_id(app->worker), WorkerRx);
  }
}

static bool sense_send(SenseApp *app, const char *command) {
  if (!app || !app->serial || !command)
    return false;
  furi_hal_serial_tx(app->serial, (const uint8_t *)command, strlen(command));
  return true;
}

static void sense_connect(SenseApp *app) {
  if (!app || app->serial)
    return;

  app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
  if (!app->serial) {
    bool busy = furi_hal_serial_control_is_busy(FuriHalSerialIdUsart);
    with_view_model(
        app->view, SenseModel * model,
        {
          model->uart_ready = false;
          model->uart_busy = busy;
          model->online = false;
        },
        true);
    return;
  }

  furi_hal_serial_init(app->serial, SENSE_BAUD);
  app->worker = furi_thread_alloc_ex("SenseUart", 3072, sense_worker, app);
  if (!app->worker) {
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);
    app->serial = NULL;
    with_view_model(
        app->view, SenseModel * model,
        {
          model->uart_ready = false;
          model->uart_busy = false;
          model->online = false;
        },
        true);
    return;
  }

  app->last_status_tick = furi_get_tick();
  app->last_diagnostic_tick = app->last_status_tick;
  furi_thread_start(app->worker);
  furi_hal_serial_async_rx_start(app->serial, sense_rx_irq, app, true);
  with_view_model(
      app->view, SenseModel * model,
      {
        model->uart_ready = true;
        model->uart_busy = false;
      },
      true);
  sense_send(app, "PING\n");
}

static bool sense_view_input(InputEvent *event, void *context) {
  SenseApp *app = context;
  if (event->type != InputTypeShort)
    return false;
  if (event->key == InputKeyLeft) {
    sense_send(app, "CAL\n");
    return true;
  }
  if (event->key == InputKeyRight) {
    sense_send(app, "PING\n");
    return true;
  }
  if (event->key == InputKeyOk) {
    if (!app->serial) {
      sense_connect(app);
      return true;
    }
    bool armed = false;
    with_view_model(
        app->view, SenseModel * model, { armed = model->armed; }, false);
    sense_send(app, armed ? "ARM 0\n" : "ARM 1\n");
    return true;
  }
  return false;
}

static uint32_t sense_previous(void *context) {
  UNUSED(context);
  return VIEW_NONE;
}

int32_t flipper_sense_app(void *arg) {
  UNUSED(arg);
  SenseApp *app = malloc(sizeof(SenseApp));
  memset(app, 0, sizeof(SenseApp));
  app->gui = furi_record_open(RECORD_GUI);
  app->rx_stream = furi_stream_buffer_alloc(SENSE_RX_BUFFER_BYTES, 1);
  app->dispatcher = view_dispatcher_alloc();
  view_dispatcher_attach_to_gui(app->dispatcher, app->gui,
                                ViewDispatcherTypeFullscreen);
  app->view = view_alloc();
  view_allocate_model(app->view, ViewModelTypeLocking, sizeof(SenseModel));
  with_view_model(
      app->view, SenseModel * model, { *model = (SenseModel){0}; }, true);
  view_set_draw_callback(app->view, sense_view_draw);
  view_set_input_callback(app->view, sense_view_input);
  view_set_context(app->view, app);
  view_set_previous_callback(app->view, sense_previous);
  view_dispatcher_add_view(app->dispatcher, 0, app->view);

  /*
   * The board is mounted on the expansion header. Its service can reserve
   * USART1 after it detects a module, so suspend that service before taking
   * the UART and restore it when the app exits.
   */
  app->expansion = furi_record_open(RECORD_EXPANSION);
  expansion_disable(app->expansion);

  /* USART maps to the 9-18 backpack UART: TX=pin 13 (PB6), RX=pin 14 (PB7). */
  sense_connect(app);

  view_dispatcher_switch_to_view(app->dispatcher, 0);
  view_dispatcher_run(app->dispatcher);

  if (app->serial && app->worker) {
    furi_hal_serial_async_rx_stop(app->serial);
    furi_thread_flags_set(furi_thread_get_id(app->worker), WorkerStop);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);
  } else if (app->serial) {
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);
  }
  view_dispatcher_remove_view(app->dispatcher, 0);
  view_free(app->view);
  view_dispatcher_free(app->dispatcher);
  furi_stream_buffer_free(app->rx_stream);
  expansion_enable(app->expansion);
  furi_record_close(RECORD_EXPANSION);
  furi_record_close(RECORD_GUI);
  free(app);
  return 0;
}
