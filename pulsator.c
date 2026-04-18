/*
 * 5V Pulsator v1.1 — Interrupt Tester für Flipper Zero (Momentum Firmware)
 *
 * Alle drei Signale verlaufen synchron:
 *   Pin 1  (5V   OTG)   — schaltbarer 5V-Ausgang
 *   Pin 7  (3.3V PC3)   — GPIO-Ausgang, identisches Signal bei 3.3V-Pegel
 *   Intern (LED blau)   — Zustandsanzeige, spiegelt Pin 1 / Pin 7
 *
 * Bedienung:
 *   Intro-Screen : OK startet, BACK beendet
 *   Main-Screen  : OK       = Impuls (alle Signale kurzzeitig invertieren)
 *                  LongOK   = Modus dauerhaft invertieren (default-Zustand)
 *                  UP / DN  = Pulsdauer +/– delta
 *                  RIGHT/LT = delta × 10  /  delta ÷ 10
 *                  BACK     = App beenden (alle Signale OFF)
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_power.h>
#include <furi_hal_light.h>
#include <furi_hal_gpio.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#define TAG           "Pulsator"
#define DISPLAY_W     128
#define DISPLAY_H     64
#define TOAST_MS      1000UL
#define DELTA_MIN     1UL
#define DELTA_MAX     10000UL
#define PULSE_MIN     1UL
#define PULSE_MAX     600000UL   /* 10 min cap */

/* 3.3V-Signal: Pin 7 des externen Headers = PC3 */
#define GPIO_3V3      gpio_ext_pc3

typedef enum { ViewIntro, ViewMain } ViewId;
typedef enum { WorkerCmdPulse, WorkerCmdExit } WorkerCmd;

typedef struct {
    FuriMutex*        mutex;
    FuriMessageQueue* input_queue;
    FuriMessageQueue* worker_queue;
    FuriThread*       worker;
    ViewPort*         vp;
    Gui*              gui;
    NotificationApp*  notifications;

    /* Zustand (mutex-geschützt) */
    ViewId   view;
    bool     mode_on;      /* true: alle Signale default ON; false: default OFF */
    uint32_t pulse_ms;
    uint32_t delta_ms;
    bool     pulsing;
    char     toast[40];
    uint32_t toast_until;

    volatile bool running;
} App;

/* ================================================================
   Hardware-Helfer
   ================================================================ */

/** Setzt 5V-OTG, PC3-GPIO und interne blaue LED synchron. */
static void pulsator_set_signal(bool on) {
    /* 5V OTG */
    if(on) {
        if(!furi_hal_power_is_otg_enabled()) furi_hal_power_enable_otg();
    } else {
        if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
    }

    /* 3.3V GPIO PC3 (Pin 7) */
    furi_hal_gpio_write(&GPIO_3V3, on);

    /* Interne LED (blau) */
    furi_hal_light_set(LightBlue, on ? 255 : 0);
}

/* ================================================================
   Zeichnen
   ================================================================ */

/* Kompakte Zeitdarstellung ohne Leerzeichen zwischen Zahl und Einheit:
 *  < 10000 ms  →  "9999ms"    (max 6 Zeichen ≈ 30 px)
 *  ≥ 10000 ms  →  "10.0s"     (max 6 Zeichen ≈ 30 px)
 * Garantiert keinen Überlauf ins rechte Feld oder über den Displayrand.         */
static void format_ms(char* buf, size_t n, uint32_t ms) {
    if(ms < 10000UL) {
        snprintf(buf, n, "%lums", (unsigned long)ms);
    } else {
        uint32_t s    = ms / 1000UL;
        uint32_t frac = (ms % 1000UL) / 100UL;   /* eine Nachkommastelle */
        snprintf(buf, n, "%lu.%lus", (unsigned long)s, (unsigned long)frac);
    }
}

static void draw_toast(Canvas* canvas, const char* text) {
    uint16_t tw = canvas_string_width(canvas, text);
    int16_t bw  = (int16_t)tw + 10;
    if(bw > DISPLAY_W - 4) bw = DISPLAY_W - 4;
    int16_t bx = (DISPLAY_W - bw) / 2;
    int16_t by = 24;
    int16_t bh = 14;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, bx - 1, by - 1, bw + 2, bh + 2);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, bx - 1, by - 1, bw + 2, bh + 2);
    canvas_draw_box(canvas, bx, by, bw, bh);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, bx + bw / 2, by + bh / 2, AlignCenter, AlignCenter, text);
    canvas_invert_color(canvas);
}

static void draw_intro(Canvas* canvas) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, "Power Interrupt Tester");
    canvas_draw_line(canvas, 0, 10, 127, 10);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 22, "Pin 1     = +5V out");
    canvas_draw_str(canvas, 2, 30, "Pin 7/PC3 = 3.3V out");
    canvas_draw_str(canvas, 2, 38, "Pin 8/18  = GND");
    canvas_draw_str(canvas, 2, 46, "Blau-LED spiegelt Signal");
    canvas_draw_str(canvas, 2, 55, "OK:puls  LongOK:mode");
    canvas_draw_str(canvas, 2, 63, ">> Press OK to start");
}

static void draw_main(Canvas* canvas, App* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    /* ── Titelzeile ── */
    canvas_draw_str(canvas, 2, 8, "Power Interrupt Tester");
    canvas_draw_line(canvas, 0, 10, 127, 10);

    bool level = app->pulsing ? !app->mode_on : app->mode_on;
    char val[12];

    /* ── Zeile 1: Mode (links) | Pin (rechts) ──────────────────────────────────
     * Linker Wert fix ab x=28 (direkt nach "Mode:" ~24 px).
     * Rechter Wert rechtsbündig an x=127: "HIGH"/"LOW" laufen nie über den Rand
     * und überlappen nie "Pin:" (endet ~x=82, Wert beginnt frühestens x=107).  */
    canvas_draw_str(canvas, 2,  24, "Mode:");
    canvas_draw_str(canvas, 28, 24, app->mode_on ? "ON" : "OFF");   /* fix x=28 */

    canvas_draw_str(canvas, 68, 24, "Pin:");
    canvas_draw_str(canvas, 68 + 23, 24, level ? "HIGH" : "LOW");   /* direkt nach Label */

    /* Puls-Indikator: 3×3-Punkt in Titelzeile, nur bei aktivem Impuls */
    if(app->pulsing) {
        canvas_draw_box(canvas, 122, 14, 3, 3);
    }

    /* ── Zeile 2: Puls (links) | Delta (rechts) ────────────────────────────────
     * format_ms liefert max 6 Zeichen (~30 px):
     *   Puls-Wert ab x=28 endet max bei x=58  →  7 px Abstand zu "Delta:" x=65
     *   Delta-Wert rechtsbündig an x=127       →  kein Überlauf über Displayrand
     *   "Delta:" endet ~x=95, Wert beginnt frühestens x=97 → kein interner Überlapp */
    canvas_draw_str(canvas, 2,  38, "Puls:");
    format_ms(val, sizeof(val), app->pulse_ms);
    canvas_draw_str(canvas, 28, 38, val);                            /* fix x=28 */

    canvas_draw_str(canvas, 65, 38, "Delta:");
    format_ms(val, sizeof(val), app->delta_ms);
    canvas_draw_str(canvas, 65 + 34, 38, val);                       /* direkt nach Label */

    /* ── Statuszeilen ── */
    canvas_draw_str(canvas, 2, 55, "OK:puls  hold OK:mode");
    canvas_draw_str(canvas, 2, 63, "U/D:dur  L/R:Delta/10*10");

    /* Toast */
    if(app->toast[0] != '\0' && furi_get_tick() < app->toast_until) {
        draw_toast(canvas, app->toast);
    }
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->view == ViewIntro) draw_intro(canvas);
    else draw_main(canvas, app);
    furi_mutex_release(app->mutex);
}

/* ================================================================
   Input-Callback (nur Weiterleitung an Queue)
   ================================================================ */

static void input_callback(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->input_queue, event, 0);
}

/* ================================================================
   Worker-Thread (Puls-Zeitsteuerung, darf furi_delay_ms nutzen)
   ================================================================ */

static int32_t worker_thread(void* ctx) {
    App* app = ctx;
    WorkerCmd cmd;

    while(true) {
        if(furi_message_queue_get(app->worker_queue, &cmd, FuriWaitForever) != FuriStatusOk)
            continue;
        if(cmd == WorkerCmdExit) break;

        if(cmd == WorkerCmdPulse) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            uint32_t duration = app->pulse_ms;
            bool mode         = app->mode_on;
            app->pulsing      = true;
            furi_mutex_release(app->mutex);

            /* Alle Signale invertieren */
            pulsator_set_signal(!mode);
            view_port_update(app->vp);

            /* Impulsdauer abwarten — unterbrechbar durch Exit-Kommando */
            WorkerCmd next = WorkerCmdPulse;
            FuriStatus s   = furi_message_queue_get(app->worker_queue, &next, duration);

            /* Alle Signale zurücksetzen */
            pulsator_set_signal(mode);

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->pulsing = false;
            furi_mutex_release(app->mutex);

            view_port_update(app->vp);

            if(s == FuriStatusOk && next == WorkerCmdExit) break;
            /* Weitere Pulse während eines laufenden Impulses werden ignoriert */
        }
    }
    return 0;
}

/* ================================================================
   Input-Logik
   ================================================================ */

static void pulsator_toast_locked(App* app, const char* msg) {
    /* Mutex muss vom Aufrufer gehalten werden */
    size_t n = sizeof(app->toast) - 1;
    size_t i = 0;
    while(i < n && msg[i] != '\0') { app->toast[i] = msg[i]; i++; }
    app->toast[i]    = '\0';
    app->toast_until = furi_get_tick() + TOAST_MS;
}

static void handle_intro_input(App* app, InputEvent* event) {
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyOk) {
        app->view = ViewMain;
        pulsator_set_signal(true); /* 5V + PC3 + LED an */
    } else if(event->key == InputKeyBack) {
        app->running = false;
    }
}

static void handle_main_input(App* app, InputEvent* event) {
    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyOk: {
            WorkerCmd cmd = WorkerCmdPulse;
            furi_message_queue_put(app->worker_queue, &cmd, 0);
            break;
        }
        case InputKeyUp:
            app->pulse_ms = (app->pulse_ms + app->delta_ms <= PULSE_MAX)
                                ? app->pulse_ms + app->delta_ms
                                : PULSE_MAX;
            break;
        case InputKeyDown:
            app->pulse_ms = (app->pulse_ms > app->delta_ms + PULSE_MIN - 1UL)
                                ? app->pulse_ms - app->delta_ms
                                : PULSE_MIN;
            break;
        case InputKeyRight:
            app->delta_ms = (app->delta_ms * 10UL <= DELTA_MAX)
                                ? app->delta_ms * 10UL
                                : DELTA_MAX;
            break;
        case InputKeyLeft:
            app->delta_ms = (app->delta_ms >= 10UL)
                                ? app->delta_ms / 10UL
                                : DELTA_MIN;
            break;
        case InputKeyBack:
            app->running = false;
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        app->mode_on = !app->mode_on;
        pulsator_set_signal(app->mode_on);
        pulsator_toast_locked(app, app->mode_on ? "Mode: ON" : "Mode: OFF");
    }
}

/* ================================================================
   App Entry Point
   ================================================================ */

int32_t pulsator_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    app->mutex        = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue  = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->worker_queue = furi_message_queue_alloc(8, sizeof(WorkerCmd));

    app->view     = ViewIntro;
    app->mode_on  = true;
    app->pulse_ms = 100;
    app->delta_ms = 10;
    app->pulsing  = false;
    app->toast[0] = '\0';
    app->toast_until = 0;
    app->running  = true;

    /* GPIO PC3 (Pin 7) als Push-Pull-Ausgang, initial LOW */
    furi_hal_gpio_init(&GPIO_3V3, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(&GPIO_3V3, false);

    /* LED initial aus */
    furi_hal_light_set(LightBlue, 0);

    /* Backlight dauerhaft an */
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);

    /* GUI */
    app->vp = view_port_alloc();
    view_port_draw_callback_set(app->vp, draw_callback, app);
    view_port_input_callback_set(app->vp, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->vp, GuiLayerFullscreen);

    /* Worker starten */
    app->worker = furi_thread_alloc_ex("PulsatorWorker", 1024, worker_thread, app);
    furi_thread_start(app->worker);

    /* ── Haupt-Loop ── */
    InputEvent event;
    while(app->running) {
        FuriStatus s = furi_message_queue_get(app->input_queue, &event, 100);
        if(s == FuriStatusOk) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->view == ViewIntro) handle_intro_input(app, &event);
            else                       handle_main_input(app, &event);
            furi_mutex_release(app->mutex);
        }
        view_port_update(app->vp);
    }

    /* ── Cleanup ── */

    /* Alle Ausgänge abschalten */
    pulsator_set_signal(false);

    /* Worker sauber beenden (unterbricht laufenden Impuls sofort) */
    WorkerCmd cmd = WorkerCmdExit;
    furi_message_queue_put(app->worker_queue, &cmd, FuriWaitForever);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    /* GPIO freigeben (Analog = hochohmig / floating) */
    furi_hal_gpio_init(&GPIO_3V3, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

    /* GUI abbauen */
    gui_remove_view_port(app->gui, app->vp);
    view_port_free(app->vp);
    furi_record_close(RECORD_GUI);

    /* Backlight zurück auf Auto */
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);

    furi_message_queue_free(app->input_queue);
    furi_message_queue_free(app->worker_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
