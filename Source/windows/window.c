/*
 * window.c - the PuTTY(tel)/pterm main program, which runs a PuTTY
 * terminal emulator and backend in a window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>
#include <wchar.h>

#define COMPILE_MULTIMON_STUBS

#include "putty.h"
#include "ssh.h"
#include "terminal.h"
#include "storage.h"
#include "putty-rc.h"
#include "security-api.h"
#include "win-gui-seat.h"
#include "tree234.h"

#ifdef NO_MULTIMON
#include <multimon.h>
#endif

#include <imm.h>
#include <commctrl.h>
#include <richedit.h>
#include <mmsystem.h>

/* From MSDN: In the WM_SYSCOMMAND message, the four low-order bits of
 * wParam are used by Windows, and should be masked off, so we shouldn't
 * attempt to store information in them. Hence all these identifiers have
 * the low 4 bits clear. Also, identifiers should < 0xF000. */

#define IDM_SHOWLOG   0x0010
#define IDM_NEWSESS   0x0020
#define IDM_DUPSESS   0x0030
#define IDM_RESTART   0x0040
#define IDM_RECONF    0x0050
#define IDM_CLRSB     0x0060
#define IDM_RESET     0x0070
#define IDM_HELP      0x0140
#define IDM_ABOUT     0x0150
#define IDM_SAVEDSESS 0x0160
#define IDM_COPYALL   0x0170
#define IDM_FULLSCREEN  0x0180
#define IDM_COPY      0x0190
#define IDM_PASTE     0x01A0
#define IDM_SPECIALSEP 0x0200

#define IDM_SPECIAL_MIN 0x0400
#define IDM_SPECIAL_MAX 0x0800

#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x5000
#define MENU_SAVED_STEP 16
/* Maximum number of sessions on saved-session submenu */
#define MENU_SAVED_MAX ((IDM_SAVED_MAX-IDM_SAVED_MIN) / MENU_SAVED_STEP)

#define WM_IGNORE_CLIP (WM_APP + 2)
#define WM_FULLSCR_ON_MAX (WM_APP + 3)
#define WM_GOT_CLIPDATA (WM_APP + 4)

/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

/* Mouse wheel support. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A           /* not defined in earlier SDKs */
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E          /* not defined in earlier SDKs */
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

/* DPI awareness support */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

/* VK_PACKET, used to send Unicode characters in WM_KEYDOWNs */
#ifndef VK_PACKET
#define VK_PACKET 0xE7
#endif

static Mouse_Button translate_button(WinGuiSeat *wgs, Mouse_Button button);
static void show_mouseptr(WinGuiSeat *wgs, bool show);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(WinGuiSeat *wgs, UINT message, WPARAM wParam,
                        LPARAM lParam, unsigned char *output);
static void init_palette(WinGuiSeat *wgs);
static void init_fonts(WinGuiSeat *wgs, int, int);
static void init_dpi_info(WinGuiSeat *wgs);
static void another_font(WinGuiSeat *wgs, int);
static void deinit_fonts(WinGuiSeat *wgs);
static void set_input_locale(WinGuiSeat *wgs, HKL);
static void update_savedsess_menu(WinGuiSeat *wgs);
static void init_winfuncs(void);

static bool is_full_screen(WinGuiSeat *wgs);
static void make_full_screen(WinGuiSeat *wgs);
static void clear_full_screen(WinGuiSeat *wgs);
static void flip_full_screen(WinGuiSeat *wgs);
static void process_clipdata(WinGuiSeat *wgs, HGLOBAL clipdata, bool unicode);
static void setup_clipboards(Terminal *, Conf *);

/* Window layout information */
static void reset_window(WinGuiSeat *wgs, int reinit);

static void flash_window(WinGuiSeat *wgs, int mode);
static void sys_cursor_update(WinGuiSeat *wgs);
static bool get_fullscreen_rect(WinGuiSeat *wgs, RECT *ss);
static bool get_workingarea_rect(WinGuiSeat *wgs, RECT *ss);

static void conf_cache_data(WinGuiSeat *wgs);

static struct sesslist sesslist;       /* for saved-session menu */

enum MONITOR_DPI_TYPE { MDT_EFFECTIVE_DPI, MDT_ANGULAR_DPI, MDT_RAW_DPI, MDT_DEFAULT };
DECL_WINDOWS_FUNCTION(static, BOOL, GetMonitorInfoA, (HMONITOR, LPMONITORINFO));
DECL_WINDOWS_FUNCTION(static, HMONITOR, MonitorFromPoint, (POINT, DWORD));
DECL_WINDOWS_FUNCTION(static, HMONITOR, MonitorFromWindow, (HWND, DWORD));
DECL_WINDOWS_FUNCTION(static, HRESULT, GetDpiForMonitor, (HMONITOR hmonitor, enum MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY));
DECL_WINDOWS_FUNCTION(static, HRESULT, GetSystemMetricsForDpi, (int nIndex, UINT dpi));
DECL_WINDOWS_FUNCTION(static, HRESULT, AdjustWindowRectExForDpi, (LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi));

static UINT wm_mousewheel = WM_MOUSEWHEEL;

struct WinGuiSeatListNode wgslisthead = {
    .next = &wgslisthead, .prev = &wgslisthead,
};

#define IS_HIGH_VARSEL(wch1, wch2) \
    ((wch1) == 0xDB40 && ((wch2) >= 0xDD00 && (wch2) <= 0xDDEF))
#define IS_LOW_VARSEL(wch) \
    (((wch) >= 0x180B && (wch) <= 0x180D) || /* MONGOLIAN FREE VARIATION SELECTOR */ \
     ((wch) >= 0xFE00 && (wch) <= 0xFE0F)) /* VARIATION SELECTOR 1-16 */

static bool wintw_setup_draw_ctx(TermWin *);
static void wintw_draw_text(TermWin *, int x, int y, wchar_t *text, int len,
                            unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_cursor(TermWin *, int x, int y, wchar_t *text, int len,
                              unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_trust_sigil(TermWin *, int x, int y);
static int wintw_char_width(TermWin *, int uc);
static void wintw_free_draw_ctx(TermWin *);
static void wintw_set_cursor_pos(TermWin *, int x, int y);
static void wintw_set_raw_mouse_mode(TermWin *, bool enable);
static void wintw_set_raw_mouse_mode_pointer(TermWin *, bool enable);
static void wintw_set_scrollbar(TermWin *, int total, int start, int page);
static void wintw_bell(TermWin *, int mode);
static void wintw_clip_write(
    TermWin *, int clipboard, wchar_t *text, int *attrs,
    truecolour *colours, int len, bool must_deselect);
static void wintw_clip_request_paste(TermWin *, int clipboard);
static void wintw_refresh(TermWin *);
static void wintw_request_resize(TermWin *, int w, int h);
static void wintw_set_title(TermWin *, const char *title, int codepage);
static void wintw_set_icon_title(TermWin *, const char *icontitle,
                                 int codepage);
static void wintw_set_minimised(TermWin *, bool minimised);
static void wintw_set_maximised(TermWin *, bool maximised);
static void wintw_move(TermWin *, int x, int y);
static void wintw_set_zorder(TermWin *, bool top);
static void wintw_palette_set(TermWin *, unsigned, unsigned, const rgb *);
static void wintw_palette_get_overrides(TermWin *, Terminal *);
static void wintw_unthrottle(TermWin *win, size_t bufsize);

static const TermWinVtable windows_termwin_vt = {
    .setup_draw_ctx = wintw_setup_draw_ctx,
    .draw_text = wintw_draw_text,
    .draw_cursor = wintw_draw_cursor,
    .draw_trust_sigil = wintw_draw_trust_sigil,
    .char_width = wintw_char_width,
    .free_draw_ctx = wintw_free_draw_ctx,
    .set_cursor_pos = wintw_set_cursor_pos,
    .set_raw_mouse_mode = wintw_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = wintw_set_raw_mouse_mode_pointer,
    .set_scrollbar = wintw_set_scrollbar,
    .bell = wintw_bell,
    .clip_write = wintw_clip_write,
    .clip_request_paste = wintw_clip_request_paste,
    .refresh = wintw_refresh,
    .request_resize = wintw_request_resize,
    .set_title = wintw_set_title,
    .set_icon_title = wintw_set_icon_title,
    .set_minimised = wintw_set_minimised,
    .set_maximised = wintw_set_maximised,
    .move = wintw_move,
    .set_zorder = wintw_set_zorder,
    .palette_set = wintw_palette_set,
    .palette_get_overrides = wintw_palette_get_overrides,
    .unthrottle = wintw_unthrottle,
};

static HICON trust_icon = INVALID_HANDLE_VALUE;

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

static bool is_utf8(WinGuiSeat *wgs)
{
    return wgs->ucsdata.line_codepage == CP_UTF8;
}

static bool win_seat_is_utf8(Seat *seat)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    return is_utf8(wgs);
}

static char *win_seat_get_ttymode(Seat *seat, const char *mode)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    return term_get_ttymode(wgs->term, mode);
}

static StripCtrlChars *win_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    return stripctrl_new_term(bs_out, false, 0, wgs->term);
}

static size_t win_seat_output(
    Seat *seat, SeatOutputType type, const void *, size_t);
static bool win_seat_eof(Seat *seat);
static SeatPromptResult win_seat_get_userpass_input(Seat *seat, prompts_t *p);
static void win_seat_notify_remote_exit(Seat *seat);
static void win_seat_connection_fatal(Seat *seat, const char *msg);
static void win_seat_nonfatal(Seat *seat, const char *msg);
static void win_seat_update_specials_menu(Seat *seat);
static void win_seat_set_busy_status(Seat *seat, BusyStatus status);
static void win_seat_set_trust_status(Seat *seat, bool trusted);
static bool win_seat_can_set_trust_status(Seat *seat);
static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y);
static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y);

static const SeatVtable win_seat_vt = {
    .output = win_seat_output,
    .eof = win_seat_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = win_seat_get_userpass_input,
    .notify_session_started = nullseat_notify_session_started,
    .notify_remote_exit = win_seat_notify_remote_exit,
    .notify_remote_disconnect = nullseat_notify_remote_disconnect,
    .connection_fatal = win_seat_connection_fatal,
    .nonfatal = win_seat_nonfatal,
    .update_specials_menu = win_seat_update_specials_menu,
    .get_ttymode = win_seat_get_ttymode,
    .set_busy_status = win_seat_set_busy_status,
    .confirm_ssh_host_key = win_seat_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = win_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = win_seat_confirm_weak_cached_hostkey,
    .prompt_descriptions = win_seat_prompt_descriptions,
    .is_utf8 = win_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = win_seat_get_window_pixel_size,
    .stripctrl_new = win_seat_stripctrl_new,
    .set_trust_status = win_seat_set_trust_status,
    .can_set_trust_status = win_seat_can_set_trust_status,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_yes,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = win_seat_get_cursor_position,
};

static void start_backend(WinGuiSeat *wgs)
{
    const struct BackendVtable *vt;
    char *error, *realhost;
    int i;

    wgs->cmdline_get_passwd_state = cmdline_get_passwd_input_state_new;

    vt = backend_vt_from_conf(wgs->conf);

    seat_set_trust_status(&wgs->seat, true);
    error = backend_init(vt, &wgs->seat, &wgs->backend, wgs->logctx, wgs->conf,
                         conf_get_str(wgs->conf, CONF_host),
                         conf_get_int(wgs->conf, CONF_port),
                         &realhost,
                         conf_get_bool(wgs->conf, CONF_tcp_nodelay),
                         conf_get_bool(wgs->conf, CONF_tcp_keepalives));
    if (error) {
        char *str = dupprintf("%s Error", appname);
        char *msg;
        if (cmdline_tooltype & TOOLTYPE_NONNETWORK) {
            /* Special case for pterm. */
            msg = dupprintf("Unable to open terminal:\n%s", error);
        } else {
            msg = dupprintf("Unable to open connection to\n%s\n%s",
                            conf_dest(wgs->conf), error);
        }
        sfree(error);
        MessageBox(NULL, msg, str, MB_ICONERROR | MB_OK);
        sfree(str);
        sfree(msg);
        exit(0);
    }
    term_setup_window_titles(wgs->term, realhost);
    sfree(realhost);

    /*
     * Connect the terminal to the backend for resize purposes.
     */
    term_provide_backend(wgs->term, wgs->backend);

    /*
     * Set up a line discipline.
     */
    wgs->ldisc = ldisc_create(wgs->conf, wgs->term, wgs->backend, &wgs->seat);

    /*
     * Destroy the Restart Session menu item. (This will return
     * failure if it's already absent, as it will be the very first
     * time we call this function. We ignore that, because as long
     * as the menu item ends up not being there, we don't care
     * whether it was us who removed it or not!)
     */
    for (i = 0; i < lenof(wgs->popup_menus); i++) {
        DeleteMenu(wgs->popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
    }

    wgs->session_closed = false;
}

static void close_session(void *vctx)
{
    WinGuiSeat *wgs = (WinGuiSeat *)vctx;
    char *newtitle;
    int i;

    wgs->session_closed = true;
    newtitle = dupprintf("%s (inactive)", appname);
    win_set_icon_title(&wgs->termwin, newtitle, DEFAULT_CODEPAGE);
    win_set_title(&wgs->termwin, newtitle, DEFAULT_CODEPAGE);
    sfree(newtitle);

    if (wgs->ldisc) {
        ldisc_free(wgs->ldisc);
        wgs->ldisc = NULL;
    }
    if (wgs->backend) {
        backend_free(wgs->backend);
        wgs->backend = NULL;
        term_provide_backend(wgs->term, NULL);
        seat_update_specials_menu(&wgs->seat);
    }

    /*
     * Show the Restart Session menu item. Do a precautionary
     * delete first to ensure we never end up with more than one.
     */
    for (i = 0; i < lenof(wgs->popup_menus); i++) {
        DeleteMenu(wgs->popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
        InsertMenu(wgs->popup_menus[i].menu, IDM_DUPSESS,
                   MF_BYCOMMAND | MF_ENABLED, IDM_RESTART, "&Restart Session");
    }
}

/*
 * Some machinery to deal with switching the window type between ANSI
 * and Unicode. We prefer Unicode, but some PuTTY builds still try to
 * run on machines so old that they don't support that mode. So we're
 * prepared to fall back to an ANSI window if we have to. For this
 * purpose, we swap out a few Windows API functions, and wrap
 * SetWindowText so that if we're not in Unicode mode we first convert
 * the wide string we're given.
 */
static bool unicode_window;
static BOOL (WINAPI *sw_PeekMessage)(LPMSG, HWND, UINT, UINT, UINT);
static LRESULT (WINAPI *sw_DispatchMessage)(const MSG *);
static LRESULT (WINAPI *sw_DefWindowProc)(HWND, UINT, WPARAM, LPARAM);
static void sw_SetWindowText(HWND hwnd, wchar_t *text)
{
    if (unicode_window) {
        SetWindowTextW(hwnd, text);
    } else {
        char *mb = dup_wc_to_mb(DEFAULT_CODEPAGE, text, "?");
        SetWindowTextA(hwnd, mb);
        sfree(mb);
    }
}

static HINSTANCE hprev;

/*
 * Also, registering window classes has to be done in a fiddly way.
 */
#define SETUP_WNDCLASS(wndclass, classname) do {                        \
        wndclass.style = 0;                                             \
        wndclass.lpfnWndProc = WndProc;                                 \
        wndclass.cbClsExtra = 0;                                        \
        wndclass.cbWndExtra = 0;                                        \
        wndclass.hInstance = hinst;                                     \
        wndclass.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_MAINICON)); \
        wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);                 \
        wndclass.hbrBackground = NULL;                                  \
        wndclass.lpszMenuName = NULL;                                   \
        wndclass.lpszClassName = classname;                             \
    } while (0)
wchar_t *terminal_window_class_w(void)
{
    static wchar_t *classname = NULL;
    if (!classname)
        classname = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
    if (!hprev) {
        WNDCLASSW wndclassw;
        SETUP_WNDCLASS(wndclassw, classname);
        RegisterClassW(&wndclassw);
    }
    return classname;
}
char *terminal_window_class_a(void)
{
    static char *classname = NULL;
    if (!classname)
        classname = dupcat(appname, ".ansi");
    if (!hprev) {
        WNDCLASSA wndclassa;
        SETUP_WNDCLASS(wndclassa, classname);
        RegisterClassA(&wndclassa);
    }
    return classname;
}

HINSTANCE hinst;

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    HRESULT hr;
    int guess_width, guess_height;

    dll_hijacking_protection();
    enable_dit();

    hinst = inst;
    hprev = prev;

/* PuttyDriver #10 - Putty is starting. */
#ifdef PuttyDriver
    putty_driver = false;

    vterm_nocapture = false;

    vterm_screen_speed = -1;
    
    vterm_started = false;

    memset(vterm_capture_file, 0, sizeof(vterm_capture_file));
#endif	
/* PuttyDriver */

    sk_init();

    init_common_controls();

    /* Set Explicit App User Model Id so that jump lists don't cause
       PuTTY to hang on to removable media. */

    set_explicit_app_user_model_id();

    /* Ensure a Maximize setting in Explorer doesn't maximise the
     * config box. */
    defuse_showwindow();

    init_winver();

    /*
     * If we're running a version of Windows that doesn't support
     * WM_MOUSEWHEEL, find out what message number we should be
     * using instead.
     */
    if (osMajorVersion < 4 ||
        (osMajorVersion == 4 && osPlatformId != VER_PLATFORM_WIN32_NT))
        wm_mousewheel = RegisterWindowMessage("MSWHEEL_ROLLMSG");

    init_help();

    init_winfuncs();

    setup_gui_timing();

    WinGuiSeat *wgs = snew(WinGuiSeat);
    memset(wgs, 0, sizeof(*wgs));
    wgs_link(wgs);

    wgs->seat.vt = &win_seat_vt;
    wgs->logpolicy.vt = &win_gui_logpolicy_vt;
    wgs->termwin.vt = &windows_termwin_vt;

    wgs->caret_x = wgs->caret_y = -1;
    wgs->busy_status = BUSY_NOT;

    wgs->conf = conf_new();

    /*
     * Initialize COM.
     */
    hr = CoInitialize(NULL);
    if (hr != S_OK && hr != S_FALSE) {
        char *str = dupprintf("%s Fatal Error", appname);
        MessageBox(NULL, "Failed to initialize COM subsystem",
                   str, MB_OK | MB_ICONEXCLAMATION);
        sfree(str);
        return 1;
    }

    /*
     * Process the command line.
     * (If the command line doesn't provide enough info to start a
     * session, this will detour via the config box.)
     */
    gui_term_process_cmdline(wgs->conf, cmdline);

    memset(&wgs->ucsdata, 0, sizeof(wgs->ucsdata));

    conf_cache_data(wgs);

    /*
     * Guess some defaults for the window size. This all gets
     * updated later, so we don't really care too much. However, we
     * do want the font width/height guesses to correspond to a
     * large font rather than a small one...
     */

    wgs->font_width = 10;
    wgs->font_height = 20;
    wgs->extra_width = 25;
    wgs->extra_height = 28;
    guess_width = wgs->extra_width + wgs->font_width * conf_get_int(
        wgs->conf, CONF_width);
    guess_height = wgs->extra_height + wgs->font_height * conf_get_int(
        wgs->conf, CONF_height);
    {
        RECT r;
        get_fullscreen_rect(wgs, &r);
        if (guess_width > r.right - r.left)
            guess_width = r.right - r.left;
        if (guess_height > r.bottom - r.top)
            guess_height = r.bottom - r.top;
    }

    {
        int winmode = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
        int exwinmode = 0;
        const struct BackendVtable *vt =
            backend_vt_from_proto(be_default_protocol);
        bool resize_forbidden = false;
        if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
            resize_forbidden = true;
        wchar_t *uappname = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
        wgs->window_name = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
        wgs->icon_name = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
        if (!conf_get_bool(wgs->conf, CONF_scrollbar))
            winmode &= ~(WS_VSCROLL);
        if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_DISABLED ||
            resize_forbidden)
            winmode &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        if (conf_get_bool(wgs->conf, CONF_alwaysontop))
            exwinmode |= WS_EX_TOPMOST;
        if (conf_get_bool(wgs->conf, CONF_sunken_edge))
            exwinmode |= WS_EX_CLIENTEDGE;

#ifdef TEST_ANSI_WINDOW
        /* For developer testing of ANSI window support, pretend
         * CreateWindowExW failed */
        wgs->term_hwnd = NULL;
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
#else
        unicode_window = true;
        sw_PeekMessage = PeekMessageW;
        sw_DispatchMessage = DispatchMessageW;
        sw_DefWindowProc = DefWindowProcW;
        wgs->term_hwnd = CreateWindowExW(
            exwinmode, terminal_window_class_w(), uappname,
            winmode, CW_USEDEFAULT, CW_USEDEFAULT,
            guess_width, guess_height, NULL, NULL, inst, NULL);
#endif

#if defined LEGACY_WINDOWS || defined TEST_ANSI_WINDOW
        if (!wgs->term_hwnd && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
            /* Fall back to an ANSI window, swapping in all the ANSI
             * window message handling functions */
            unicode_window = false;
            sw_PeekMessage = PeekMessageA;
            sw_DispatchMessage = DispatchMessageA;
            sw_DefWindowProc = DefWindowProcA;
            wgs->term_hwnd = CreateWindowExA(
                exwinmode, terminal_window_class_a(), appname,
                winmode, CW_USEDEFAULT, CW_USEDEFAULT,
                guess_width, guess_height, NULL, NULL, inst, NULL);
        }
#endif

        if (!wgs->term_hwnd) {
            modalfatalbox("Unable to create terminal window: %s",
                          win_strerror(GetLastError()));
        }
        memset(&wgs->dpi_info, 0, sizeof(struct _dpi_info));
        init_dpi_info(wgs);
        sfree(uappname);
    }

    SetWindowLongPtr(wgs->term_hwnd, GWLP_USERDATA, (LONG_PTR)wgs);

    /*
     * Initialise the fonts, simultaneously correcting the guesses
     * for font_{width,height}.
     */
    init_fonts(wgs, 0, 0);

    /*
     * Prepare a logical palette.
     */
    init_palette(wgs);

    /*
     * Initialise the terminal. (We have to do this _after_
     * creating the window, since the terminal is the first thing
     * which will call schedule_timer(), which will in turn call
     * timer_change_notify() which will expect hwnd to exist.)
     */
    wgs->term = term_init(wgs->conf, &wgs->ucsdata, &wgs->termwin);
    setup_clipboards(wgs->term, wgs->conf);
    wgs->logctx = log_init(&wgs->logpolicy, wgs->conf);
    term_provide_logctx(wgs->term, wgs->logctx);
    term_size(wgs->term, conf_get_int(wgs->conf, CONF_height),
              conf_get_int(wgs->conf, CONF_width),
              conf_get_int(wgs->conf, CONF_savelines));

    /*
     * Correct the guesses for extra_{width,height}.
     */
    {
        RECT cr, wr;
        GetWindowRect(wgs->term_hwnd, &wr);
        GetClientRect(wgs->term_hwnd, &cr);
        wgs->offset_width = wgs->offset_height =
            conf_get_int(wgs->conf, CONF_window_border);
        wgs->extra_width =
            wr.right - wr.left - cr.right + cr.left + wgs->offset_width*2;
        wgs->extra_height =
            wr.bottom - wr.top - cr.bottom + cr.top +wgs->offset_height*2;
    }

    /*
     * Compute what size we _really_ want the window to be.
     */
    guess_width = wgs->extra_width + wgs->font_width * wgs->term->cols;
    guess_height = wgs->extra_height + wgs->font_height * wgs->term->rows;

    /*
     * Resize the window to that size, also repositioning it if it's extended
     * off the edge of a monitor.
     */
    {
        /* Find the previous coordinates of the window */
        RECT winr;
        GetWindowRect(wgs->term_hwnd, &winr);

        int x = winr.left;
        int y = winr.top;

        /* Adjust them if necessary */
        RECT war;
        if (get_workingarea_rect(wgs, &war)) {
            /*
             * Try to ensure the window is entirely within the monitor's
             * working area, by adjusting its position if not.
             *
             * We first move it left, if it overlaps off the right side. Then
             * we move it right if it overlaps off the left side. This means
             * that if it's wider than the working area (so that some overlap
             * is unavoidable), we prefer to get its left edge in bounds than
             * its right edge. Similarly, we do the y checks in the same
             * order, privileging the top edge over the bottom.
             */
            if (x + guess_width > war.right)
                x = war.right - guess_width;
            if (x < war.left)
                x = war.left;
            if (y + guess_height > war.bottom)
                y = war.bottom - guess_height;
            if (y < war.top)
                y = war.top;
        }

        /* And set the window to the final size and position we've chosen */
        SetWindowPos(wgs->term_hwnd, NULL, x, y, guess_width, guess_height,
                    SWP_NOREDRAW | SWP_NOZORDER);
    }

    /*
     * Set up a caret bitmap, with no content.
     */
    {
        char *bits;
        int size = (wgs->font_width + 15) / 16 * 2 * wgs->font_height;
        bits = snewn(size, char);
        memset(bits, 0, size);
        wgs->caretbm = CreateBitmap(wgs->font_width, wgs->font_height,
                                    1, 1, bits);
        sfree(bits);
    }
    CreateCaret(wgs->term_hwnd, wgs->caretbm,
                wgs->font_width, wgs->font_height);

    /*
     * Initialise the scroll bar.
     */
    {
        SCROLLINFO si;

        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = wgs->term->rows - 1;
        si.nPage = wgs->term->rows;
        si.nPos = 0;
        SetScrollInfo(wgs->term_hwnd, SB_VERT, &si, false);
    }

    /*
     * Prepare the mouse handler.
     */
    wgs->lastact = MA_NOTHING;
    wgs->lastbtn = MBT_NOTHING;
    wgs->dbltime = GetDoubleClickTime();

    /*
     * Set up the session-control options on the system menu.
     */
    {
        HMENU m;
        int j;
        char *str;

        wgs->popup_menus[SYSMENU].menu = GetSystemMenu(wgs->term_hwnd, false);
        wgs->popup_menus[CTXMENU].menu = CreatePopupMenu();

        for (j = 0; j < lenof(wgs->popup_menus); j++) {
            m = wgs->popup_menus[j].menu;
            AppendMenu(m, MF_ENABLED, IDM_COPY, "&Copy");
            AppendMenu(m, MF_ENABLED, IDM_PASTE, "&Paste");
        }

        wgs->savedsess_menu = CreateMenu();
        get_sesslist(&sesslist, true);
        update_savedsess_menu(wgs);

        for (j = 0; j < lenof(wgs->popup_menus); j++) {
            m = wgs->popup_menus[j].menu;

            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
            AppendMenu(m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
            AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT_PTR)wgs->savedsess_menu,
                       "Sa&ved Sessions");
            AppendMenu(m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
            AppendMenu(m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
            AppendMenu(m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, (conf_get_int(wgs->conf, CONF_resize_action)
                           == RESIZE_DISABLED) ? MF_GRAYED : MF_ENABLED,
                       IDM_FULLSCREEN, "&Full Screen");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            if (has_help())
                AppendMenu(m, MF_ENABLED, IDM_HELP, "&Help");
            str = dupprintf("&About %s", appname);
            AppendMenu(m, MF_ENABLED, IDM_ABOUT, str);
            sfree(str);
        }
    }

    if (restricted_acl()) {
        lp_eventlog(&wgs->logpolicy, "Running with restricted process ACL");
    }

    winselgui_set_hwnd(wgs->term_hwnd);
    start_backend(wgs);

    /*
     * Set up the initial input locale.
     */
    set_input_locale(wgs, GetKeyboardLayout(0));

    /*
     * Finally show the window!
     */
    ShowWindow(wgs->term_hwnd, show);
    SetForegroundWindow(wgs->term_hwnd);

    term_set_focus(wgs->term, GetForegroundWindow() == wgs->term_hwnd);
    UpdateWindow(wgs->term_hwnd);

    gui_terminal_ready(wgs->term_hwnd, &wgs->seat, wgs->backend);

/* PuttyDriver #11 - Putty has started. */
#ifdef PuttyDriver
    if (putty_driver == true) {

        if (parent_hwnd > 0) {
            putty_hwnd = wgs->term_hwnd;

            vterm_curs_x = -1;
            vterm_curs_y = -1;
        }
        else {
            vTermInitialise(wgs->term_hwnd);
        }
    }
#endif
/* PuttyDriver */
    while (1) {
        int n;
        DWORD timeout;

        if (toplevel_callback_pending() ||
            PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
            /*
             * If we have anything we'd like to do immediately, set
             * the timeout for MsgWaitForMultipleObjects to zero so
             * that we'll only do a quick check of our handles and
             * then get on with whatever that was.
             *
             * One such option is a pending toplevel callback. The
             * other is a non-empty Windows message queue, which you'd
             * think we could leave to MsgWaitForMultipleObjects to
             * check for us along with all the handles, but in fact we
             * can't because once PeekMessage in one iteration of this
             * loop has removed a message from the queue, the whole
             * queue is considered uninteresting by the next
             * invocation of MWFMO. So we check ourselves whether the
             * message queue is non-empty, and if so, set this timeout
             * to zero to ensure MWFMO doesn't block.
             */
            timeout = 0;
        } else {
            timeout = INFINITE;
            /* The messages seem unreliable; especially if we're being tricky */
            term_set_focus(wgs->term, GetForegroundWindow() == wgs->term_hwnd);
        }

        HandleWaitList *hwl = get_handle_wait_list();

        n = MsgWaitForMultipleObjects(hwl->nhandles, hwl->handles, false,
                                      timeout, QS_ALLINPUT);

        if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)hwl->nhandles)
            handle_wait_activate(hwl, n - WAIT_OBJECT_0);
        handle_wait_list_free(hwl);

        while (sw_PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
/* PuttyDriver #12 - Putty is closing.  */
#ifdef PuttyDriver
                if (putty_driver == true) {

                    if (parent_hwnd > 0) {
                        HWND parent = GetWindow(parent_hwnd, GW_HWNDFIRST);
                        if (parent)
                            SendMessage(parent_hwnd, WM_CLOSE, (WPARAM)putty_hwnd, 0);
                    }
                    if (vTermLog_Stream != NULL) vTermCloseSessionLogs();
                }
#endif
/* PuttyDriver */
                goto finished;         /* two-level break */
            }
            HWND logbox = event_log_window();
            if (!(IsWindow(logbox) && IsDialogMessage(logbox, &msg)))
                sw_DispatchMessage(&msg);

            /*
             * WM_NETEVENT messages seem to jump ahead of others in
             * the message queue. I'm not sure why; the docs for
             * PeekMessage mention that messages are prioritised in
             * some way, but I'm unclear on which priorities go where.
             *
             * Anyway, in practice I observe that WM_NETEVENT seems to
             * jump to the head of the queue, which means that if we
             * were to only process one message every time round this
             * loop, we'd get nothing but NETEVENTs if the server
             * flooded us with data, and stop responding to any other
             * kind of window message. So instead, we keep on round
             * this loop until we've consumed at least one message
             * that _isn't_ a NETEVENT, or run out of messages
             * completely (whichever comes first). And we don't go to
             * run_toplevel_callbacks (which is where the netevents
             * are actually processed, causing fresh NETEVENT messages
             * to appear) until we've done this.
             */
            if (msg.message != WM_NETEVENT)
                break;
        }

        run_toplevel_callbacks();

/* PuttyDriver #13 - Putty is waiting for some user input.  */
#ifdef PuttyDriver
        if (putty_driver == true) {

            char buf[30];

            sprintf(buf, "#~#CUR3%04d %04d %04d %04d#~#", wgs->term->curs.x, wgs->term->curs.y, wgs->term->cols, wgs->term->rows);

            if (parent_hwnd > 0) {

                HWND parent = GetWindow(parent_hwnd, GW_HWNDFIRST);

                if (parent) {

                    if (!(vterm_started == true)) {

                        vterm_started = true;

                        SendMessage(parent_hwnd, WM_APP, (WPARAM)putty_hwnd, 0);
                    }

                    COPYDATASTRUCT cd;

                    cd.dwData = 5;
                    cd.cbData = strlen(buf);
                    cd.lpData = (PVOID)buf;

                    SendMessage(parent_hwnd, WM_COPYDATA, (WPARAM)putty_hwnd, (LPARAM)&cd);
                }
            }
            else {

                vterm_started = true;

                strncpy(vterm_message, buf, strlen(buf));

                if (vTermLog_Execution == true) {
                    vTermWriteToLog("PuTTY WinMain->vTermWaitingForInput - Before", vterm_message, "");
                }

                vTermWaitingForInput(wgs->term->curs.x, wgs->term->curs.y, wgs->term->cols, wgs->term->rows, false);

                if (vTermLog_Execution == true) {
                    vTermWriteToLog("PuTTY WinMain->vTermWaitingForInput - After", vterm_message, "");
                }

            }
        }
#endif
/* PuttyDriver */
    }

  finished:
    cleanup_exit(msg.wParam);          /* this doesn't return... */
    return msg.wParam;                 /* ... but optimiser doesn't know */
}

static void wgs_cleanup(WinGuiSeat *wgs)
{
    deinit_fonts(wgs);
    sfree(wgs->logpal);
    if (wgs->pal)
        DeleteObject(wgs->pal);
    wgs_unlink(wgs);
    sfree(wgs);
}

char *handle_restrict_acl_cmdline_prefix(char *p)
{
    /*
     * Process the &R prefix on a command line, which is equivalent to
     * -restrict-acl but lexically easier to prepend when another
     * instance of ourself automatically constructs a command line.
     *
     * If successful, restricts the process ACL and advances the input
     * pointer past the prefix. Returns the updated pointer (whether
     * it moved or not).
     */
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p == '&' && p[1] == 'R' &&
        (!p[2] || p[2] == '@' || p[2] == '&')) {
        /* &R restrict-acl prefix */
        restrict_process_acl();
        p += 2;
    }
    return p;
}

bool handle_special_sessionname_cmdline(char *p, Conf *conf)
{
    /*
     * Process the special form of command line with an initial @
     * followed by the name of a saved session with _no quoting or
     * escaping_. This is a very convenient means of automated
     * saved-session launching, via IDM_SAVEDSESS or Windows 7 jump
     * lists.
     *
     * If successful, the whole command line has been interpreted in
     * this way, so there's nothing left to parse into other arguments.
     */
    if (*p != '@')
        return false;

    ptrlen sessionname = ptrlen_from_asciz(p + 1);
    while (sessionname.len > 0 &&
           isspace(((unsigned char *)sessionname.ptr)[sessionname.len-1]))
        sessionname.len--;

    char *dup = mkstr(sessionname);
    bool loaded = do_defaults(dup, conf);
    sfree(dup);

    return loaded;
}

bool handle_special_filemapping_cmdline(char *p, Conf *conf)
{
    /*
     * Process the special form of command line with an initial &
     * followed by the hex value of a HANDLE for a file mapping object
     * and the size of the data contained in it, which we must
     * interpret as a serialised Conf.
     *
     * If successful, the whole command line has been interpreted in
     * this way, so there's nothing left to parse into other arguments.
     */

    if (*p != '&')
        return false;

    HANDLE filemap;
    unsigned cpsize;
    if (sscanf(p + 1, "%p:%u", &filemap, &cpsize) != 2)
        return false;

    void *cp = MapViewOfFile(filemap, FILE_MAP_READ, 0, 0, cpsize);
    if (!cp)
        return false;

    BinarySource src[1];
    BinarySource_BARE_INIT(src, cp, cpsize);
    if (!conf_deserialise(conf, src))
        modalfatalbox("Serialised configuration data was invalid");
    UnmapViewOfFile(cp);
    CloseHandle(filemap);
    return true;
}

static void setup_clipboards(Terminal *term, Conf *conf)
{
    assert(term->mouse_select_clipboards[0] == CLIP_LOCAL);

    term->n_mouse_select_clipboards = 1;

    if (conf_get_bool(conf, CONF_mouseautocopy)) {
        term->mouse_select_clipboards[
            term->n_mouse_select_clipboards++] = CLIP_SYSTEM;
    }

    switch (conf_get_int(conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        term->mouse_paste_clipboard = CLIP_LOCAL;
        break;
      case CLIPUI_EXPLICIT:
        term->mouse_paste_clipboard = CLIP_SYSTEM;
        break;
      default:
        term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    while (wgslisthead.next != &wgslisthead) {
        WinGuiSeat *wgs = container_of(
            wgslisthead.next, WinGuiSeat, wgslistnode);
        wgs_cleanup(wgs);
    }
    sk_cleanup();

    random_save_seed();
    shutdown_help();

    /* Clean up COM. */
    CoUninitialize();

/* PuttyDriver #14 - Putty is closing.  */
#ifdef PuttyDriver
    if (putty_driver == true) {

        if (parent_hwnd > 0) {
            HWND parent = GetWindow(parent_hwnd, GW_HWNDFIRST);
            if (parent)
                SendMessage(parent_hwnd, WM_CLOSE, (WPARAM)putty_hwnd, 0);
        }
        if (vTermLog_Stream != NULL) vTermCloseSessionLogs();
    }
#endif
/* PuttyDriver */

    exit(code);
}

/*
 * Refresh the saved-session submenu from `sesslist'.
 */
static void update_savedsess_menu(WinGuiSeat *wgs)
{
    int i;
    while (DeleteMenu(wgs->savedsess_menu, 0, MF_BYPOSITION)) ;
    /* skip sesslist.sessions[0] == Default Settings */
    for (i = 1;
         i < ((sesslist.nsessions <= MENU_SAVED_MAX+1) ? sesslist.nsessions
                                                       : MENU_SAVED_MAX+1);
         i++)
        AppendMenu(wgs->savedsess_menu, MF_ENABLED,
                   IDM_SAVED_MIN + (i-1)*MENU_SAVED_STEP,
                   sesslist.sessions[i]);
    if (sesslist.nsessions <= 1)
        AppendMenu(wgs->savedsess_menu, MF_GRAYED, IDM_SAVED_MIN,
                   "(No sessions)");
}

/*
 * Update the Special Commands submenu.
 */
static void win_seat_update_specials_menu(Seat *seat)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    HMENU new_menu;
    int i, j;

    if (wgs->backend)
        wgs->specials = backend_get_specials(wgs->backend);
    else
        wgs->specials = NULL;

    if (wgs->specials) {
        /* We can't use Windows to provide a stack for submenus, so
         * here's a lame "stack" that will do for now. */
        HMENU saved_menu = NULL;
        int nesting = 1;
        new_menu = CreatePopupMenu();
        for (i = 0; nesting > 0; i++) {
            assert(IDM_SPECIAL_MIN + 0x10 * i < IDM_SPECIAL_MAX);
            switch (wgs->specials[i].code) {
              case SS_SEP:
                AppendMenu(new_menu, MF_SEPARATOR, 0, 0);
                break;
              case SS_SUBMENU:
                assert(nesting < 2);
                nesting++;
                saved_menu = new_menu; /* XXX lame stacking */
                new_menu = CreatePopupMenu();
                AppendMenu(saved_menu, MF_POPUP | MF_ENABLED,
                           (UINT_PTR) new_menu, wgs->specials[i].name);
                break;
              case SS_EXITMENU:
                nesting--;
                if (nesting) {
                    new_menu = saved_menu; /* XXX lame stacking */
                    saved_menu = NULL;
                }
                break;
              default:
                AppendMenu(new_menu, MF_ENABLED, IDM_SPECIAL_MIN + 0x10 * i,
                           wgs->specials[i].name);
                break;
            }
        }
        /* Squirrel the highest special. */
        wgs->n_specials = i - 1;
    } else {
        new_menu = NULL;
        wgs->n_specials = 0;
    }

    for (j = 0; j < lenof(wgs->popup_menus); j++) {
        if (wgs->specials_menu) {
            /* XXX does this free up all submenus? */
            DeleteMenu(wgs->popup_menus[j].menu, (UINT_PTR)wgs->specials_menu,
                       MF_BYCOMMAND);
            DeleteMenu(wgs->popup_menus[j].menu, IDM_SPECIALSEP, MF_BYCOMMAND);
        }
        if (new_menu) {
            InsertMenu(wgs->popup_menus[j].menu, IDM_SHOWLOG,
                       MF_BYCOMMAND | MF_POPUP | MF_ENABLED,
                       (UINT_PTR) new_menu, "S&pecial Command");
            InsertMenu(wgs->popup_menus[j].menu, IDM_SHOWLOG,
                       MF_BYCOMMAND | MF_SEPARATOR, IDM_SPECIALSEP, 0);
        }
    }
    wgs->specials_menu = new_menu;
}

static void update_mouse_pointer(WinGuiSeat *wgs)
{
    LPTSTR curstype = NULL;
    bool force_visible = false;
    static bool forced_visible = false;
    switch (wgs->busy_status) {
      case BUSY_NOT:
        if (wgs->pointer_indicates_raw_mouse)
            curstype = IDC_ARROW;
        else
            curstype = IDC_IBEAM;
        break;
      case BUSY_WAITING:
        curstype = IDC_APPSTARTING; /* this may be an abuse */
        force_visible = true;
        break;
      case BUSY_CPU:
        curstype = IDC_WAIT;
        force_visible = true;
        break;
      default:
        unreachable("Bad busy_status");
    }
    {
        HCURSOR cursor = LoadCursor(NULL, curstype);
        SetClassLongPtr(wgs->term_hwnd, GCLP_HCURSOR, (LONG_PTR)cursor);
        SetCursor(cursor); /* force redraw of cursor at current posn */
    }
    if (force_visible != forced_visible) {
        /* We want some cursor shapes to be visible always.
         * Along with show_mouseptr(), this manages the ShowCursor()
         * counter such that if we switch back to a non-force_visible
         * cursor, the previous visibility state is restored. */
        ShowCursor(force_visible);
        forced_visible = force_visible;
    }
}

static void win_seat_set_busy_status(Seat *seat, BusyStatus status)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    wgs->busy_status = status;
    update_mouse_pointer(wgs);
}

static void wintw_set_raw_mouse_mode(TermWin *tw, bool activate)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    wgs->send_raw_mouse = activate;
}

static void wintw_set_raw_mouse_mode_pointer(TermWin *tw, bool activate)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    wgs->pointer_indicates_raw_mouse = activate;
    update_mouse_pointer(wgs);
}

/*
 * Print a message box and close the connection.
 */
static void win_seat_connection_fatal(Seat *seat, const char *msg)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    char *title = dupprintf("%s Fatal Error", appname);
    show_mouseptr(wgs, true);
    MessageBox(wgs->term_hwnd, msg, title, MB_ICONERROR | MB_OK);
    sfree(title);

    if (conf_get_int(wgs->conf, CONF_close_on_exit) == FORCE_ON)
        PostQuitMessage(1);
    else {
        queue_toplevel_callback(close_session, wgs);
    }
}

/*
 * Print a message box and don't close the connection.
 */
static void win_seat_nonfatal(Seat *seat, const char *msg)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    char *title = dupprintf("%s Error", appname);
    show_mouseptr(wgs, true);
    MessageBox(wgs->term_hwnd, msg, title, MB_ICONERROR | MB_OK);
    sfree(title);
}

static HWND find_window_for_msgbox(void)
{
    if (wgslisthead.next != &wgslisthead) {
        WinGuiSeat *wgs = container_of(
            wgslisthead.next, WinGuiSeat, wgslistnode);
        return wgs->term_hwnd;
    }
    return NULL;
}

/*
 * Report an error at the command-line parsing stage.
 */
void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    title = dupprintf("%s Command Line Error", appname);
    MessageBox(find_window_for_msgbox(), message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    exit(1);
}

static inline rgb rgb_from_colorref(COLORREF cr)
{
    rgb toret;
    toret.r = GetRValue(cr);
    toret.g = GetGValue(cr);
    toret.b = GetBValue(cr);
    return toret;
}

static void wintw_palette_get_overrides(TermWin *tw, Terminal *term)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (conf_get_bool(wgs->conf, CONF_system_colour)) {
        rgb rgb;

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOWTEXT));
        term_palette_override(term, OSC4_COLOUR_fg, rgb);
        term_palette_override(term, OSC4_COLOUR_fg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOW));
        term_palette_override(term, OSC4_COLOUR_bg, rgb);
        term_palette_override(term, OSC4_COLOUR_bg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHTTEXT));
        term_palette_override(term, OSC4_COLOUR_cursor_fg, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHT));
        term_palette_override(term, OSC4_COLOUR_cursor_bg, rgb);
    }
}

/*
 * This is a wrapper to ExtTextOut() to force Windows to display
 * the precise glyphs we give it. Otherwise it would do its own
 * bidi and Arabic shaping, and we would end up uncertain which
 * characters it had put where.
 */
static void exact_textout(HDC hdc, int x, int y, CONST RECT *lprc,
                          unsigned short *lpString, UINT cbCount,
                          CONST INT *lpDx, bool opaque)
{
#if HAVE_GCP_RESULTSW
    GCP_RESULTSW gcpr;
#else
    /*
     * If building against old enough headers that the GCP_RESULTSW
     * type isn't available, we can make do with GCP_RESULTS proper:
     * the differences aren't important to us (the only variable-width
     * string parameter is one we don't use anyway).
     */
    GCP_RESULTS gcpr;
#endif
    char *buffer = snewn(cbCount*2+2, char);
    char *classbuffer = snewn(cbCount, char);
    memset(&gcpr, 0, sizeof(gcpr));
    memset(buffer, 0, cbCount*2+2);
    memset(classbuffer, GCPCLASS_NEUTRAL, cbCount);

    gcpr.lStructSize = sizeof(gcpr);
    gcpr.lpGlyphs = (void *)buffer;
    gcpr.lpClass = (void *)classbuffer;
    gcpr.nGlyphs = cbCount;
    GetCharacterPlacementW(hdc, lpString, cbCount, 0, &gcpr,
                           FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);

    ExtTextOut(hdc, x, y,
               ETO_GLYPH_INDEX | ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
               lprc, buffer, cbCount, lpDx);
}

/*
 * The exact_textout() wrapper, unfortunately, destroys the useful
 * Windows `font linking' behaviour: automatic handling of Unicode
 * code points not supported in this font by falling back to a font
 * which does contain them. Therefore, we adopt a multi-layered
 * approach: for any potentially-bidi text, we use exact_textout(),
 * and for everything else we use a simple ExtTextOut as we did
 * before exact_textout() was introduced.
 */
static void general_textout(
    WinGuiSeat *wgs, HDC hdc, int x, int y, CONST RECT *lprc,
    unsigned short *lpString, UINT cbCount, CONST INT *lpDx, bool opaque)
{
    int i, j, xp, xn;
    int bkmode = 0;
    bool got_bkmode = false;

    xp = xn = x;

    for (i = 0; i < (int)cbCount ;) {
        bool rtl = is_rtl(lpString[i]);

        xn += lpDx[i];

        for (j = i+1; j < (int)cbCount; j++) {
            if (rtl != is_rtl(lpString[j]))
                break;
            xn += lpDx[j];
        }

        /*
         * Now [i,j) indicates a maximal substring of lpString
         * which should be displayed using the same textout
         * function.
         */
        if (rtl) {
            exact_textout(hdc, xp, y, lprc, lpString+i, j-i,
                          wgs->font_varpitch ? NULL : lpDx+i, opaque);
        } else {
            ExtTextOutW(hdc, xp, y, ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                        lprc, lpString+i, j-i,
                        wgs->font_varpitch ? NULL : lpDx+i);
        }

        i = j;
        xp = xn;

        bkmode = GetBkMode(hdc);
        got_bkmode = true;
        SetBkMode(hdc, TRANSPARENT);
        opaque = false;
    }

    if (got_bkmode)
        SetBkMode(hdc, bkmode);
}

static int get_font_width(WinGuiSeat *wgs, HDC hdc, const TEXTMETRIC *tm)
{
    int ret;
    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm->tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        ret = tm->tmAveCharWidth;
    } else {
#define FIRST '0'
#define LAST '9'
        ABCFLOAT widths[LAST-FIRST + 1];
        int j;

        wgs->font_varpitch = true;
        wgs->font_dualwidth = true;
        if (GetCharABCWidthsFloat(hdc, FIRST, LAST, widths)) {
            ret = 0;
            for (j = 0; j < lenof(widths); j++) {
                int width = (int)(0.5 + widths[j].abcfA +
                                  widths[j].abcfB + widths[j].abcfC);
                if (ret < width)
                    ret = width;
            }
        } else {
            ret = tm->tmMaxCharWidth;
        }
#undef FIRST
#undef LAST
    }
    return ret;
}

static void init_dpi_info(WinGuiSeat *wgs)
{
    if (wgs->dpi_info.cur_dpi.x == 0 || wgs->dpi_info.cur_dpi.y == 0) {
        if (p_GetDpiForMonitor && p_MonitorFromWindow) {
            UINT dpiX, dpiY;
            HMONITOR currentMonitor = p_MonitorFromWindow(
                wgs->term_hwnd, MONITOR_DEFAULTTOPRIMARY);
            if (p_GetDpiForMonitor(currentMonitor, MDT_EFFECTIVE_DPI,
                                   &dpiX, &dpiY) == S_OK) {
                wgs->dpi_info.cur_dpi.x = (int)dpiX;
                wgs->dpi_info.cur_dpi.y = (int)dpiY;
            }
        }

        /* Fall back to system DPI */
        if (wgs->dpi_info.cur_dpi.x == 0 || wgs->dpi_info.cur_dpi.y == 0) {
            HDC hdc = GetDC(wgs->term_hwnd);
            wgs->dpi_info.cur_dpi.x = GetDeviceCaps(hdc, LOGPIXELSX);
            wgs->dpi_info.cur_dpi.y = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(wgs->term_hwnd, hdc);
        }
    }
}

/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (potentially) twenty-one fonts are done
 * if/when they are needed.
 *
 * We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 *
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 *
 * - find a trust sigil icon that will look OK with the chosen font.
 */
static void init_fonts(WinGuiSeat *wgs, int pick_width, int pick_height)
{
    TEXTMETRIC tm;
    OUTLINETEXTMETRIC otm;
    CPINFO cpinfo;
    FontSpec *font;
    int fontsize[3];
    int i;
    int quality;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
        wgs->fonts[i] = NULL;

    wgs->bold_font_mode =
        conf_get_int(wgs->conf, CONF_bold_style) & BOLD_STYLE_FONT ?
        BOLD_FONT : BOLD_NONE;
    wgs->bold_colours =
        conf_get_int(wgs->conf, CONF_bold_style) & BOLD_STYLE_COLOUR ?
        true : false;
    wgs->und_mode = UND_FONT;

    font = conf_get_fontspec(wgs->conf, CONF_font);
    if (font->isbold) {
        fw_dontcare = FW_BOLD;
        fw_bold = FW_HEAVY;
    } else {
        fw_dontcare = FW_DONTCARE;
        fw_bold = FW_BOLD;
    }

    hdc = GetDC(wgs->term_hwnd);

    if (pick_height)
        wgs->font_height = pick_height;
    else {
        wgs->font_height = font->height;
        if (wgs->font_height > 0) {
            wgs->font_height = -MulDiv(
                wgs->font_height, wgs->dpi_info.cur_dpi.y, 72);
        }
    }
    wgs->font_width = pick_width;

    quality = conf_get_int(wgs->conf, CONF_font_quality);
#define f(i,c,w,u)                                                      \
    wgs->fonts[i] = CreateFont(                                         \
        wgs->font_height, wgs->font_width, 0, 0, w, false, u, false, c, \
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality), \
        FIXED_PITCH | FF_DONTCARE, font->name)

    f(FONT_NORMAL, font->charset, fw_dontcare, false);

    SelectObject(hdc, wgs->fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);
    if (GetOutlineTextMetrics(hdc, sizeof(otm), &otm))
        wgs->font_strikethrough_y = tm.tmAscent - otm.otmsStrikeoutPosition;
    else
        wgs->font_strikethrough_y = tm.tmAscent - (tm.tmAscent * 3 / 8);

    GetObject(wgs->fonts[FONT_NORMAL], sizeof(LOGFONT), &wgs->lfont);

    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        wgs->font_varpitch = false;
        wgs->font_dualwidth = (tm.tmAveCharWidth != tm.tmMaxCharWidth);
    } else {
        wgs->font_varpitch = true;
        wgs->font_dualwidth = true;
    }
    if (pick_width == 0 || pick_height == 0) {
        wgs->font_height = tm.tmHeight;
        wgs->font_width = get_font_width(wgs, hdc, &tm);
    }

    {
        CHARSETINFO info;
        DWORD cset = tm.tmCharSet;
        memset(&info, 0xFF, sizeof(info));

        /* !!! Yes the next line is right */
        if (cset == OEM_CHARSET)
            wgs->ucsdata.font_codepage = GetOEMCP();
        else if (TranslateCharsetInfo ((DWORD *)(ULONG_PTR)cset,
                                       &info, TCI_SRCCHARSET))
            wgs->ucsdata.font_codepage = info.ciACP;
        else
            wgs->ucsdata.font_codepage = -1;

        GetCPInfo(wgs->ucsdata.font_codepage, &cpinfo);
        wgs->ucsdata.dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }

    f(FONT_UNDERLINE, font->charset, fw_dontcare, true);

    /*
     * Some fonts, e.g. 9-pt Courier, draw their underlines
     * outside their character cell. We successfully prevent
     * screen corruption by clipping the text output, but then
     * we lose the underline completely. Here we try to work
     * out whether this is such a font, and if it is, we set a
     * flag that causes underlines to be drawn by hand.
     *
     * Having tried other more sophisticated approaches (such
     * as examining the TEXTMETRIC structure or requesting the
     * height of a string), I think we'll do this the brute
     * force way: we create a small bitmap, draw an underlined
     * space on it, and test to see whether any pixels are
     * foreground-coloured. (Since we expect the underline to
     * go all the way across the character cell, we only search
     * down a single column of the bitmap, half way across.)
     */
    {
        HDC und_dc;
        HBITMAP und_bm, und_oldbm;
        int i;
        bool gotit;
        COLORREF c;

        und_dc = CreateCompatibleDC(hdc);
        und_bm = CreateCompatibleBitmap(
            hdc, wgs->font_width, wgs->font_height);
        und_oldbm = SelectObject(und_dc, und_bm);
        SelectObject(und_dc, wgs->fonts[FONT_UNDERLINE]);
        SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        SetTextColor(und_dc, RGB(255, 255, 255));
        SetBkColor(und_dc, RGB(0, 0, 0));
        SetBkMode(und_dc, OPAQUE);
        ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
        gotit = false;
        for (i = 0; i < wgs->font_height; i++) {
            c = GetPixel(und_dc, wgs->font_width / 2, i);
            if (c != RGB(0, 0, 0))
                gotit = true;
        }
        SelectObject(und_dc, und_oldbm);
        DeleteObject(und_bm);
        DeleteDC(und_dc);
        if (!gotit) {
            wgs->und_mode = UND_LINE;
            DeleteObject(wgs->fonts[FONT_UNDERLINE]);
            wgs->fonts[FONT_UNDERLINE] = 0;
        }
    }

    if (wgs->bold_font_mode == BOLD_FONT) {
        f(FONT_BOLD, font->charset, fw_bold, false);
    }
#undef f

    wgs->descent = tm.tmAscent + 1;
    if (wgs->descent >= wgs->font_height)
        wgs->descent = wgs->font_height - 1;

    for (i = 0; i < 3; i++) {
        if (wgs->fonts[i]) {
            if (SelectObject(hdc, wgs->fonts[i]) && GetTextMetrics(hdc, &tm))
                fontsize[i] = (get_font_width(wgs, hdc, &tm) +
                               256 * tm.tmHeight);
            else
                fontsize[i] = -i;
        } else
            fontsize[i] = -i;
    }

    ReleaseDC(wgs->term_hwnd, hdc);

    if (trust_icon != INVALID_HANDLE_VALUE) {
        DestroyIcon(trust_icon);
    }
    trust_icon = LoadImage(hinst, MAKEINTRESOURCE(IDI_MAINICON),
                           IMAGE_ICON, wgs->font_width*2, wgs->font_height,
                           LR_DEFAULTCOLOR);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
        wgs->und_mode = UND_LINE;
        DeleteObject(wgs->fonts[FONT_UNDERLINE]);
        wgs->fonts[FONT_UNDERLINE] = 0;
    }

    if (wgs->bold_font_mode == BOLD_FONT &&
        fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
        wgs->bold_font_mode = BOLD_SHADOW;
        DeleteObject(wgs->fonts[FONT_BOLD]);
        wgs->fonts[FONT_BOLD] = 0;
    }
    wgs->fontflag[0] = true;
    wgs->fontflag[1] = true;
    wgs->fontflag[2] = true;

    init_ucs(wgs->conf, &wgs->ucsdata);
}

static void another_font(WinGuiSeat *wgs, int fontno)
{
    int basefont;
    int fw_dontcare, fw_bold, quality;
    int c, w, x;
    bool u;
    char *s;
    FontSpec *font;

    if (fontno < 0 || fontno >= FONT_MAXNO || wgs->fontflag[fontno])
        return;

    basefont = (fontno & ~(FONT_BOLDUND));
    if (basefont != fontno && !wgs->fontflag[basefont])
        another_font(wgs, basefont);

    font = conf_get_fontspec(wgs->conf, CONF_font);

    if (font->isbold) {
        fw_dontcare = FW_BOLD;
        fw_bold = FW_HEAVY;
    } else {
        fw_dontcare = FW_DONTCARE;
        fw_bold = FW_BOLD;
    }

    c = font->charset;
    w = fw_dontcare;
    u = false;
    s = font->name;
    x = wgs->font_width;

    if (fontno & FONT_WIDE)
        x *= 2;
    if (fontno & FONT_NARROW)
        x = (x+1)/2;
    if (fontno & FONT_OEM)
        c = OEM_CHARSET;
    if (fontno & FONT_BOLD)
        w = fw_bold;
    if (fontno & FONT_UNDERLINE)
        u = true;

    quality = conf_get_int(wgs->conf, CONF_font_quality);

    wgs->fonts[fontno] =
        CreateFont(wgs->font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w,
                   false, u, false, c, OUT_DEFAULT_PRECIS,
                   CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality),
                   DEFAULT_PITCH | FF_DONTCARE, s);

    wgs->fontflag[fontno] = true;
}

static void deinit_fonts(WinGuiSeat *wgs)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
        if (wgs->fonts[i])
            DeleteObject(wgs->fonts[i]);
        wgs->fonts[i] = 0;
        wgs->fontflag[i] = false;
    }

    if (trust_icon != INVALID_HANDLE_VALUE) {
        DestroyIcon(trust_icon);
    }
    trust_icon = INVALID_HANDLE_VALUE;
}

static void wintw_request_resize(TermWin *tw, int w, int h)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    const struct BackendVtable *vt;
    int width, height;
    int resize_action = conf_get_int(wgs->conf, CONF_resize_action);
    bool deny_resize = false;

    /* Suppress server-originated resizing attempts if local resizing
     * is disabled entirely, or if it's supposed to change
     * rows/columns but the window is maximised. */
    if (resize_action == RESIZE_DISABLED
        || (resize_action == RESIZE_TERM && IsZoomed(wgs->term_hwnd))) {
        deny_resize = true;
    }

    vt = backend_vt_from_proto(be_default_protocol);
    if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
        deny_resize = true;
    if (h == wgs->term->rows && w == wgs->term->cols) deny_resize = true;

    /* We still need to acknowledge a suppressed resize attempt. */
    if (deny_resize) {
        term_resize_request_completed(wgs->term);
        return;
    }

    /* Sanity checks ... */
    {
        RECT ss;
        if (get_fullscreen_rect(wgs, &ss)) {
            /* Make sure the values aren't too big */
            width = (ss.right - ss.left - wgs->extra_width) / 4;
            height = (ss.bottom - ss.top - wgs->extra_height) / 6;

            if (w > width || h > height) {
                term_resize_request_completed(wgs->term);
                return;
            }
            if (w < 15)
                w = 15;
            if (h < 1)
                h = 1;
        }
    }

    if (resize_action != RESIZE_FONT && !IsZoomed(wgs->term_hwnd)) {
        width = wgs->extra_width + wgs->font_width * w;
        height = wgs->extra_height + wgs->font_height * h;

        SetWindowPos(wgs->term_hwnd, NULL, 0, 0, width, height,
                     SWP_NOACTIVATE | SWP_NOCOPYBITS |
                     SWP_NOMOVE | SWP_NOZORDER);
    } else {
        /*
         * If we're resizing by changing the font, we must tell the
         * terminal the new size immediately, so that reset_window
         * will know what to do.
         */
        term_size(wgs->term, h, w, conf_get_int(wgs->conf, CONF_savelines));
        reset_window(wgs, 0);
    }

    term_resize_request_completed(wgs->term);
    InvalidateRect(wgs->term_hwnd, NULL, true);
}

static void recompute_window_offset(WinGuiSeat *wgs)
{
    RECT cr;
    GetClientRect(wgs->term_hwnd, &cr);

    int win_width  = cr.right - cr.left;
    int win_height = cr.bottom - cr.top;

    int new_offset_width = (win_width-wgs->font_width*wgs->term->cols)/2;
    int new_offset_height = (win_height-wgs->font_height*wgs->term->rows)/2;

    if (wgs->offset_width != new_offset_width ||
        wgs->offset_height != new_offset_height) {
        wgs->offset_width = new_offset_width;
        wgs->offset_height = new_offset_height;
        InvalidateRect(wgs->term_hwnd, NULL, true);
    }
}

static void reset_window(WinGuiSeat *wgs, int reinit)
{
    /*
     * This function decides how to resize or redraw when the
     * user changes something.
     *
     * This function doesn't like to change the terminal size but if the
     * font size is locked that may be it's only soluion.
     */
    int win_width, win_height, resize_action, window_border;
    RECT cr, wr;

    /* Current window sizes ... */
    GetWindowRect(wgs->term_hwnd, &wr);
    GetClientRect(wgs->term_hwnd, &cr);

    win_width  = cr.right - cr.left;
    win_height = cr.bottom - cr.top;

    resize_action = conf_get_int(wgs->conf, CONF_resize_action);
    window_border = conf_get_int(wgs->conf, CONF_window_border);

    if (resize_action == RESIZE_DISABLED)
        reinit = 2;

    /* Are we being forced to reload the fonts ? */
    if (reinit>1) {
        deinit_fonts(wgs);
        init_fonts(wgs, 0, 0);
    }

    /* Oh, looks like we're minimised */
    if (win_width == 0 || win_height == 0)
        return;

    /* Is the window out of position ? */
    if (!reinit) {
        recompute_window_offset(wgs);
    }

    if (IsZoomed(wgs->term_hwnd)) {
        /* We're fullscreen, this means we must not change the size of
         * the window so it's the font size or the terminal itself.
         */

        wgs->extra_width = wr.right - wr.left - cr.right + cr.left;
        wgs->extra_height = wr.bottom - wr.top - cr.bottom + cr.top;

        if (resize_action != RESIZE_TERM) {
            if (wgs->font_width != win_width/wgs->term->cols ||
                wgs->font_height != win_height/wgs->term->rows) {
                int fw = (win_width - 2*window_border) / wgs->term->cols;
                int fh = (win_height - 2*window_border) / wgs->term->rows;
                /* In case that subtraction made the font size go
                 * negative in an edge case, bound it below by 1 */
                if (fw < 1) fw = 1;
                if (fh < 1) fh = 1;
                deinit_fonts(wgs);
                init_fonts(wgs, fw, fh);
                wgs->offset_width =
                    (win_width - wgs->font_width*wgs->term->cols) / 2;
                wgs->offset_height =
                    (win_height - wgs->font_height*wgs->term->rows) / 2;
                InvalidateRect(wgs->term_hwnd, NULL, true);
            }
        } else {
            if (wgs->font_width * wgs->term->cols != win_width ||
                wgs->font_height * wgs->term->rows != win_height) {
                /* Our only choice at this point is to change the
                 * size of the terminal; Oh well.
                 */
                term_size(wgs->term,
                          (win_height - 2*window_border) / wgs->font_height,
                          (win_width - 2*window_border) / wgs->font_width,
                          conf_get_int(wgs->conf, CONF_savelines));
                wgs->offset_width =
                    (win_width - window_border - wgs->font_width*wgs->term->cols) / 2;
                wgs->offset_height =
                    (win_height - window_border - wgs->font_height*wgs->term->rows) / 2;
                InvalidateRect(wgs->term_hwnd, NULL, true);
            }
        }
        return;
    }

    /* Resize window after DPI change */
    if (reinit == 3 && p_GetSystemMetricsForDpi && p_AdjustWindowRectExForDpi) {
        RECT rect;
        rect.left = rect.top = 0;
        rect.right = (wgs->font_width * wgs->term->cols);
        if (conf_get_bool(wgs->conf, CONF_scrollbar))
            rect.right += p_GetSystemMetricsForDpi(SM_CXVSCROLL,
                                                   wgs->dpi_info.cur_dpi.x);
        rect.bottom = (wgs->font_height * wgs->term->rows);
        p_AdjustWindowRectExForDpi(
            &rect, GetWindowLongPtr(wgs->term_hwnd, GWL_STYLE),
            FALSE, GetWindowLongPtr(wgs->term_hwnd, GWL_EXSTYLE),
            wgs->dpi_info.cur_dpi.x);
        rect.right += (window_border * 2);
        rect.bottom += (window_border * 2);
        OffsetRect(&wgs->dpi_info.new_wnd_rect,
                   ((wgs->dpi_info.new_wnd_rect.right -
                     wgs->dpi_info.new_wnd_rect.left) -
                    (rect.right - rect.left)) / 2,
                   ((wgs->dpi_info.new_wnd_rect.bottom -
                     wgs->dpi_info.new_wnd_rect.top) -
                    (rect.bottom - rect.top)) / 2);
        SetWindowPos(wgs->term_hwnd, NULL,
                     wgs->dpi_info.new_wnd_rect.left,
                     wgs->dpi_info.new_wnd_rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER);

        InvalidateRect(wgs->term_hwnd, NULL, true);
        return;
    }

    /* Hmm, a force re-init means we should ignore the current window
     * so we resize to the default font size.
     */
    if (reinit>0) {
        wgs->offset_width = wgs->offset_height = window_border;
        wgs->extra_width =
            wr.right - wr.left - cr.right + cr.left + wgs->offset_width*2;
        wgs->extra_height =
            wr.bottom - wr.top - cr.bottom + cr.top + wgs->offset_height*2;

        if (win_width != (wgs->font_width*wgs->term->cols +
                          wgs->offset_width*2) ||
            win_height != (wgs->font_height*wgs->term->rows +
                           wgs->offset_height*2)) {

            /* If this is too large windows will resize it to the maximum
             * allowed window size, we will then be back in here and resize
             * the font or terminal to fit.
             */
            SetWindowPos(wgs->term_hwnd, NULL, 0, 0,
                         wgs->font_width*wgs->term->cols + wgs->extra_width,
                         wgs->font_height*wgs->term->rows + wgs->extra_height,
                         SWP_NOMOVE | SWP_NOZORDER);
        }

        InvalidateRect(wgs->term_hwnd, NULL, true);
        return;
    }

    /* Okay the user doesn't want us to change the font so we try the
     * window. But that may be too big for the screen which forces us
     * to change the terminal.
     */
    if ((resize_action == RESIZE_TERM && reinit<=0) ||
        (resize_action == RESIZE_EITHER && reinit<0) ||
        reinit>0) {
        wgs->offset_width = wgs->offset_height = window_border;
        wgs->extra_width =
            wr.right - wr.left - cr.right + cr.left + wgs->offset_width*2;
        wgs->extra_height =
            wr.bottom - wr.top - cr.bottom + cr.top + wgs->offset_height*2;

        if (win_width != (wgs->font_width*wgs->term->cols +
                          wgs->offset_width*2) ||
            win_height != (wgs->font_height*wgs->term->rows +
                           wgs->offset_height*2)) {

            RECT ss;
            int width, height;

            get_fullscreen_rect(wgs, &ss);

            width = (ss.right - ss.left - wgs->extra_width) / wgs->font_width;
            height = (ss.bottom - ss.top - wgs->extra_height)/wgs->font_height;

            /* Grrr too big */
            if ( wgs->term->rows > height || wgs->term->cols > width ) {
                if (resize_action == RESIZE_EITHER) {
                    /* Make the font the biggest we can */
                    if (wgs->term->cols > width)
                        wgs->font_width =
                            (ss.right - ss.left - wgs->extra_width) /
                            wgs->term->cols;
                    if (wgs->term->rows > height)
                        wgs->font_height =
                            (ss.bottom - ss.top - wgs->extra_height) /
                            wgs->term->rows;

                    deinit_fonts(wgs);
                    init_fonts(wgs, wgs->font_width, wgs->font_height);

                    width = (ss.right - ss.left - wgs->extra_width) /
                        wgs->font_width;
                    height = (ss.bottom - ss.top - wgs->extra_height) /
                        wgs->font_height;
                } else {
                    if ( height > wgs->term->rows ) height = wgs->term->rows;
                    if ( width > wgs->term->cols )  width = wgs->term->cols;
                    term_size(wgs->term, height, width,
                              conf_get_int(wgs->conf, CONF_savelines));
                }
            }

            SetWindowPos(wgs->term_hwnd, NULL, 0, 0,
                         wgs->font_width*wgs->term->cols + wgs->extra_width,
                         wgs->font_height*wgs->term->rows + wgs->extra_height,
                         SWP_NOMOVE | SWP_NOZORDER);

            InvalidateRect(wgs->term_hwnd, NULL, true);
        }
        return;
    }

    /* We're allowed to or must change the font but do we want to ?  */

    if (wgs->font_width != (win_width-window_border*2)/wgs->term->cols ||
        wgs->font_height != (win_height-window_border*2)/wgs->term->rows) {

        deinit_fonts(wgs);
        init_fonts(wgs, (win_width-window_border*2)/wgs->term->cols,
                   (win_height-window_border*2)/wgs->term->rows);
        wgs->offset_width = (win_width-wgs->font_width*wgs->term->cols)/2;
        wgs->offset_height = (win_height-wgs->font_height*wgs->term->rows)/2;

        wgs->extra_width =
            wr.right - wr.left - cr.right + cr.left + wgs->offset_width*2;
        wgs->extra_height =
            wr.bottom - wr.top - cr.bottom + cr.top + wgs->offset_height*2;

        InvalidateRect(wgs->term_hwnd, NULL, true);
    }
}

static void set_input_locale(WinGuiSeat *wgs, HKL kl)
{
    char lbuf[20];

    GetLocaleInfo(LOWORD(kl), LOCALE_IDEFAULTANSICODEPAGE,
                  lbuf, sizeof(lbuf));

    wgs->kbd_codepage = atoi(lbuf);
}

static void click(WinGuiSeat *wgs, Mouse_Button b, int x, int y,
                  bool shift, bool ctrl, bool alt)
{
    int thistime = GetMessageTime();

    if (wgs->send_raw_mouse &&
        !(shift && conf_get_bool(wgs->conf, CONF_mouse_override))) {
        wgs->lastbtn = MBT_NOTHING;
        term_mouse(wgs->term, b, translate_button(wgs, b), MA_CLICK,
                   x, y, shift, ctrl, alt);
        return;
    }

    if (wgs->lastbtn == b && thistime - wgs->lasttime < wgs->dbltime) {
        wgs->lastact = (wgs->lastact == MA_CLICK ? MA_2CLK :
                   wgs->lastact == MA_2CLK ? MA_3CLK :
                   wgs->lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
        wgs->lastbtn = b;
        wgs->lastact = MA_CLICK;
    }
    if (wgs->lastact != MA_NOTHING)
        term_mouse(wgs->term, b, translate_button(wgs, b), wgs->lastact,
                   x, y, shift, ctrl, alt);
    wgs->lasttime = thistime;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
static Mouse_Button translate_button(WinGuiSeat *wgs, Mouse_Button button)
{
    if (button == MBT_LEFT)
        return MBT_SELECT;
    if (button == MBT_MIDDLE)
        return conf_get_int(wgs->conf, CONF_mouse_is_xterm) == MOUSE_XTERM ?
            MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
        return conf_get_int(wgs->conf, CONF_mouse_is_xterm) == MOUSE_XTERM ?
            MBT_EXTEND : MBT_PASTE;
    return 0;                          /* shouldn't happen */
}

static void show_mouseptr(WinGuiSeat *wgs, bool show)
{
    /* NB that the counter in ShowCursor() is also frobbed by
     * update_mouse_pointer() */
    static bool cursor_visible = true;
    if (wgs) {
        if (!conf_get_bool(wgs->conf, CONF_hide_mouseptr))
            show = true; /* hiding mouse pointer disabled in Conf */
    } else {
        /*
         * You can pass wgs==NULL if you want to _show_ the pointer
         * rather than hiding it, because that's never disallowed.
         */
        assert(show);
    }
    if (cursor_visible && !show)
        ShowCursor(false);
    else if (!cursor_visible && show)
        ShowCursor(true);
    cursor_visible = show;
}

static bool is_alt_pressed(void)
{
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
        return false;
    if (keystate[VK_MENU] & 0x80)
        return true;
    if (keystate[VK_RMENU] & 0x80)
        return true;
    return false;
}

static void exit_callback(void *vctx)
{
    WinGuiSeat *wgs = (WinGuiSeat *)vctx;
    int exitcode, close_on_exit;

    if (!wgs->session_closed &&
        (exitcode = backend_exitcode(wgs->backend)) >= 0) {
        close_on_exit = conf_get_int(wgs->conf, CONF_close_on_exit);
        /* Abnormal exits will already have set session_closed and taken
         * appropriate action. */
        if (close_on_exit == FORCE_ON ||
            (close_on_exit == AUTO && exitcode != INT_MAX)) {
            PostQuitMessage(0);
        } else {
            queue_toplevel_callback(close_session, wgs);
            wgs->session_closed = true;
            /* exitcode == INT_MAX indicates that the connection was closed
             * by a fatal error, so an error box will be coming our way and
             * we should not generate this informational one. */
            if (exitcode != INT_MAX) {
                show_mouseptr(wgs, true);
                MessageBox(wgs->term_hwnd, "Connection closed by remote host",
                           appname, MB_OK | MB_ICONINFORMATION);
            }
        }
    }
}

static void win_seat_notify_remote_exit(Seat *seat)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    queue_toplevel_callback(exit_callback, wgs);
}

static void conf_cache_data(WinGuiSeat *wgs)
{
    /* Cache some items from conf to speed lookups in very hot code */
    wgs->cursor_type = conf_get_int(wgs->conf, CONF_cursor_type);
    wgs->vtmode = conf_get_int(wgs->conf, CONF_vtmode);
}

static const int clips_system[] = { CLIP_SYSTEM };

static HDC make_hdc(WinGuiSeat *wgs)
{
    HDC hdc;

    if (!wgs->term_hwnd)
        return NULL;

    hdc = GetDC(wgs->term_hwnd);
    if (!hdc)
        return NULL;

    SelectPalette(hdc, wgs->pal, false);
    return hdc;
}

static void free_hdc(WinGuiSeat *wgs, HDC hdc)
{
    assert(wgs->term_hwnd);
    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);
    ReleaseDC(wgs->term_hwnd, hdc);
}

static void wm_size_resize_term(WinGuiSeat *wgs, LPARAM lParam)
{
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    int border_size = conf_get_int(wgs->conf, CONF_window_border);

    int w = (width - border_size*2) / wgs->font_width;
    int h = (height - border_size*2) / wgs->font_height;

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (wgs->resizing) {
        /*
         * If we're in the middle of an interactive resize, we don't
         * call term_size. This means that, firstly, the user can drag
         * the size back and forth indecisively without wiping out any
         * actual terminal contents, and secondly, the Terminal
         * doesn't call back->size in turn for each increment of the
         * resizing drag, so we don't spam the server with huge
         * numbers of resize events.
         */
        wgs->need_backend_resize = true;
    } else {
        term_size(wgs->term, h, w,
                  conf_get_int(wgs->conf, CONF_savelines));
    }
    conf_set_int(wgs->conf, CONF_height, h);
    conf_set_int(wgs->conf, CONF_width, w);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                                WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    int resize_action;
    WinGuiSeat *wgs = (WinGuiSeat *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (message) {
      case WM_CREATE:
        break;
      case WM_CLOSE: {
        char *title, *msg, *additional = NULL;
        show_mouseptr(wgs, true);
        title = dupprintf("%s Exit Confirmation", appname);
/* PuttyDriver #15 - Instruction received to close Putty.  */
#ifdef PuttyDriver
        if (putty_driver == true) {

            msg = NULL;

            if (parent_hwnd > 0) {

                HWND parent = GetWindow(parent_hwnd, GW_HWNDFIRST);

                if (parent)
                    SendMessage(parent_hwnd, WM_CLOSE, (WPARAM)putty_hwnd, 0);

                DestroyWindow(hwnd);
            }
            if (vTermLog_Stream != NULL) vTermCloseSessionLogs();
        }
        else {
#endif
/* PuttyDriver */
            if (wgs->backend && wgs->backend->vt->close_warn_text) {
                additional = wgs->backend->vt->close_warn_text(wgs->backend);
            }
            msg = dupprintf("Are you sure you want to close this session?%s%s",
                            additional ? "\n" : "",
                            additional ? additional : "");
            if (wgs->session_closed ||
                !conf_get_bool(wgs->conf, CONF_warn_on_close) ||
                MessageBox(hwnd, msg, title,
                           MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1)
                == IDOK)
                DestroyWindow(hwnd);
/* PuttyDriver #16 - Instruction received to close Putty.  */
#ifdef PuttyDriver
      }
#endif
        sfree(title);
        sfree(msg);
        sfree(additional);
        return 0;
    }
      case WM_DESTROY:
        show_mouseptr(wgs, true);
        PostQuitMessage(0);
        return 0;
      case WM_INITMENUPOPUP:
        if ((HMENU)wParam == wgs->savedsess_menu) {
            /* About to pop up Saved Sessions sub-menu.
             * Refresh the session list. */
            get_sesslist(&sesslist, false); /* free */
            get_sesslist(&sesslist, true);
            update_savedsess_menu(wgs);
            return 0;
        }
        break;
      case WM_COMMAND:
      case WM_SYSCOMMAND:
        switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
          case SC_VSCROLL:
          case SC_HSCROLL:
            if (message == WM_SYSCOMMAND) {
                /* As per the long comment in WM_VSCROLL handler: give
                 * this message the default handling, which starts a
                 * subsidiary message loop, but set a flag so that
                 * when we're re-entered from that loop, scroll events
                 * within an interactive scrollbar-drag can be handled
                 * differently. */
                wgs->in_scrollbar_loop = true;
                LRESULT result = sw_DefWindowProc(
                    hwnd, message, wParam, lParam);
                wgs->in_scrollbar_loop = false;
                return result;
            }
            break;
          case IDM_SHOWLOG:
            showeventlog(hwnd);
            break;
          case IDM_NEWSESS:
          case IDM_DUPSESS:
          case IDM_SAVEDSESS: {
            char b[2048];
            char *cl;
            const char *argprefix;
            bool inherit_handles;
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            HANDLE filemap = NULL;

            if (restricted_acl())
                argprefix = "&R";
            else
                argprefix = "";

            if (wParam == IDM_DUPSESS) {
                /*
                 * Allocate a file-mapping memory chunk for the
                 * config structure.
                 */
                SECURITY_ATTRIBUTES sa;
                strbuf *serbuf;
                void *p;
                int size;

                serbuf = strbuf_new();
                conf_serialise(BinarySink_UPCAST(serbuf), wgs->conf);
                size = serbuf->len;

                sa.nLength = sizeof(sa);
                sa.lpSecurityDescriptor = NULL;
                sa.bInheritHandle = true;
                filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
                                            &sa,
                                            PAGE_READWRITE,
                                            0, size, NULL);
                if (filemap && filemap != INVALID_HANDLE_VALUE) {
                    p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, size);
                    if (p) {
                        memcpy(p, serbuf->s, size);
                        UnmapViewOfFile(p);
                    }
                }

                strbuf_free(serbuf);
                inherit_handles = true;
                cl = dupprintf("putty %s&%p:%u", argprefix,
                               filemap, (unsigned)size);
            } else if (wParam == IDM_SAVEDSESS) {
                unsigned int sessno = ((lParam - IDM_SAVED_MIN)
                                       / MENU_SAVED_STEP) + 1;
                if (sessno < (unsigned)sesslist.nsessions) {
                    const char *session = sesslist.sessions[sessno];
                    cl = dupprintf("putty %s@%s", argprefix, session);
                    inherit_handles = false;
                } else
                    break;
            } else /* IDM_NEWSESS */ {
                cl = dupprintf("putty%s%s",
                               *argprefix ? " " : "",
                               argprefix);
                inherit_handles = false;
            }

            GetModuleFileName(NULL, b, sizeof(b) - 1);
            si.cb = sizeof(si);
            si.lpReserved = NULL;
            si.lpDesktop = NULL;
            si.lpTitle = NULL;
            si.dwFlags = 0;
            si.cbReserved2 = 0;
            si.lpReserved2 = NULL;
            CreateProcess(b, cl, NULL, NULL, inherit_handles,
                          NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (filemap)
                CloseHandle(filemap);
            sfree(cl);
            break;
          }
          case IDM_RESTART:
            if (!wgs->backend) {
                lp_eventlog(&wgs->logpolicy, "----- Session restarted -----");
                term_pwron(wgs->term, false);
                start_backend(wgs);
            }

            break;
          case IDM_RECONF: {
            Conf *prev_conf;
            int init_lvl = 1;
            bool reconfig_result;

            if (wgs->reconfiguring)
                break;
            else
                wgs->reconfiguring = true;

            term_pre_reconfig(wgs->term, wgs->conf);
            prev_conf = conf_copy(wgs->conf);

            reconfig_result = do_reconfig(
                hwnd, wgs->conf,
                wgs->backend ? backend_cfg_info(wgs->backend) : 0);
            wgs->reconfiguring = false;
            if (!reconfig_result) {
                conf_free(prev_conf);
                break;
            }

            conf_cache_data(wgs);

            resize_action = conf_get_int(wgs->conf, CONF_resize_action);
            {
                /* Disable full-screen if resizing forbidden */
                int i;
                for (i = 0; i < lenof(wgs->popup_menus); i++)
                    EnableMenuItem(wgs->popup_menus[i].menu, IDM_FULLSCREEN,
                                   MF_BYCOMMAND |
                                   (resize_action == RESIZE_DISABLED
                                    ? MF_GRAYED : MF_ENABLED));
                /* Gracefully unzoom if necessary */
                if (IsZoomed(hwnd) && (resize_action == RESIZE_DISABLED))
                    ShowWindow(hwnd, SW_RESTORE);
            }

            /* Pass new config data to the logging module */
            log_reconfig(wgs->logctx, wgs->conf);

            sfree(wgs->logpal);
            /*
             * Flush the line discipline's edit buffer in the
             * case where local editing has just been disabled.
             */
            if (wgs->ldisc) {
                ldisc_configure(wgs->ldisc, wgs->conf);
                ldisc_echoedit_update(wgs->ldisc);
            }

            if (conf_get_bool(wgs->conf, CONF_system_colour) !=
                conf_get_bool(prev_conf, CONF_system_colour))
                term_notify_palette_changed(wgs->term);

            /* Pass new config data to the terminal */
            term_reconfig(wgs->term, wgs->conf);
            setup_clipboards(wgs->term, wgs->conf);

            /* Reinitialise the colour palette, in case the terminal
             * just read new settings out of Conf */
            if (wgs->pal)
                DeleteObject(wgs->pal);
            wgs->logpal = NULL;
            wgs->pal = NULL;
            init_palette(wgs);

            /* Pass new config data to the back end */
            if (wgs->backend)
                backend_reconfig(wgs->backend, wgs->conf);

            /* Screen size changed ? */
            if (conf_get_int(wgs->conf, CONF_height) !=
                conf_get_int(prev_conf, CONF_height) ||
                conf_get_int(wgs->conf, CONF_width) !=
                conf_get_int(prev_conf, CONF_width) ||
                conf_get_int(wgs->conf, CONF_savelines) !=
                conf_get_int(prev_conf, CONF_savelines) ||
                resize_action == RESIZE_FONT ||
                (resize_action == RESIZE_EITHER && IsZoomed(hwnd)) ||
                resize_action == RESIZE_DISABLED)
                term_size(wgs->term, conf_get_int(wgs->conf, CONF_height),
                          conf_get_int(wgs->conf, CONF_width),
                          conf_get_int(wgs->conf, CONF_savelines));

            /* Enable or disable the scroll bar, etc */
            {
                LONG nflg, flag = GetWindowLongPtr(hwnd, GWL_STYLE);
                LONG nexflag, exflag =
                    GetWindowLongPtr(hwnd, GWL_EXSTYLE);

                nexflag = exflag;
                if (conf_get_bool(wgs->conf, CONF_alwaysontop) !=
                    conf_get_bool(prev_conf, CONF_alwaysontop)) {
                    if (conf_get_bool(wgs->conf, CONF_alwaysontop)) {
                        nexflag |= WS_EX_TOPMOST;
                        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE);
                    } else {
                        nexflag &= ~(WS_EX_TOPMOST);
                        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE);
                    }
                }
                if (conf_get_bool(wgs->conf, CONF_sunken_edge))
                    nexflag |= WS_EX_CLIENTEDGE;
                else
                    nexflag &= ~(WS_EX_CLIENTEDGE);

                nflg = flag;
                if (conf_get_bool(wgs->conf, is_full_screen(wgs) ?
                                  CONF_scrollbar_in_fullscreen :
                                  CONF_scrollbar))
                    nflg |= WS_VSCROLL;
                else
                    nflg &= ~WS_VSCROLL;

                if (resize_action == RESIZE_DISABLED ||
                    is_full_screen(wgs))
                    nflg &= ~WS_THICKFRAME;
                else
                    nflg |= WS_THICKFRAME;

                if (resize_action == RESIZE_DISABLED)
                    nflg &= ~WS_MAXIMIZEBOX;
                else
                    nflg |= WS_MAXIMIZEBOX;

                if (nflg != flag || nexflag != exflag) {
                    if (nflg != flag)
                        SetWindowLongPtr(hwnd, GWL_STYLE, nflg);
                    if (nexflag != exflag)
                        SetWindowLongPtr(hwnd, GWL_EXSTYLE, nexflag);

                    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                                 SWP_NOACTIVATE | SWP_NOCOPYBITS |
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_FRAMECHANGED);

                    init_lvl = 2;
                }
            }

            /* Oops */
            if (resize_action == RESIZE_DISABLED && IsZoomed(hwnd)) {
                force_normal(hwnd);
                init_lvl = 2;
            }

            {
                FontSpec *font = conf_get_fontspec(wgs->conf, CONF_font);
                FontSpec *prev_font = conf_get_fontspec(prev_conf,
                                                        CONF_font);

                if (!strcmp(font->name, prev_font->name) ||
                    !strcmp(conf_get_str(wgs->conf, CONF_line_codepage),
                            conf_get_str(prev_conf, CONF_line_codepage)) ||
                    font->isbold != prev_font->isbold ||
                    font->height != prev_font->height ||
                    font->charset != prev_font->charset ||
                    conf_get_int(wgs->conf, CONF_font_quality) !=
                    conf_get_int(prev_conf, CONF_font_quality) ||
                    conf_get_int(wgs->conf, CONF_vtmode) !=
                    conf_get_int(prev_conf, CONF_vtmode) ||
                    conf_get_int(wgs->conf, CONF_bold_style) !=
                    conf_get_int(prev_conf, CONF_bold_style) ||
                    resize_action == RESIZE_DISABLED ||
                    resize_action == RESIZE_EITHER ||
                    resize_action != conf_get_int(prev_conf,
                                                  CONF_resize_action))
                    init_lvl = 2;
            }

            InvalidateRect(hwnd, NULL, true);
            reset_window(wgs, init_lvl);

            conf_free(prev_conf);
            break;
          }
          case IDM_COPYALL:
            term_copyall(wgs->term, clips_system, lenof(clips_system));
            break;
          case IDM_COPY:
            term_request_copy(wgs->term, clips_system, lenof(clips_system));
            break;
          case IDM_PASTE:
            term_request_paste(wgs->term, CLIP_SYSTEM);
            break;
          case IDM_CLRSB:
            term_clrsb(wgs->term);
            break;
          case IDM_RESET:
            term_pwron(wgs->term, true);
            if (wgs->ldisc)
                ldisc_echoedit_update(wgs->ldisc);
            break;
          case IDM_ABOUT:
            showabout(hwnd);
            break;
          case IDM_HELP:
            launch_help(hwnd, NULL);
            break;
          case SC_MOUSEMENU:
            /*
             * We get this if the System menu has been activated
             * using the mouse.
             */
            show_mouseptr(wgs, true);
            break;
          case SC_KEYMENU:
            /*
             * We get this if the System menu has been activated
             * using the keyboard. This might happen from within
             * TranslateKey, in which case it really wants to be
             * followed by a `space' character to actually _bring
             * the menu up_ rather than just sitting there in
             * `ready to appear' state.
             */
            show_mouseptr(wgs, true);    /* make sure pointer is visible */
            if (lParam == 0)
                PostMessage(hwnd, WM_CHAR, ' ', 0);
            break;
          case IDM_FULLSCREEN:
            flip_full_screen(wgs);
            break;
          default:
            if (wParam >= IDM_SAVED_MIN && wParam < IDM_SAVED_MAX) {
                SendMessage(hwnd, WM_SYSCOMMAND, IDM_SAVEDSESS, wParam);
            }
            if (wParam >= IDM_SPECIAL_MIN && wParam <= IDM_SPECIAL_MAX) {
                int i = (wParam - IDM_SPECIAL_MIN) / 0x10;
                /*
                 * Ensure we haven't been sent a bogus SYSCOMMAND
                 * which would cause us to reference invalid memory
                 * and crash. Perhaps I'm just too paranoid here.
                 */
                if (i >= wgs->n_specials)
                    break;
                if (wgs->backend)
                    backend_special(wgs->backend, wgs->specials[i].code,
                                    wgs->specials[i].arg);
            }
        }
        break;

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))

#define TO_CHR_X(x) ((((x)<0 ? (x)-wgs->font_width+1 :                  \
                       (x))-wgs->offset_width) / wgs->font_width)
#define TO_CHR_Y(y) ((((y)<0 ? (y)-wgs->font_height+1 :                 \
                       (y))-wgs->offset_height) / wgs->font_height)
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:
        if (message == WM_RBUTTONDOWN &&
            ((wParam & MK_CONTROL) ||
             (conf_get_int(wgs->conf, CONF_mouse_is_xterm) == MOUSE_WINDOWS))) {
            POINT cursorpos;

            /* Just in case this happened in mid-select */
            term_cancel_selection_drag(wgs->term);

            show_mouseptr(wgs, true);    /* make sure pointer is visible */
            GetCursorPos(&cursorpos);
            TrackPopupMenu(wgs->popup_menus[CTXMENU].menu,
                           TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                           cursorpos.x, cursorpos.y,
                           0, hwnd, NULL);
            break;
        }
        {
            int button;
            bool press;

            switch (message) {
              case WM_LBUTTONDOWN:
                button = MBT_LEFT;
                wParam |= MK_LBUTTON;
                press = true;
                break;
              case WM_MBUTTONDOWN:
                button = MBT_MIDDLE;
                wParam |= MK_MBUTTON;
                press = true;
                break;
              case WM_RBUTTONDOWN:
                button = MBT_RIGHT;
                wParam |= MK_RBUTTON;
                press = true;
                break;
              case WM_LBUTTONUP:
                button = MBT_LEFT;
                wParam &= ~MK_LBUTTON;
                press = false;
                break;
              case WM_MBUTTONUP:
                button = MBT_MIDDLE;
                wParam &= ~MK_MBUTTON;
                press = false;
                break;
              case WM_RBUTTONUP:
                button = MBT_RIGHT;
                wParam &= ~MK_RBUTTON;
                press = false;
                break;
              default: /* shouldn't happen */
                button = 0;
                press = false;
            }
            show_mouseptr(wgs, true);
            /*
             * Special case: in full-screen mode, if the left
             * button is clicked in the very top left corner of the
             * window, we put up the System menu instead of doing
             * selection.
             */
            {
                bool mouse_on_hotspot = false;
                POINT pt;

                GetCursorPos(&pt);
#ifndef NO_MULTIMON
                if (p_GetMonitorInfoA && p_MonitorFromPoint) {
                    HMONITOR mon;
                    MONITORINFO mi;

                    mon = p_MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);

                    if (mon != NULL) {
                        mi.cbSize = sizeof(MONITORINFO);
                        p_GetMonitorInfoA(mon, &mi);

                        if (mi.rcMonitor.left == pt.x &&
                            mi.rcMonitor.top == pt.y) {
                            mouse_on_hotspot = true;
                        }
                    }
                } else
#endif
                if (pt.x == 0 && pt.y == 0) {
                    mouse_on_hotspot = true;
                }
                if (is_full_screen(wgs) && press &&
                    button == MBT_LEFT && mouse_on_hotspot) {
                    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOUSEMENU,
                                MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
            }

            if (press) {
                click(wgs, button,
                      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
                      wParam & MK_SHIFT, wParam & MK_CONTROL,
                      is_alt_pressed());
                SetCapture(hwnd);
            } else {
                term_mouse(wgs->term, button, translate_button(wgs, button),
                           MA_RELEASE, TO_CHR_X(X_POS(lParam)),
                           TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
                           wParam & MK_CONTROL, is_alt_pressed());
                if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
                    ReleaseCapture();
            }
        }
        return 0;
      case WM_MOUSEMOVE:
        /*
         * Windows seems to like to occasionally send MOUSEMOVE
         * events even if the mouse hasn't moved. Don't unhide
         * the mouse pointer in this case.
         */
        if (wgs->last_mousemove != WM_MOUSEMOVE ||
            wParam != wgs->last_wm_mousemove_wParam ||
            lParam != wgs->last_wm_mousemove_lParam) {
            show_mouseptr(wgs, true);
            wgs->last_mousemove = WM_MOUSEMOVE;
            wgs->last_wm_mousemove_wParam = wParam;
            wgs->last_wm_mousemove_lParam = lParam;
        }
        /*
         * Add the mouse position and message time to the random
         * number noise.
         */
        noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);

        if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON) &&
            GetCapture() == hwnd) {
            Mouse_Button b;
            if (wParam & MK_LBUTTON)
                b = MBT_LEFT;
            else if (wParam & MK_MBUTTON)
                b = MBT_MIDDLE;
            else
                b = MBT_RIGHT;
            term_mouse(wgs->term, b, translate_button(wgs, b), MA_DRAG,
                       TO_CHR_X(X_POS(lParam)),
                       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
                       wParam & MK_CONTROL, is_alt_pressed());
        } else {
            term_mouse(wgs->term, MBT_NOTHING, MBT_NOTHING, MA_MOVE,
                       TO_CHR_X(X_POS(lParam)),
                       TO_CHR_Y(Y_POS(lParam)), false,
                       false, false);
        }
        return 0;
      case WM_NCMOUSEMOVE:
        if (wgs->last_mousemove != WM_NCMOUSEMOVE ||
            wParam != wgs->last_wm_ncmousemove_wParam ||
            lParam != wgs->last_wm_ncmousemove_lParam) {
            show_mouseptr(wgs, true);
            wgs->last_mousemove = WM_NCMOUSEMOVE;
            wgs->last_wm_ncmousemove_wParam = wParam;
            wgs->last_wm_ncmousemove_lParam = lParam;
        }
        noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);
        break;
      case WM_IGNORE_CLIP:
        wgs->ignore_clip = wParam; /* don't panic on DESTROYCLIPBOARD */
        break;
      case WM_DESTROYCLIPBOARD:
        if (!wgs->ignore_clip)
            term_lost_clipboard_ownership(wgs->term, CLIP_SYSTEM);
        wgs->ignore_clip = false;
        return 0;
      case WM_PAINT: {
        PAINTSTRUCT p;

        HideCaret(hwnd);
        hdc = BeginPaint(hwnd, &p);
        if (wgs->pal) {
            SelectPalette(hdc, wgs->pal, true);
            RealizePalette(hdc);
        }

        /*
         * We have to be careful about term_paint(). It will
         * set a bunch of character cells to INVALID and then
         * call do_paint(), which will redraw those cells and
         * _then mark them as done_. This may not be accurate:
         * when painting in WM_PAINT context we are restricted
         * to the rectangle which has just been exposed - so if
         * that only covers _part_ of a character cell and the
         * rest of it was already visible, that remainder will
         * not be redrawn at all. Accordingly, we must not
         * paint any character cell in a WM_PAINT context which
         * already has a pending update due to terminal output.
         * The simplest solution to this - and many, many
         * thanks to Hung-Te Lin for working all this out - is
         * not to do any actual painting at _all_ if there's a
         * pending terminal update: just mark the relevant
         * character cells as INVALID and wait for the
         * scheduled full update to sort it out.
         *
         * I have a suspicion this isn't the _right_ solution.
         * An alternative approach would be to have terminal.c
         * separately track what _should_ be on the terminal
         * screen and what _is_ on the terminal screen, and
         * have two completely different types of redraw (one
         * for full updates, which syncs the former with the
         * terminal itself, and one for WM_PAINT which syncs
         * the latter with the former); yet another possibility
         * would be to have the Windows front end do what the
         * GTK one already does, and maintain a bitmap of the
         * current terminal appearance so that WM_PAINT becomes
         * completely trivial. However, this should do for now.
         */
        assert(!wgs->wintw_hdc);
        wgs->wintw_hdc = hdc;
        term_paint(wgs->term,
                   (p.rcPaint.left-wgs->offset_width)/wgs->font_width,
                   (p.rcPaint.top-wgs->offset_height)/wgs->font_height,
                   (p.rcPaint.right-wgs->offset_width-1)/wgs->font_width,
                   (p.rcPaint.bottom-wgs->offset_height-1)/wgs->font_height,
                   !wgs->term->window_update_pending);
        wgs->wintw_hdc = NULL;

        if (p.fErase ||
            p.rcPaint.left  < wgs->offset_width  ||
            p.rcPaint.top   < wgs->offset_height ||
            p.rcPaint.right >= (wgs->offset_width +
                                wgs->font_width*wgs->term->cols) ||
            p.rcPaint.bottom>= (wgs->offset_height +
                                wgs->font_height*wgs->term->rows)) {
            HBRUSH fillcolour, oldbrush;
            HPEN   edge, oldpen;
            fillcolour = CreateSolidBrush (
                wgs->colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
            oldbrush = SelectObject(hdc, fillcolour);
            edge = CreatePen(PS_SOLID, 0,
                             wgs->colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
            oldpen = SelectObject(hdc, edge);

            /*
             * Jordan Russell reports that this apparently
             * ineffectual IntersectClipRect() call masks a
             * Windows NT/2K bug causing strange display
             * problems when the PuTTY window is taller than
             * the primary monitor. It seems harmless enough...
             */
            IntersectClipRect(hdc,
                              p.rcPaint.left, p.rcPaint.top,
                              p.rcPaint.right, p.rcPaint.bottom);

            ExcludeClipRect(
                hdc, wgs->offset_width, wgs->offset_height,
                wgs->offset_width+wgs->font_width*wgs->term->cols,
                wgs->offset_height+wgs->font_height*wgs->term->rows);

            Rectangle(hdc, p.rcPaint.left, p.rcPaint.top,
                      p.rcPaint.right, p.rcPaint.bottom);

            /* SelectClipRgn(hdc, NULL); */

            SelectObject(hdc, oldbrush);
            DeleteObject(fillcolour);
            SelectObject(hdc, oldpen);
            DeleteObject(edge);
        }
        SelectObject(hdc, GetStockObject(SYSTEM_FONT));
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        EndPaint(hwnd, &p);
        ShowCaret(hwnd);
        return 0;
      }
      case WM_NETEVENT:
        winselgui_response(wParam, lParam);
        return 0;
      case WM_SETFOCUS:
        term_set_focus(wgs->term, true);
        CreateCaret(hwnd, wgs->caretbm, wgs->font_width, wgs->font_height);
        ShowCaret(hwnd);
        flash_window(wgs, 0);               /* stop */
        wgs->compose_state = 0;
        term_update(wgs->term);
        break;
      case WM_KILLFOCUS:
        show_mouseptr(wgs, true);
        term_set_focus(wgs->term, false);
        DestroyCaret();
        wgs->caret_x = wgs->caret_y = -1; /* ensure caret replaced next time */
        term_update(wgs->term);
        break;
      case WM_ENTERSIZEMOVE:
        EnableSizeTip(true);
        wgs->resizing = true;
        wgs->need_backend_resize = false;
        break;
      case WM_EXITSIZEMOVE:
        EnableSizeTip(false);
        wgs->resizing = false;
        if (wgs->need_backend_resize) {
            term_size(wgs->term, conf_get_int(wgs->conf, CONF_height),
                      conf_get_int(wgs->conf, CONF_width),
                      conf_get_int(wgs->conf, CONF_savelines));
            InvalidateRect(hwnd, NULL, true);
        }
        recompute_window_offset(wgs);
        break;
      case WM_SIZING:
        /*
         * This does two jobs:
         * 1) Keep the sizetip uptodate
         * 2) Make sure the window size is _stepped_ in units of the font size.
         */
        resize_action = conf_get_int(wgs->conf, CONF_resize_action);
        if (resize_action == RESIZE_TERM ||
            (resize_action == RESIZE_EITHER && !is_alt_pressed())) {
            int width, height, w, h, ew, eh;
            LPRECT r = (LPRECT) lParam;

            if (!wgs->need_backend_resize && resize_action == RESIZE_EITHER &&
                (conf_get_int(wgs->conf, CONF_height) != wgs->term->rows ||
                 conf_get_int(wgs->conf, CONF_width) != wgs->term->cols)) {
                /*
                 * Great! It seems that both the terminal size and the
                 * font size have been changed and the user is now dragging.
                 *
                 * It will now be difficult to get back to the configured
                 * font size!
                 *
                 * This would be easier but it seems to be too confusing.
                 */
                conf_set_int(wgs->conf, CONF_height, wgs->term->rows);
                conf_set_int(wgs->conf, CONF_width, wgs->term->cols);

                InvalidateRect(hwnd, NULL, true);
                wgs->need_backend_resize = true;
            }

            width = r->right - r->left - wgs->extra_width;
            height = r->bottom - r->top - wgs->extra_height;
            w = (width + wgs->font_width / 2) / wgs->font_width;
            if (w < 1)
                w = 1;
            h = (height + wgs->font_height / 2) / wgs->font_height;
            if (h < 1)
                h = 1;
            UpdateSizeTip(hwnd, w, h);
            ew = width - w * wgs->font_width;
            eh = height - h * wgs->font_height;
            if (ew != 0) {
                if (wParam == WMSZ_LEFT ||
                    wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                    r->left += ew;
                else
                    r->right -= ew;
            }
            if (eh != 0) {
                if (wParam == WMSZ_TOP ||
                    wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                    r->top += eh;
                else
                    r->bottom -= eh;
            }
            if (ew || eh)
                return 1;
            else
                return 0;
        } else {
            int width, height, w, h, rv = 0;
            int window_border = conf_get_int(wgs->conf, CONF_window_border);
            int ex_width = wgs->extra_width +
                (window_border - wgs->offset_width) * 2;
            int ex_height = wgs->extra_height +
                (window_border - wgs->offset_height) * 2;
            LPRECT r = (LPRECT) lParam;

            width = r->right - r->left - ex_width;
            height = r->bottom - r->top - ex_height;

            w = (width + wgs->term->cols/2)/wgs->term->cols;
            h = (height + wgs->term->rows/2)/wgs->term->rows;
            if ( r->right != r->left + w*wgs->term->cols + ex_width)
                rv = 1;

            if (wParam == WMSZ_LEFT ||
                wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                r->left = r->right - w*wgs->term->cols - ex_width;
            else
                r->right = r->left + w*wgs->term->cols + ex_width;

            if (r->bottom != r->top + h*wgs->term->rows + ex_height)
                rv = 1;

            if (wParam == WMSZ_TOP ||
                wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                r->top = r->bottom - h*wgs->term->rows - ex_height;
            else
                r->bottom = r->top + h*wgs->term->rows + ex_height;

            return rv;
        }
        /* break;  (never reached) */
      case WM_FULLSCR_ON_MAX:
        wgs->fullscr_on_max = true;
        break;
      case WM_MOVE:
        term_notify_window_pos(wgs->term, LOWORD(lParam), HIWORD(lParam));
        sys_cursor_update(wgs);
        break;
      case WM_SIZE:
        resize_action = conf_get_int(wgs->conf, CONF_resize_action);
        term_notify_minimised(wgs->term, wParam == SIZE_MINIMIZED);
        {
            /*
             * WM_SIZE's lParam tells us the size of the client area.
             * But historic PuTTY practice is that we want to tell the
             * terminal the size of the overall window.
             */
            RECT r;
            GetWindowRect(hwnd, &r);
            term_notify_window_size_pixels(
                wgs->term, r.right - r.left, r.bottom - r.top);
        }
        if (wParam == SIZE_MINIMIZED)
            sw_SetWindowText(hwnd,
                             conf_get_bool(wgs->conf, CONF_win_name_always) ?
                             wgs->window_name : wgs->icon_name);
        if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
            sw_SetWindowText(hwnd, wgs->window_name);
        if (wParam == SIZE_RESTORED) {
            wgs->processed_resize = false;
            clear_full_screen(wgs);
            if (wgs->processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * clear_full_screen which contained the correct
                 * client area size.
                 */
                return 0;
            }
        }
        if (wParam == SIZE_MAXIMIZED && wgs->fullscr_on_max) {
            wgs->fullscr_on_max = false;
            wgs->processed_resize = false;
            make_full_screen(wgs);
            if (wgs->processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * make_full_screen which contained the correct client
                 * area size.
                 */
                return 0;
            }
        }

        wgs->processed_resize = true;

        if (resize_action == RESIZE_DISABLED) {
            /* A resize, well it better be a minimize. */
            reset_window(wgs, -1);
        } else {
            if (wParam == SIZE_MAXIMIZED) {
                wgs->was_zoomed = true;
                wgs->prev_rows = wgs->term->rows;
                wgs->prev_cols = wgs->term->cols;
                if (resize_action == RESIZE_TERM)
                    wm_size_resize_term(wgs, lParam);
                reset_window(wgs, 0);
            } else if (wParam == SIZE_RESTORED && wgs->was_zoomed) {
                wgs->was_zoomed = false;
                if (resize_action == RESIZE_TERM) {
                    wm_size_resize_term(wgs, lParam);
                    reset_window(wgs, 2);
                } else if (resize_action != RESIZE_FONT)
                    reset_window(wgs, 2);
                else
                    reset_window(wgs, 0);
            } else if (wParam == SIZE_MINIMIZED) {
                /* do nothing */
            } else if (resize_action == RESIZE_TERM ||
                       (resize_action == RESIZE_EITHER &&
                        !is_alt_pressed())) {
                wm_size_resize_term(wgs, lParam);

                /*
                 * Sometimes, we can get a spontaneous resize event
                 * outside a WM_SIZING interactive drag which wants to
                 * set us to a new specific SIZE_RESTORED size. An
                 * example is what happens if you press Windows+Right
                 * and then Windows+Up: the first operation fits the
                 * window to the right-hand half of the screen, and
                 * the second one changes that for the top right
                 * quadrant. In that situation, if we've responded
                 * here by resizing the terminal, we may still need to
                 * recompute the border around the window and do a
                 * full redraw to clear the new border.
                 */
                if (!wgs->resizing)
                    recompute_window_offset(wgs);
            } else {
                reset_window(wgs, 0);
            }
        }
        sys_cursor_update(wgs);
        return 0;
      case WM_DPICHANGED:
        wgs->dpi_info.cur_dpi.x = LOWORD(wParam);
        wgs->dpi_info.cur_dpi.y = HIWORD(wParam);
        wgs->dpi_info.new_wnd_rect = *(RECT*)(lParam);
        reset_window(wgs, 3);
        return 0;
      case WM_VSCROLL:
        switch (LOWORD(wParam)) {
          case SB_BOTTOM:
            term_scroll(wgs->term, -1, 0);
            break;
          case SB_TOP:
            term_scroll(wgs->term, +1, 0);
            break;
          case SB_LINEDOWN:
            term_scroll(wgs->term, 0, +1);
            break;
          case SB_LINEUP:
            term_scroll(wgs->term, 0, -1);
            break;
          case SB_PAGEDOWN:
            term_scroll(wgs->term, 0, +wgs->term->rows / 2);
            break;
          case SB_PAGEUP:
            term_scroll(wgs->term, 0, -wgs->term->rows / 2);
            break;
          case SB_THUMBPOSITION:
          case SB_THUMBTRACK: {
            /*
             * Use GetScrollInfo instead of HIWORD(wParam) to get
             * 32-bit scroll position.
             */
            SCROLLINFO si;

            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(hwnd, SB_VERT, &si) == 0)
                si.nTrackPos = HIWORD(wParam);
            term_scroll(wgs->term, 1, si.nTrackPos);
            break;
          }
        }

        if (wgs->in_scrollbar_loop) {
            /*
             * Allow window updates to happen during interactive
             * scroll.
             *
             * When the user takes hold of our window's scrollbar and
             * wobbles it interactively back and forth, or presses on
             * one of the arrow buttons at the ends, the first thing
             * that happens is that this window procedure receives
             * WM_SYSCOMMAND / SC_VSCROLL. [1] The default handler for
             * that window message starts a subsidiary message loop,
             * which continues to run until the user lets go of the
             * scrollbar again. All WM_VSCROLL / SB_THUMBTRACK
             * messages are generated by the handlers within that
             * subsidiary message loop.
             *
             * So, during that time, _our_ message loop is not
             * running, which means toplevel callbacks and timers and
             * so forth are not happening, which means that when we
             * redraw the window and set a timer to clear the cooldown
             * flag 20ms later, that timer never fires, and we aren't
             * able to keep redrawing the window.
             *
             * The 'obvious' answer would be to seize that SYSCOMMAND
             * ourselves and inhibit the default handler, so that our
             * message loop carries on running. But that would mean
             * we'd have to reimplement the whole of the scrollbar
             * handler!
             *
             * So instead we apply a bodge: set a static variable that
             * indicates that we're _in_ that sub-loop, and if so,
             * decide it's OK to manually call term_update() proper,
             * bypassing the timer and cooldown and rate-limiting
             * systems completely, whenever we see an SB_THUMBTRACK.
             * This shouldn't cause a rate overload, because we're
             * only doing it once per UI event!
             *
             * [1] Actually, there's an extra oddity where SC_HSCROLL
             * and SC_VSCROLL have their documented values the wrong
             * way round. Many people on the Internet have noticed
             * this, e.g. https://stackoverflow.com/q/55528397
             */
            term_update(wgs->term);
        }
        break;
      case WM_PALETTECHANGED:
        if ((HWND) wParam != hwnd && wgs->pal != NULL) {
            HDC hdc = make_hdc(wgs);
            if (hdc) {
                if (RealizePalette(hdc) > 0)
                    UpdateColors(hdc);
                free_hdc(wgs, hdc);
            }
        }
        break;
      case WM_QUERYNEWPALETTE:
        if (wgs->pal != NULL) {
            HDC hdc = make_hdc(wgs);
            if (hdc) {
                if (RealizePalette(hdc) > 0)
                    UpdateColors(hdc);
                free_hdc(wgs, hdc);
                return true;
            }
        }
        return false;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
        /*
         * Add the scan code and keypress timing to the random
         * number noise.
         */
        noise_ultralight(NOISE_SOURCE_KEY, lParam);

        /*
         * We don't do TranslateMessage since it disassociates the
         * resulting CHAR message from the KEYDOWN that sparked it,
         * which we occasionally don't want. Instead, we process
         * KEYDOWN, and call the Win32 translator functions so that
         * we get the translations under _our_ control.
         */
        {
            unsigned char buf[20];
            int len;

            if (wParam == VK_PROCESSKEY || /* IME PROCESS key */
                wParam == VK_PACKET) {     /* 'this key is a Unicode char' */
                if (message == WM_KEYDOWN) {
                    MSG m;
                    m.hwnd = hwnd;
                    m.message = WM_KEYDOWN;
                    m.wParam = wParam;
                    m.lParam = lParam & 0xdfff;
                    TranslateMessage(&m);
                } else break; /* pass to Windows for default processing */
            } else {
                len = TranslateKey(wgs, message, wParam, lParam, buf);
                if (len == -1)
                    return sw_DefWindowProc(hwnd, message, wParam, lParam);

                if (len != 0) {
                    /*
                     * We need not bother about stdin backlogs
                     * here, because in GUI PuTTY we can't do
                     * anything about it anyway; there's no means
                     * of asking Windows to hold off on KEYDOWN
                     * messages. We _have_ to buffer everything
                     * we're sent.
                     */
                    term_keyinput(wgs->term, -1, buf, len);
                    show_mouseptr(wgs, false);
                }
            }
        }
        return 0;
      case WM_INPUTLANGCHANGE:
        /* wParam == Font number */
        /* lParam == Locale */
        set_input_locale(wgs, (HKL)lParam);
        sys_cursor_update(wgs);
        break;
      case WM_IME_STARTCOMPOSITION: {
        HIMC hImc = ImmGetContext(hwnd);
        ImmSetCompositionFont(hImc, &wgs->lfont);
        ImmReleaseContext(hwnd, hImc);
        break;
      }
      case WM_IME_COMPOSITION: {
        HIMC hIMC;
        int n;
        char *buff;

        if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS ||
            osPlatformId == VER_PLATFORM_WIN32s)
            break; /* no Unicode */

        if ((lParam & GCS_RESULTSTR) == 0) /* Composition unfinished. */
            break; /* fall back to DefWindowProc */

        hIMC = ImmGetContext(hwnd);
        n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);

        if (n > 0) {
            int i;
            buff = snewn(n, char);
            ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buff, n);
            /*
             * Jaeyoun Chung reports that Korean character
             * input doesn't work correctly if we do a single
             * term_keyinputw covering the whole of buff. So
             * instead we send the characters one by one.
             */
            /* don't divide SURROGATE PAIR */
            if (wgs->ldisc) {
                for (i = 0; i < n; i += 2) {
                    WCHAR hs = *(unsigned short *)(buff+i);
                    if (IS_HIGH_SURROGATE(hs) && i+2 < n) {
                        WCHAR ls = *(unsigned short *)(buff+i+2);
                        if (IS_LOW_SURROGATE(ls)) {
                            term_keyinputw(
                                wgs->term, (unsigned short *)(buff+i), 2);
                            i += 2;
                            continue;
                        }
                    }
                    term_keyinputw(
                        wgs->term, (unsigned short *)(buff+i), 1);
                }
            }
            free(buff);
        }
        ImmReleaseContext(hwnd, hIMC);
        return 1;
      }

      case WM_IME_CHAR:
        if (wParam & 0xFF00) {
            char buf[2];

            buf[1] = wParam;
            buf[0] = wParam >> 8;
            term_keyinput(wgs->term, wgs->kbd_codepage, buf, 2);
        } else {
            char c = (unsigned char) wParam;
            term_seen_key_event(wgs->term);
            term_keyinput(wgs->term, wgs->kbd_codepage, &c, 1);
        }
        return (0);
      case WM_CHAR:
      case WM_SYSCHAR:
        /*
         * Nevertheless, we are prepared to deal with WM_CHAR
         * messages, should they crop up. So if someone wants to
         * post the things to us as part of a macro manoeuvre,
         * we're ready to cope.
         */
        if (unicode_window) {
            wchar_t c = wParam;

            if (IS_HIGH_SURROGATE(c)) {
                wgs->pending_surrogate = c;
            } else if (IS_SURROGATE_PAIR(wgs->pending_surrogate, c)) {
                wchar_t pair[2];
                pair[0] = wgs->pending_surrogate;
                pair[1] = c;
                term_keyinputw(wgs->term, pair, 2);
            } else if (!IS_SURROGATE(c)) {
                term_keyinputw(wgs->term, &c, 1);
            }
        } else {
            char c = (unsigned char)wParam;
            term_seen_key_event(wgs->term);
            if (wgs->ldisc)
                term_keyinput(wgs->term, CP_ACP, &c, 1);
        }
        return 0;
      case WM_SYSCOLORCHANGE:
        if (conf_get_bool(wgs->conf, CONF_system_colour)) {
            /* Refresh palette from system colours. */
            term_notify_palette_changed(wgs->term);
            init_palette(wgs);
            /* Force a repaint of the terminal window. */
            term_invalidate(wgs->term);
        }
        break;
      case WM_GOT_CLIPDATA:
        process_clipdata(wgs, (HGLOBAL)lParam, wParam);
        return 0;
      default:
        if (message == wm_mousewheel || message == WM_MOUSEWHEEL
                                                || message == WM_MOUSEHWHEEL) {
            bool shift_pressed = false, control_pressed = false;

            if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
                wgs->wheel_accumulator += (short)HIWORD(wParam);
                shift_pressed=LOWORD(wParam) & MK_SHIFT;
                control_pressed=LOWORD(wParam) & MK_CONTROL;
            } else {
                BYTE keys[256];
                wgs->wheel_accumulator += (int)wParam;
                if (GetKeyboardState(keys)!=0) {
                    shift_pressed=keys[VK_SHIFT]&0x80;
                    control_pressed=keys[VK_CONTROL]&0x80;
                }
            }

            /* process events when the threshold is reached */
            while (abs(wgs->wheel_accumulator) >= WHEEL_DELTA) {
                int b;

                /* reduce amount for next time */
                if (wgs->wheel_accumulator > 0) {
                    b = message == WM_MOUSEHWHEEL ? MBT_WHEEL_RIGHT : MBT_WHEEL_UP;
                    wgs->wheel_accumulator -= WHEEL_DELTA;
                } else if (wgs->wheel_accumulator < 0) {
                    b =  message == WM_MOUSEHWHEEL ? MBT_WHEEL_LEFT : MBT_WHEEL_DOWN;
                    wgs->wheel_accumulator += WHEEL_DELTA;
                } else
                    break;

                if (wgs->send_raw_mouse &&
                    !(conf_get_bool(wgs->conf, CONF_mouse_override) &&
                      shift_pressed)) {
                    /* Mouse wheel position is in screen coordinates for
                     * some reason */
                    POINT p;
                    p.x = X_POS(lParam); p.y = Y_POS(lParam);
                    if (ScreenToClient(hwnd, &p)) {
                        /* send a mouse-down followed by a mouse up */
                        term_mouse(wgs->term, b, translate_button(wgs, b),
                                   MA_CLICK,
                                   TO_CHR_X(p.x),
                                   TO_CHR_Y(p.y), shift_pressed,
                                   control_pressed, is_alt_pressed());
                    } /* else: not sure when this can fail */
                } else if (message != WM_MOUSEHWHEEL) {
                    /* trigger a scroll */
                    term_scroll(wgs->term, 0,
                                b == MBT_WHEEL_UP ?
                                -wgs->term->rows / 2 : wgs->term->rows / 2);
                }
            }
            return 0;
        }
    }

    /*
     * Any messages we don't process completely above are passed through to
     * DefWindowProc() for default processing.
     */
    return sw_DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
static void wintw_set_cursor_pos(TermWin *tw, int x, int y)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    int cx, cy;

    if (!wgs->term->has_focus) return;

    /*
     * Avoid gratuitously re-updating the cursor position and IMM
     * window if there's no actual change required.
     */
    cx = x * wgs->font_width + wgs->offset_width;
    cy = y * wgs->font_height + wgs->offset_height;
    if (cx == wgs->caret_x && cy == wgs->caret_y)
        return;
    wgs->caret_x = cx;
    wgs->caret_y = cy;

    sys_cursor_update(wgs);
}

static void sys_cursor_update(WinGuiSeat *wgs)
{
    COMPOSITIONFORM cf;
    HIMC hIMC;

    if (!wgs->term->has_focus) return;

    if (wgs->caret_x < 0 || wgs->caret_y < 0)
        return;

    SetCaretPos(wgs->caret_x, wgs->caret_y);

    /* IMM calls on Win98 and beyond only */
    if (osPlatformId == VER_PLATFORM_WIN32s) return; /* 3.11 */

    if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
        osMinorVersion == 0) return; /* 95 */

    /* we should have the IMM functions */
    hIMC = ImmGetContext(wgs->term_hwnd);
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = wgs->caret_x;
    cf.ptCurrentPos.y = wgs->caret_y;
    ImmSetCompositionWindow(hIMC, &cf);

    ImmReleaseContext(wgs->term_hwnd, hIMC);
}

static void draw_horizontal_line_on_text(
    WinGuiSeat *wgs, int y, int lattr, RECT line_box, COLORREF colour)
{
    if (lattr == LATTR_TOP || lattr == LATTR_BOT) {
        y *= 2;
        if (lattr == LATTR_BOT)
            y -= wgs->font_height;
    }

    if (!(0 <= y && y < wgs->font_height))
        return;

    HPEN oldpen = SelectObject(wgs->wintw_hdc, CreatePen(PS_SOLID, 0, colour));
    MoveToEx(wgs->wintw_hdc, line_box.left, line_box.top + y, NULL);
    LineTo(wgs->wintw_hdc, line_box.right, line_box.top + y);
    oldpen = SelectObject(wgs->wintw_hdc, oldpen);
    DeleteObject(oldpen);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
static void do_text_internal(
    WinGuiSeat *wgs, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    RECT line_box;
    bool force_manual_underline = false;
    int fnt_width, char_width;
    int text_adjust = 0;
    int xoffset = 0;
    int maxlen, remaining;
    bool opaque;
    bool is_cursor = false;
    int *lpDx = NULL;
    size_t lpDx_len = 0;
    bool use_lpDx;
    wchar_t *wbuf = NULL;
    char *cbuf = NULL;
    size_t wbuflen = 0, cbuflen = 0;
    int len2; /* for SURROGATE PAIR */

    lattr &= LATTR_MODE;

    char_width = fnt_width = wgs->font_width * (1 + (lattr != LATTR_NORM));

    if (attr & ATTR_WIDE)
        char_width *= 2;

    /* Only want the left half of double width lines */
    if (lattr != LATTR_NORM && x*2 >= wgs->term->cols)
        return;

    x *= fnt_width;
    y *= wgs->font_height;
    x += wgs->offset_width;
    y += wgs->offset_height;

    if ((attr & TATTR_ACTCURS) &&
        (wgs->cursor_type == CURSOR_BLOCK || wgs->term->big_cursor)) {
        truecolour.fg = truecolour.bg = optionalrgb_none;
        attr &= ~(ATTR_REVERSE|ATTR_BLINK|ATTR_COLOURS|ATTR_DIM);
        /* cursor fg and bg */
        attr |= (260 << ATTR_FGSHIFT) | (261 << ATTR_BGSHIFT);
        is_cursor = true;
    }

    nfont = 0;
    if (wgs->vtmode == VT_POORMAN && lattr != LATTR_NORM) {
        /* Assume a poorman font is borken in other ways too. */
        lattr = LATTR_WIDE;
    } else
        switch (lattr) {
          case LATTR_NORM:
            break;
          case LATTR_WIDE:
            nfont |= FONT_WIDE;
            break;
          default:
            nfont |= FONT_WIDE + FONT_HIGH;
            break;
        }
    if (attr & ATTR_NARROW)
        nfont |= FONT_NARROW;

#ifdef USES_VTLINE_HACK
    /* Special hack for the VT100 linedraw glyphs. */
    if (text[0] >= 0x23BA && text[0] <= 0x23BD) {
        switch ((unsigned char) (text[0])) {
          case 0xBA:
            text_adjust = -2 * wgs->font_height / 5;
            break;
          case 0xBB:
            text_adjust = -1 * wgs->font_height / 5;
            break;
          case 0xBC:
            text_adjust = wgs->font_height / 5;
            break;
          case 0xBD:
            text_adjust = 2 * wgs->font_height / 5;
            break;
        }
        if (lattr == LATTR_TOP || lattr == LATTR_BOT)
            text_adjust *= 2;
        text[0] = wgs->ucsdata.unitab_xterm['q'];
        if (attr & ATTR_UNDER) {
            attr &= ~ATTR_UNDER;
            force_manual_underline = true;
        }
    }
#endif

    /* Anything left as an original character set is unprintable. */
    if (DIRECT_CHAR(text[0]) &&
        (len < 2 || !IS_SURROGATE_PAIR(text[0], text[1]))) {
        int i;
        for (i = 0; i < len; i++)
            text[i] = 0xFFFD;
    }

    /* OEM CP */
    if ((text[0] & CSET_MASK) == CSET_OEMCP)
        nfont |= FONT_OEM;

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (wgs->bold_font_mode == BOLD_FONT && (attr & ATTR_BOLD))
        nfont |= FONT_BOLD;
    if (wgs->und_mode == UND_FONT && (attr & ATTR_UNDER))
        nfont |= FONT_UNDERLINE;
    another_font(wgs, nfont);
    if (!wgs->fonts[nfont]) {
        if (nfont & FONT_UNDERLINE)
            force_manual_underline = true;
        /* Don't do the same for manual bold, it could be bad news. */

        nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
    }
    another_font(wgs, nfont);
    if (!wgs->fonts[nfont])
        nfont = FONT_NORMAL;
    if (attr & ATTR_REVERSE) {
        struct optionalrgb trgb;

        t = nfg;
        nfg = nbg;
        nbg = t;

        trgb = truecolour.fg;
        truecolour.fg = truecolour.bg;
        truecolour.bg = trgb;
    }
    if (wgs->bold_colours && (attr & ATTR_BOLD) && !is_cursor) {
        if (nfg < 16) nfg |= 8;
        else if (nfg >= 256) nfg |= 1;
    }
    if (wgs->bold_colours && (attr & ATTR_BLINK)) {
        if (nbg < 16) nbg |= 8;
        else if (nbg >= 256) nbg |= 1;
    }
    if (!wgs->pal && truecolour.fg.enabled)
        fg = RGB(truecolour.fg.r, truecolour.fg.g, truecolour.fg.b);
    else
        fg = wgs->colours[nfg];

    if (!wgs->pal && truecolour.bg.enabled)
        bg = RGB(truecolour.bg.r, truecolour.bg.g, truecolour.bg.b);
    else
        bg = wgs->colours[nbg];

    if (!wgs->pal && (attr & ATTR_DIM)) {
        fg = RGB(GetRValue(fg) * 2 / 3,
                 GetGValue(fg) * 2 / 3,
                 GetBValue(fg) * 2 / 3);
    }

    SelectObject(wgs->wintw_hdc, wgs->fonts[nfont]);
    SetTextColor(wgs->wintw_hdc, fg);
    SetBkColor(wgs->wintw_hdc, bg);
    if (attr & TATTR_COMBINING)
        SetBkMode(wgs->wintw_hdc, TRANSPARENT);
    else
        SetBkMode(wgs->wintw_hdc, OPAQUE);
    line_box.left = x;
    line_box.top = y;
    line_box.right = x + char_width * len;
    line_box.bottom = y + wgs->font_height;
    /* adjust line_box.right for SURROGATE PAIR & VARIATION SELECTOR */
    {
        int i;
        int rc_width = 0;
        for (i = 0; i < len ; i++) {
            if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
                i++;
            } else if (i+1 < len && IS_SURROGATE_PAIR(text[i], text[i+1])) {
                rc_width += char_width;
                i++;
            } else if (IS_LOW_VARSEL(text[i])) {
                /* do nothing */
            } else {
                rc_width += char_width;
            }
        }
        line_box.right = line_box.left + rc_width;
    }

    /* Only want the left half of double width lines */
    if (line_box.right > wgs->font_width*wgs->term->cols+wgs->offset_width)
        line_box.right = wgs->font_width*wgs->term->cols+wgs->offset_width;

    if (wgs->font_varpitch) {
        /*
         * If we're using a variable-pitch font, we unconditionally
         * draw the glyphs one at a time and centre them in their
         * character cells (which means in particular that we must
         * disable the lpDx mechanism). This gives slightly odd but
         * generally reasonable results.
         */
        xoffset = char_width / 2;
        SetTextAlign(wgs->wintw_hdc, TA_TOP | TA_CENTER | TA_NOUPDATECP);
        use_lpDx = false;
        maxlen = 1;
    } else {
        /*
         * In a fixed-pitch font, we draw the whole string in one go
         * in the normal way.
         */
        xoffset = 0;
        SetTextAlign(wgs->wintw_hdc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        use_lpDx = true;
        maxlen = len;
    }

    opaque = true;                     /* start by erasing the rectangle */
    for (remaining = len; remaining > 0;
         text += len, remaining -= len, x += char_width * len2) {
        len = (maxlen < remaining ? maxlen : remaining);
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
        len2 = len;
        if (maxlen == 1) {
            if (remaining >= 1 && IS_SURROGATE_PAIR(text[0], text[1]))
                len++;
            if (remaining-len >= 1 && IS_LOW_VARSEL(text[len]))
                len++;
            else if (remaining-len >= 2 &&
                     IS_HIGH_VARSEL(text[len], text[len+1]))
                len += 2;
        }

        if (len > lpDx_len)
            sgrowarray(lpDx, lpDx_len, len);

        {
            int i;
            /* only last char has dx width in SURROGATE PAIR and
             * VARIATION sequence */
            for (i = 0; i < len; i++) {
                lpDx[i] = char_width;
                if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (i+1 < len && IS_SURROGATE_PAIR(text[i],text[i+1])) {
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (IS_LOW_VARSEL(text[i])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = char_width;
                }
            }
        }

        /* We're using a private area for direct to font. (512 chars.) */
        if (wgs->ucsdata.dbcs_screenfont &&
            (text[0] & CSET_MASK) == CSET_ACP) {
            /* Ho Hum, dbcs fonts are a PITA! */
            /* To display on W9x I have to convert to UCS */
            int nlen, mptr;

            sgrowarray(wbuf, wbuflen, len);
            for (nlen = mptr = 0; mptr<len; mptr++) {
                wbuf[nlen] = 0xFFFD;
                if (IsDBCSLeadByteEx(wgs->ucsdata.font_codepage,
                                     (BYTE) text[mptr])) {
                    char dbcstext[2];
                    dbcstext[0] = text[mptr] & 0xFF;
                    dbcstext[1] = text[mptr+1] & 0xFF;
                    lpDx[nlen] += char_width;
                    MultiByteToWideChar(
                        wgs->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                        dbcstext, 2, wbuf+nlen, 1);
                    mptr++;
                } else {
                    char dbcstext[1];
                    dbcstext[0] = text[mptr] & 0xFF;
                    MultiByteToWideChar(
                        wgs->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                        dbcstext, 1, wbuf+nlen, 1);
                }
                nlen++;
            }
            if (nlen <= 0)
                goto out;                /* Eeek! */

            ExtTextOutW(
                wgs->wintw_hdc, x + xoffset,
                y - wgs->font_height * (lattr == LATTR_BOT) + text_adjust,
                ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                &line_box, wbuf, nlen, (use_lpDx ? lpDx : NULL));
            if (wgs->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wgs->wintw_hdc, TRANSPARENT);
                ExtTextOutW(
                    wgs->wintw_hdc, x + xoffset - 1,
                    y - wgs->font_height * (lattr == LATTR_BOT) + text_adjust,
                    ETO_CLIPPED, &line_box, wbuf, nlen,
                    (use_lpDx ? lpDx : NULL));
            }

            lpDx[0] = -1;
        } else if (DIRECT_FONT(text[0])) {
            sgrowarray(cbuf, cbuflen, len);
            for (size_t i = 0; i < len; i++)
                cbuf[i] = text[i] & 0xFF;

            ExtTextOut(
                wgs->wintw_hdc, x + xoffset,
                y - wgs->font_height * (lattr == LATTR_BOT) + text_adjust,
                ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                &line_box, cbuf, len, (use_lpDx ? lpDx : NULL));
            if (wgs->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wgs->wintw_hdc, TRANSPARENT);

                /* GRR: This draws the character outside its box and
                 * can leave 'droppings' even with the clip box! I
                 * suppose I could loop it one character at a time ...
                 * yuk.
                 *
                 * Or ... I could do a test print with "W", and use +1
                 * or -1 for this shift depending on if the leftmost
                 * column is blank...
                 */
                ExtTextOut(
                    wgs->wintw_hdc, x + xoffset - 1,
                    y - wgs->font_height * (lattr == LATTR_BOT) + text_adjust,
                    ETO_CLIPPED, &line_box, cbuf, len,
                    (use_lpDx ? lpDx : NULL));
            }
        } else {
            /* And 'normal' unicode characters */
            sgrowarray(wbuf, wbuflen, len);
            for (int i = 0; i < len; i++)
                wbuf[i] = text[i];

            /* print Glyphs as they are, without Windows' Shaping*/
            general_textout(
                wgs, wgs->wintw_hdc, x + xoffset,
                y - wgs->font_height * (lattr==LATTR_BOT) + text_adjust,
                &line_box, wbuf, len, lpDx,
                opaque && !(attr & TATTR_COMBINING));

            /* And the shadow bold hack. */
            if (wgs->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wgs->wintw_hdc, TRANSPARENT);
                ExtTextOutW(
                    wgs->wintw_hdc, x + xoffset - 1,
                    y - wgs->font_height * (lattr == LATTR_BOT) + text_adjust,
                    ETO_CLIPPED, &line_box, wbuf, len,
                    (use_lpDx ? lpDx : NULL));
            }
        }

        /*
         * If we're looping round again, stop erasing the background
         * rectangle.
         */
        SetBkMode(wgs->wintw_hdc, TRANSPARENT);
        opaque = false;
    }

    if (lattr != LATTR_TOP && 
        (force_manual_underline || (wgs->und_mode == UND_LINE &&
                                    (attr & ATTR_UNDER))))
        draw_horizontal_line_on_text(wgs, wgs->descent, lattr, line_box, fg);

    if (attr & ATTR_STRIKE)
        draw_horizontal_line_on_text(wgs, wgs->font_strikethrough_y, lattr,
                                     line_box, fg);

  out:
    sfree(lpDx);
    sfree(wbuf);
    sfree(cbuf);
}

/*
 * Wrapper that handles combining characters.
 */
static void wintw_draw_text(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (attr & TATTR_COMBINING) {
        unsigned long a = 0;
        int len0 = 1;
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
        if (len >= 2 && IS_SURROGATE_PAIR(text[0], text[1]))
            len0 = 2;
        if (len-len0 >= 1 && IS_LOW_VARSEL(text[len0])) {
            attr &= ~TATTR_COMBINING;
            do_text_internal(wgs, x, y, text, len0+1, attr, lattr, truecolour);
            text += len0+1;
            len -= len0+1;
            a = TATTR_COMBINING;
        } else if (len-len0 >= 2 && IS_HIGH_VARSEL(text[len0], text[len0+1])) {
            attr &= ~TATTR_COMBINING;
            do_text_internal(wgs, x, y, text, len0+2, attr, lattr, truecolour);
            text += len0+2;
            len -= len0+2;
            a = TATTR_COMBINING;
        } else {
            attr &= ~TATTR_COMBINING;
        }

        while (len--) {
            if (len >= 1 && IS_SURROGATE_PAIR(text[0], text[1])) {
                do_text_internal(wgs, x, y, text, 2, attr | a, lattr,
                                 truecolour);
                len--;
                text++;
            } else
                do_text_internal(wgs, x, y, text, 1, attr | a, lattr,
                                 truecolour);

            text++;
            a = TATTR_COMBINING;
        }
    } else
        do_text_internal(wgs, x, y, text, len, attr, lattr, truecolour);
}

static void wintw_draw_cursor(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    int fnt_width;
    int char_width;
    int ctype = wgs->cursor_type;

    lattr &= LATTR_MODE;

    if ((attr & TATTR_ACTCURS) &&
        (ctype == CURSOR_BLOCK || wgs->term->big_cursor)) {
        if (*text != UCSWIDE) {
            win_draw_text(tw, x, y, text, len, attr, lattr, truecolour);
            return;
        }
        ctype = CURSOR_VERTICAL_LINE;
        attr |= TATTR_RIGHTCURS;
    }

    fnt_width = char_width = wgs->font_width * (1 + (lattr != LATTR_NORM));
    if (attr & ATTR_WIDE)
        char_width *= 2;
    x *= fnt_width;
    y *= wgs->font_height;
    x += wgs->offset_width;
    y += wgs->offset_height;

    if ((attr & TATTR_PASCURS) &&
        (ctype == CURSOR_BLOCK || wgs->term->big_cursor)) {
        POINT pts[5];
        HPEN oldpen;
        pts[0].x = pts[1].x = pts[4].x = x;
        pts[2].x = pts[3].x = x + char_width - 1;
        pts[0].y = pts[3].y = pts[4].y = y;
        pts[1].y = pts[2].y = y + wgs->font_height - 1;
        oldpen = SelectObject(wgs->wintw_hdc,
                              CreatePen(PS_SOLID, 0, wgs->colours[261]));
        Polyline(wgs->wintw_hdc, pts, 5);
        oldpen = SelectObject(wgs->wintw_hdc, oldpen);
        DeleteObject(oldpen);
    } else if ((attr & (TATTR_ACTCURS | TATTR_PASCURS)) &&
               ctype != CURSOR_BLOCK) {
        int startx, starty, dx, dy, length, i;
        if (ctype == CURSOR_UNDERLINE) {
            startx = x;
            starty = y + wgs->descent;
            dx = 1;
            dy = 0;
            length = char_width;
        } else /* ctype == CURSOR_VERTICAL_LINE */ {
            int xadjust = 0;
            if (attr & TATTR_RIGHTCURS)
                xadjust = char_width - 1;
            startx = x + xadjust;
            starty = y;
            dx = 0;
            dy = 1;
            length = wgs->font_height;
        }
        if (attr & TATTR_ACTCURS) {
            HPEN oldpen;
            oldpen =
                SelectObject(wgs->wintw_hdc,
                             CreatePen(PS_SOLID, 0, wgs->colours[261]));
            MoveToEx(wgs->wintw_hdc, startx, starty, NULL);
            LineTo(wgs->wintw_hdc, startx + dx * length, starty + dy * length);
            oldpen = SelectObject(wgs->wintw_hdc, oldpen);
            DeleteObject(oldpen);
        } else {
            for (i = 0; i < length; i++) {
                if (i % 2 == 0) {
                    SetPixel(wgs->wintw_hdc, startx, starty,
                             wgs->colours[261]);
                }
                startx += dx;
                starty += dy;
            }
        }
    }
}

static void wintw_draw_trust_sigil(TermWin *tw, int x, int y)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);

    x *= wgs->font_width;
    y *= wgs->font_height;
    x += wgs->offset_width;
    y += wgs->offset_height;

    DrawIconEx(wgs->wintw_hdc, x, y, trust_icon,
               wgs->font_width * 2, wgs->font_height, 0, NULL, DI_NORMAL);
}

/* This function gets the actual width of a character in the normal font.
 */
static int wintw_char_width(TermWin *tw, int uc)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    int ibuf = 0;

    /* If the font max is the same as the font ave width then this
     * function is a no-op.
     */
    if (!wgs->font_dualwidth) return 1;

    switch (uc & CSET_MASK) {
      case CSET_ASCII:
        uc = wgs->ucsdata.unitab_line[uc & 0xFF];
        break;
      case CSET_LINEDRW:
        uc = wgs->ucsdata.unitab_xterm[uc & 0xFF];
        break;
      case CSET_SCOACS:
        uc = wgs->ucsdata.unitab_scoacs[uc & 0xFF];
        break;
    }
    if (DIRECT_FONT(uc)) {
        if (wgs->ucsdata.dbcs_screenfont) return 1;

        /* Speedup, I know of no font where ascii is the wrong width */
        if ((uc&~CSET_MASK) >= ' ' && (uc&~CSET_MASK)<= '~')
            return 1;

        if ( (uc & CSET_MASK) == CSET_ACP ) {
            SelectObject(wgs->wintw_hdc, wgs->fonts[FONT_NORMAL]);
        } else if ( (uc & CSET_MASK) == CSET_OEMCP ) {
            another_font(wgs, FONT_OEM);
            if (!wgs->fonts[FONT_OEM]) return 0;

            SelectObject(wgs->wintw_hdc, wgs->fonts[FONT_OEM]);
        } else
            return 0;

        if (GetCharWidth32(wgs->wintw_hdc, uc & ~CSET_MASK,
                           uc & ~CSET_MASK, &ibuf) != 1 &&
            GetCharWidth(wgs->wintw_hdc, uc & ~CSET_MASK,
                         uc & ~CSET_MASK, &ibuf) != 1)
            return 0;
    } else {
        /* Speedup, I know of no font where ascii is the wrong width */
        if (uc >= ' ' && uc <= '~') return 1;

        SelectObject(wgs->wintw_hdc, wgs->fonts[FONT_NORMAL]);
        if (GetCharWidth32W(wgs->wintw_hdc, uc, uc, &ibuf) == 1)
            /* Okay that one worked */ ;
        else if (GetCharWidthW(wgs->wintw_hdc, uc, uc, &ibuf) == 1)
            /* This should work on 9x too, but it's "less accurate" */ ;
        else
            return 0;
    }

    ibuf += wgs->font_width / 2 -1;
    ibuf /= wgs->font_width;

    return ibuf;
}

DECL_WINDOWS_FUNCTION(static, BOOL, FlashWindowEx, (PFLASHWINFO));
DECL_WINDOWS_FUNCTION(static, BOOL, ToUnicodeEx,
                      (UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL));
DECL_WINDOWS_FUNCTION(static, BOOL, PlaySoundW, (LPCWSTR, HMODULE, DWORD));
DECL_WINDOWS_FUNCTION(static, BOOL, PlaySoundA, (LPCSTR, HMODULE, DWORD));

static void init_winfuncs(void)
{
    HMODULE user32_module = load_system32_dll("user32.dll");
    HMODULE winmm_module = load_system32_dll("winmm.dll");
    HMODULE shcore_module = load_system32_dll("shcore.dll");
    GET_WINDOWS_FUNCTION(user32_module, FlashWindowEx);
    GET_WINDOWS_FUNCTION(user32_module, ToUnicodeEx);
    GET_WINDOWS_FUNCTION(winmm_module, PlaySoundW);
    GET_WINDOWS_FUNCTION(winmm_module, PlaySoundA);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetMonitorInfoA);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, MonitorFromPoint);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, MonitorFromWindow);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(shcore_module, GetDpiForMonitor);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetSystemMetricsForDpi);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, AdjustWindowRectExForDpi);
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
static int TranslateKey(WinGuiSeat *wgs, UINT message, WPARAM wParam,
                        LPARAM lParam, unsigned char *output)
{
    BYTE keystate[256];
    int scan, shift_state;
    bool left_alt = false, key_down;
    int r, i;
    unsigned char *p = output;
    int funky_type = conf_get_int(wgs->conf, CONF_funky_type);
    bool no_applic_k = conf_get_bool(wgs->conf, CONF_no_applic_k);
    bool ctrlaltkeys = conf_get_bool(wgs->conf, CONF_ctrlaltkeys);
    bool nethack_keypad = conf_get_bool(wgs->conf, CONF_nethack_keypad);
    char keypad_key = '\0';

    HKL kbd_layout = GetKeyboardLayout(0);

    r = GetKeyboardState(keystate);
    if (!r)
        memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
        {                              /* Tell us all about key events */
            static BYTE oldstate[256];
            static int first = 1;
            static int scan;
            int ch;
            if (first)
                memcpy(oldstate, keystate, sizeof(oldstate));
            first = 0;

            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
                debug("+");
            } else if ((HIWORD(lParam) & KF_UP)
                       && scan == (HIWORD(lParam) & 0xFF)) {
                debug(". U");
            } else {
                debug(".\n");
                if (wParam >= VK_F1 && wParam <= VK_F20)
                    debug("K_F%d", wParam + 1 - VK_F1);
                else
                    switch (wParam) {
                      case VK_SHIFT:
                        debug("SHIFT");
                        break;
                      case VK_CONTROL:
                        debug("CTRL");
                        break;
                      case VK_MENU:
                        debug("ALT");
                        break;
                      default:
                        debug("VK_%02x", wParam);
                    }
                if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
                    debug("*");
                debug(", S%02x", scan = (HIWORD(lParam) & 0xFF));

                ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
                if (ch >= ' ' && ch <= '~')
                    debug(", '%c'", ch);
                else if (ch)
                    debug(", $%02x", ch);

                if ((keystate[VK_SHIFT] & 0x80) != 0)
                    debug(", S");
                if ((keystate[VK_CONTROL] & 0x80) != 0)
                    debug(", C");
                if ((HIWORD(lParam) & KF_EXTENDED))
                    debug(", E");
                if ((HIWORD(lParam) & KF_UP))
                    debug(", U");
            }

            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
            else if ((HIWORD(lParam) & KF_UP))
                oldstate[wParam & 0xFF] ^= 0x80;
            else
                oldstate[wParam & 0xFF] ^= 0x81;

            for (ch = 0; ch < 256; ch++)
                if (oldstate[ch] != keystate[ch])
                    debug(", M%02x=%02x", ch, keystate[ch]);

            memcpy(oldstate, keystate, sizeof(oldstate));
        }
#endif

        if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
            keystate[VK_RMENU] = keystate[VK_MENU];
        }


        /* Nastiness with NUMLock - Shift-NUMLock is left alone though */
        if ((funky_type == FUNKY_VT400 ||
             (funky_type <= FUNKY_LINUX && wgs->term->app_keypad_keys &&
              !no_applic_k))
            && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

            wParam = VK_EXECUTE;

            /* UnToggle NUMLock */
            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
                keystate[VK_NUMLOCK] ^= 1;
        }

        /* And write back the 'adjusted' state */
        SetKeyboardState(keystate);
    }

    /* Disable Auto repeat if required */
    if (wgs->term->repeat_off &&
        (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
        return 0;

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
        left_alt = true;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
        if (ctrlaltkeys)
            keystate[VK_MENU] = 0;
        else {
            keystate[VK_RMENU] = 0x80;
            left_alt = false;
        }
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
        + ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!wgs->compose_state) {
        wgs->compose_keycode = 0x100;
        if (conf_get_bool(wgs->conf, CONF_compose_key)) {
            if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
                wgs->compose_keycode = wParam;
        }
        if (wParam == VK_APPS)
            wgs->compose_keycode = wParam;
    }

    if (wParam == wgs->compose_keycode) {
        if (wgs->compose_state == 0 &&
            (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
            wgs->compose_state = 1;
        else if (wgs->compose_state == 1 && (HIWORD(lParam) & KF_UP))
            wgs->compose_state = 2;
        else
            wgs->compose_state = 0;
    } else if (wgs->compose_state == 1 && wParam != VK_CONTROL)
        wgs->compose_state = 0;

    if (wgs->compose_state > 1 && left_alt)
        wgs->compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (wgs->term->app_keypad_keys && !no_applic_k
                     && funky_type != FUNKY_XTERM) ||
        funky_type == FUNKY_VT400 || nethack_keypad || wgs->compose_state) {
        if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
            int nParam = 0;
            switch (wParam) {
              case VK_INSERT:
                nParam = VK_NUMPAD0;
                break;
              case VK_END:
                nParam = VK_NUMPAD1;
                break;
              case VK_DOWN:
                nParam = VK_NUMPAD2;
                break;
              case VK_NEXT:
                nParam = VK_NUMPAD3;
                break;
              case VK_LEFT:
                nParam = VK_NUMPAD4;
                break;
              case VK_CLEAR:
                nParam = VK_NUMPAD5;
                break;
              case VK_RIGHT:
                nParam = VK_NUMPAD6;
                break;
              case VK_HOME:
                nParam = VK_NUMPAD7;
                break;
              case VK_UP:
                nParam = VK_NUMPAD8;
                break;
              case VK_PRIOR:
                nParam = VK_NUMPAD9;
                break;
              case VK_DELETE:
                nParam = VK_DECIMAL;
                break;
            }
            if (nParam) {
                if (keystate[VK_NUMLOCK] & 1)
                    shift_state |= 1;
                wParam = nParam;
            }
        }
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !wgs->compose_state) {
        /* Okay, prepare for most alts then ... */
        if (left_alt)
            *p++ = '\033';

        /* Lets see if it's a pattern we know all about ... */
        if (wParam == VK_PRIOR && shift_state == 1) {
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_PAGEUP, 0);
            return 0;
        }
        if (wParam == VK_PRIOR && shift_state == 3) { /* ctrl-shift-pageup */
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_TOP, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 3) { /* ctrl-shift-pagedown */
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_BOTTOM, 0);
            return 0;
        }

        if (wParam == VK_PRIOR && shift_state == 2) {
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_LINEUP, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 1) {
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 2) {
            SendMessage(wgs->term_hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
            return 0;
        }
        if ((wParam == VK_PRIOR || wParam == VK_NEXT) && shift_state == 3) {
            term_scroll_to_selection(wgs->term, (wParam == VK_PRIOR ? 0 : 1));
            return 0;
        }
        if (wParam == VK_INSERT && shift_state == 2) {
            switch (conf_get_int(wgs->conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(wgs->term, clips_system,
                                  lenof(clips_system));
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == VK_INSERT && shift_state == 1) {
            switch (conf_get_int(wgs->conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(wgs->term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(wgs->term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == 'C' && shift_state == 3) {
            switch (conf_get_int(wgs->conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(wgs->term, clips_system,
                                  lenof(clips_system));
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == 'V' && shift_state == 3) {
            switch (conf_get_int(wgs->conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(wgs->term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(wgs->term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
            return 0;
        }
        if (left_alt && wParam == VK_F4 &&
            conf_get_bool(wgs->conf, CONF_alt_f4)) {
            return -1;
        }
        if (left_alt && wParam == VK_SPACE &&
            conf_get_bool(wgs->conf, CONF_alt_space)) {
            SendMessage(wgs->term_hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
            return -1;
        }
        if (left_alt && wParam == VK_RETURN &&
            conf_get_bool(wgs->conf, CONF_fullscreenonaltenter) &&
            (conf_get_int(wgs->conf, CONF_resize_action) != RESIZE_DISABLED)) {
            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) != KF_REPEAT)
                flip_full_screen(wgs);
            return -1;
        }
        /* Control-Numlock for app-keypad mode switch */
        if (wParam == VK_PAUSE && shift_state == 2) {
            wgs->term->app_keypad_keys = !wgs->term->app_keypad_keys;
            return 0;
        }

        if (wParam == VK_BACK && shift_state == 0) {    /* Backspace */
            *p++ = (conf_get_bool(wgs->conf, CONF_bksp_is_delete) ?
                    0x7F : 0x08);
            *p++ = 0;
            return -2;
        }
        if (wParam == VK_BACK && shift_state == 1) {    /* Shift Backspace */
            /* We do the opposite of what is configured */
            *p++ = (conf_get_bool(wgs->conf, CONF_bksp_is_delete) ?
                    0x08 : 0x7F);
            *p++ = 0;
            return -2;
        }
        if (wParam == VK_TAB && shift_state == 1) {     /* Shift tab */
            *p++ = 0x1B;
            *p++ = '[';
            *p++ = 'Z';
            return p - output;
        }
        if (wParam == VK_SPACE && shift_state == 2) {   /* Ctrl-Space */
            *p++ = 0;
            return p - output;
        }
        if (wParam == VK_SPACE && shift_state == 3) {   /* Ctrl-Shift-Space */
            *p++ = 160;
            return p - output;
        }
        if (wParam == VK_CANCEL && shift_state == 2) {  /* Ctrl-Break */
            if (wgs->backend)
                backend_special(wgs->backend, SS_BRK, 0);
            return 0;
        }
        if (wParam == VK_PAUSE) {      /* Break/Pause */
            *p++ = 26;
            *p++ = 0;
            return -2;
        }
        /* Control-2 to Control-8 are special */
        if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
            *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
            return p - output;
        }
        if (shift_state == 2 && (wParam == 0xBD || wParam == 0xBF)) {
            *p++ = 0x1F;
            return p - output;
        }
        if (shift_state == 2 && (wParam == 0xDF || wParam == 0xDC)) {
            *p++ = 0x1C;
            return p - output;
        }
        if (shift_state == 3 && wParam == 0xDE) {
            *p++ = 0x1E;               /* Ctrl-~ == Ctrl-^ in xterm at least */
            return p - output;
        }

        switch (wParam) {
            bool consumed_alt;

          case VK_NUMPAD0: keypad_key = '0'; goto numeric_keypad;
          case VK_NUMPAD1: keypad_key = '1'; goto numeric_keypad;
          case VK_NUMPAD2: keypad_key = '2'; goto numeric_keypad;
          case VK_NUMPAD3: keypad_key = '3'; goto numeric_keypad;
          case VK_NUMPAD4: keypad_key = '4'; goto numeric_keypad;
          case VK_NUMPAD5: keypad_key = '5'; goto numeric_keypad;
          case VK_NUMPAD6: keypad_key = '6'; goto numeric_keypad;
          case VK_NUMPAD7: keypad_key = '7'; goto numeric_keypad;
          case VK_NUMPAD8: keypad_key = '8'; goto numeric_keypad;
          case VK_NUMPAD9: keypad_key = '9'; goto numeric_keypad;
          case VK_DECIMAL: keypad_key = '.'; goto numeric_keypad;
          case VK_ADD: keypad_key = '+'; goto numeric_keypad;
          case VK_SUBTRACT: keypad_key = '-'; goto numeric_keypad;
          case VK_MULTIPLY: keypad_key = '*'; goto numeric_keypad;
          case VK_DIVIDE: keypad_key = '/'; goto numeric_keypad;
          case VK_EXECUTE: keypad_key = 'G'; goto numeric_keypad;
            /* also the case for VK_RETURN below can sometimes come here */
          numeric_keypad:
            /* Left Alt overrides all numeric keypad usage to act as
             * numeric character code input */
            if (left_alt) {
                if (keypad_key >= '0' && keypad_key <= '9')
                    wgs->alt_numberpad_accumulator =
                        wgs->alt_numberpad_accumulator * 10 + keypad_key - '0';
                else
                    wgs->alt_numberpad_accumulator = 0;
                break;
            }

            {
                int nchars = format_numeric_keypad_key(
                    (char *)p, wgs->term, keypad_key,
                    shift_state & 1, shift_state & 2);
                if (!nchars) {
                    /*
                     * If we didn't get an escape sequence out of the
                     * numeric keypad key, then that must be because
                     * we're in Num Lock mode without application
                     * keypad enabled. In that situation we leave this
                     * keypress to the ToUnicode/ToAsciiEx handler
                     * below, which will translate it according to the
                     * appropriate keypad layout (e.g. so that what a
                     * Brit thinks of as keypad '.' can become ',' in
                     * the German layout).
                     *
                     * An exception is the keypad Return key: if we
                     * didn't get an escape sequence for that, we
                     * treat it like ordinary Return, taking into
                     * account Telnet special new line codes and
                     * config options.
                     */
                    if (keypad_key == '\r')
                        goto ordinary_return_key;
                    break;
                }

                p += nchars;
                return p - output;
            }

            int fkey_number;
          case VK_F1: fkey_number = 1; goto numbered_function_key;
          case VK_F2: fkey_number = 2; goto numbered_function_key;
          case VK_F3: fkey_number = 3; goto numbered_function_key;
          case VK_F4: fkey_number = 4; goto numbered_function_key;
          case VK_F5: fkey_number = 5; goto numbered_function_key;
          case VK_F6: fkey_number = 6; goto numbered_function_key;
          case VK_F7: fkey_number = 7; goto numbered_function_key;
          case VK_F8: fkey_number = 8; goto numbered_function_key;
          case VK_F9: fkey_number = 9; goto numbered_function_key;
          case VK_F10: fkey_number = 10; goto numbered_function_key;
          case VK_F11: fkey_number = 11; goto numbered_function_key;
          case VK_F12: fkey_number = 12; goto numbered_function_key;
          case VK_F13: fkey_number = 13; goto numbered_function_key;
          case VK_F14: fkey_number = 14; goto numbered_function_key;
          case VK_F15: fkey_number = 15; goto numbered_function_key;
          case VK_F16: fkey_number = 16; goto numbered_function_key;
          case VK_F17: fkey_number = 17; goto numbered_function_key;
          case VK_F18: fkey_number = 18; goto numbered_function_key;
          case VK_F19: fkey_number = 19; goto numbered_function_key;
          case VK_F20: fkey_number = 20; goto numbered_function_key;
          numbered_function_key:
            consumed_alt = false;
            p += format_function_key((char *)p, wgs->term, fkey_number,
                                     shift_state & 1, shift_state & 2,
                                     left_alt, &consumed_alt);
            if (consumed_alt) {
                /* supersedes the usual prefixing of Esc */
                p -= 1;
                memmove(output, output + 1, p - output);
            }
            return p - output;

            SmallKeypadKey sk_key;
          case VK_HOME: sk_key = SKK_HOME; goto small_keypad_key;
          case VK_END: sk_key = SKK_END; goto small_keypad_key;
          case VK_INSERT: sk_key = SKK_INSERT; goto small_keypad_key;
          case VK_DELETE: sk_key = SKK_DELETE; goto small_keypad_key;
          case VK_PRIOR: sk_key = SKK_PGUP; goto small_keypad_key;
          case VK_NEXT: sk_key = SKK_PGDN; goto small_keypad_key;
          small_keypad_key:
            /* These keys don't generate terminal input with Ctrl */
            if (shift_state & 2)
                break;

            consumed_alt = false;
            p += format_small_keypad_key((char *)p, wgs->term, sk_key,
                                         shift_state & 1, shift_state & 2,
                                         left_alt, &consumed_alt);
            if (consumed_alt) {
                /* supersedes the usual prefixing of Esc */
                p -= 1;
                memmove(output, output + 1, p - output);
            }
            return p - output;

            char xkey;
          case VK_UP: xkey = 'A'; goto arrow_key;
          case VK_DOWN: xkey = 'B'; goto arrow_key;
          case VK_RIGHT: xkey = 'C'; goto arrow_key;
          case VK_LEFT: xkey = 'D'; goto arrow_key;
          case VK_CLEAR: xkey = 'G'; goto arrow_key; /* close enough */
          arrow_key:
            consumed_alt = false;
            p += format_arrow_key((char *)p, wgs->term, xkey, shift_state & 1,
                                  shift_state & 2, left_alt, &consumed_alt);
            if (consumed_alt) {
                /* supersedes the usual prefixing of Esc */
                p -= 1;
                memmove(output, output + 1, p - output);
            }
            return p - output;

          case VK_RETURN:
            if (HIWORD(lParam) & KF_EXTENDED) {
                keypad_key = '\r';
                goto numeric_keypad;
            }
          ordinary_return_key:
            if (shift_state == 0 && wgs->term->cr_lf_return) {
                *p++ = '\r';
                *p++ = '\n';
                return p - output;
            } else {
                *p++ = 0x0D;
                *p++ = 0;
                return -2;
            }
        }
    }

    /* Okay we've done everything interesting; let windows deal with
     * the boring stuff */
    {
        bool capsOn = false;

        /* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
        if (keystate[VK_CAPITAL] != 0 &&
            conf_get_bool(wgs->conf, CONF_xlat_capslockcyr)) {
            capsOn = !left_alt;
            keystate[VK_CAPITAL] = 0;
        }

        wchar_t keys_unicode[3];

        /* XXX how do we know what the max size of the keys array should
         * be is? There's indication on MS' website of an Inquire/InquireEx
         * functioning returning a KBINFO structure which tells us. */
        if (osPlatformId == VER_PLATFORM_WIN32_NT && p_ToUnicodeEx) {
            r = p_ToUnicodeEx(wParam, scan, keystate, keys_unicode,
                              lenof(keys_unicode), 0, kbd_layout);
        } else {
            /* XXX 'keys' parameter is declared in MSDN documentation as
             * 'LPWORD lpChar'.
             * The experience of a French user indicates that on
             * Win98, WORD[] should be passed in, but on Win2K, it should
             * be BYTE[]. German WinXP and my Win2K with "US International"
             * driver corroborate this.
             * Experimentally I've conditionalised the behaviour on the
             * Win9x/NT split, but I suspect it's worse than that.
             * See wishlist item `win-dead-keys' for more horrible detail
             * and speculations. */
            int i;
            WORD keys[3];
            BYTE keysb[3];
            r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
            if (r > 0) {
                for (i = 0; i < r; i++) {
                    keysb[i] = (BYTE)keys[i];
                }
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)keysb, r,
                                    keys_unicode, lenof(keys_unicode));
            }
        }
#ifdef SHOW_TOASCII_RESULT
        if (r == 1 && !key_down) {
            if (wgs->alt_numberpad_accumulator) {
                if (in_utf(term) || ucsdata.dbcs_screenfont)
                    debug(", (U+%04x)", wgs->alt_numberpad_accumulator);
                else
                    debug(", LCH(%d)", wgs->alt_numberpad_accumulator);
            } else {
                debug(", ACH(%d)", keys_unicode[0]);
            }
        } else if (r > 0) {
            int r1;
            debug(", ASC(");
            for (r1 = 0; r1 < r; r1++) {
                debug("%s%d", r1 ? "," : "", keys_unicode[r1]);
            }
            debug(")");
        }
#endif
        if (r > 0) {
            WCHAR keybuf;

            p = output;
            for (i = 0; i < r; i++) {
                wchar_t wch = keys_unicode[i];

                if (wgs->compose_state == 2 && wch >= ' ' && wch < 0x80) {
                    wgs->compose_char = wch;
                    wgs->compose_state++;
                    continue;
                }
                if (wgs->compose_state == 3 && wch >= ' ' && wch < 0x80) {
                    int nc;
                    wgs->compose_state = 0;

                    if ((nc = check_compose(wgs->compose_char, wch)) == -1) {
                        MessageBeep(MB_ICONHAND);
                        return 0;
                    }
                    keybuf = nc;
                    term_keyinputw(wgs->term, &keybuf, 1);
                    continue;
                }

                wgs->compose_state = 0;

                if (!key_down) {
                    if (wgs->alt_numberpad_accumulator) {
                        if (in_utf(wgs->term) ||
                            wgs->ucsdata.dbcs_screenfont) {
                            keybuf = wgs->alt_numberpad_accumulator;
                            term_keyinputw(wgs->term, &keybuf, 1);
                        } else {
                            char ch = (char) wgs->alt_numberpad_accumulator;
                            /*
                             * We need not bother about stdin
                             * backlogs here, because in GUI PuTTY
                             * we can't do anything about it
                             * anyway; there's no means of asking
                             * Windows to hold off on KEYDOWN
                             * messages. We _have_ to buffer
                             * everything we're sent.
                             */
                            term_keyinput(wgs->term, -1, &ch, 1);
                        }
                        wgs->alt_numberpad_accumulator = 0;
                    } else {
                        term_keyinputw(wgs->term, &wch, 1);
                    }
                } else {
                    if (capsOn && wch < 0x80) {
                        WCHAR cbuf[2];
                        cbuf[0] = 27;
                        cbuf[1] = xlat_uskbd2cyrllic(wch);
                        term_keyinputw(
                            wgs->term, cbuf+!left_alt, 1+!!left_alt);
                    } else {
                        WCHAR cbuf[2];
                        cbuf[0] = '\033';
                        cbuf[1] = wch;
                        term_keyinputw(
                            wgs->term, cbuf +!left_alt, 1+!!left_alt);
                    }
                }
                show_mouseptr(wgs, false);
            }

            return p - output;
        }
    }

    /*
     * ALT alone may or may not want to bring up the System menu.
     * If it's not meant to, we return 0 on presses or releases of
     * ALT, to show that we've swallowed the keystroke. Otherwise
     * we return -1, which means Windows will give the keystroke
     * its default handling (i.e. bring up the System menu).
     */
    if (wParam == VK_MENU && !conf_get_bool(wgs->conf, CONF_alt_only))
        return 0;

    return -1;
}

static void wintw_set_title(TermWin *tw, const char *title, int codepage)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    wchar_t *new_window_name = dup_mb_to_wc(codepage, title);
    if (!wcscmp(new_window_name, wgs->window_name)) {
        sfree(new_window_name);
        return;
    }
    sfree(wgs->window_name);
    wgs->window_name = new_window_name;
    if (conf_get_bool(wgs->conf, CONF_win_name_always) ||
        !IsIconic(wgs->term_hwnd))
        sw_SetWindowText(wgs->term_hwnd, wgs->window_name);
}

static void wintw_set_icon_title(TermWin *tw, const char *title, int codepage)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    wchar_t *new_icon_name = dup_mb_to_wc(codepage, title);
    if (!wcscmp(new_icon_name, wgs->icon_name)) {
        sfree(new_icon_name);
        return;
    }
    sfree(wgs->icon_name);
    wgs->icon_name = new_icon_name;
    if (!conf_get_bool(wgs->conf, CONF_win_name_always) &&
        IsIconic(wgs->term_hwnd))
        sw_SetWindowText(wgs->term_hwnd, wgs->icon_name);
}

static void wintw_set_scrollbar(TermWin *tw, int total, int start, int page)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    SCROLLINFO si;

    if (!conf_get_bool(wgs->conf, is_full_screen(wgs) ?
                       CONF_scrollbar_in_fullscreen : CONF_scrollbar))
        return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (wgs->term_hwnd)
        SetScrollInfo(wgs->term_hwnd, SB_VERT, &si, true);
}

static bool wintw_setup_draw_ctx(TermWin *tw)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    assert(!wgs->wintw_hdc);
    wgs->wintw_hdc = make_hdc(wgs);
    return wgs->wintw_hdc != NULL;
}

static void wintw_free_draw_ctx(TermWin *tw)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    assert(wgs->wintw_hdc);
    free_hdc(wgs, wgs->wintw_hdc);
    wgs->wintw_hdc = NULL;
}

/*
 * Set up the colour palette.
 */
static void init_palette(WinGuiSeat *wgs)
{
    wgs->pal = NULL;
    wgs->logpal = snew_plus(
        LOGPALETTE, (OSC4_NCOLOURS - 1) * sizeof(PALETTEENTRY));
    wgs->logpal->palVersion = 0x300;
    wgs->logpal->palNumEntries = OSC4_NCOLOURS;
    for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
        wgs->logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
}

static void wintw_palette_set(TermWin *tw, unsigned start,
                              unsigned ncolours, const rgb *colours_in)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    assert(start <= OSC4_NCOLOURS);
    assert(ncolours <= OSC4_NCOLOURS - start);

    for (unsigned i = 0; i < ncolours; i++) {
        const rgb *in = &colours_in[i];
        PALETTEENTRY *out = &wgs->logpal->palPalEntry[i + start];
        out->peRed = in->r;
        out->peGreen = in->g;
        out->peBlue = in->b;
        wgs->colours[i + start] =
            RGB(in->r, in->g, in->b) ^ wgs->colorref_modifier;
    }

    bool got_new_palette = false;

    if (!wgs->tried_pal && conf_get_bool(wgs->conf, CONF_try_palette)) {
        HDC hdc = GetDC(wgs->term_hwnd);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            wgs->pal = CreatePalette(wgs->logpal);
            if (wgs->pal) {
                SelectPalette(hdc, wgs->pal, false);
                RealizePalette(hdc);
                SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);

                /* Convert all RGB() values in colours[] into PALETTERGB(),
                 * and ensure we stick to that later */
                wgs->colorref_modifier = PALETTERGB(0, 0, 0) ^ RGB(0, 0, 0);
                for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
                    wgs->colours[i] ^= wgs->colorref_modifier;

                /* Inhibit the SetPaletteEntries call below */
                got_new_palette = true;
            }
        }
        ReleaseDC(wgs->term_hwnd, hdc);
        wgs->tried_pal = true;
    }

    if (wgs->pal && !got_new_palette) {
        /* We already had a palette, so replace the changed colours in the
         * existing one. */
        SetPaletteEntries(wgs->pal, start, ncolours,
                          wgs->logpal->palPalEntry + start);

        HDC hdc = make_hdc(wgs);
        UnrealizeObject(wgs->pal);
        RealizePalette(hdc);
        free_hdc(wgs, hdc);
    }

    if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
        /* If Default Background changes, we need to ensure any space between
         * the text area and the window border is redrawn. */
        InvalidateRect(wgs->term_hwnd, NULL, true);
    }
}

void write_aclip(HWND hwnd, int clipboard, char *data, int len)
{
    HGLOBAL clipdata;
    void *lock;

    if (clipboard != CLIP_SYSTEM)
        return;

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
        return;
    lock = GlobalLock(clipdata);
    if (!lock)
        return;
    memcpy(lock, data, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, clipdata);
        CloseClipboard();
    } else
        GlobalFree(clipdata);
}

typedef struct _rgbindex {
    int index;
    COLORREF ref;
} rgbindex;

int cmpCOLORREF(void *va, void *vb)
{
    COLORREF a = ((rgbindex *)va)->ref;
    COLORREF b = ((rgbindex *)vb)->ref;
    return (a < b) ? -1 : (a > b) ? +1 : 0;
}

/*
 * Note: unlike write_aclip() this will not append a nul.
 */
static void wintw_clip_write(
    TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    HGLOBAL clipdata, clipdata2, clipdata3;
    int len2;
    void *lock, *lock2, *lock3;

    if (clipboard != CLIP_SYSTEM)
        return;

    len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, NULL, NULL);

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE,
                           len * sizeof(wchar_t));
    clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

    if (!clipdata || !clipdata2) {
        if (clipdata)
            GlobalFree(clipdata);
        if (clipdata2)
            GlobalFree(clipdata2);
        return;
    }
    if (!(lock = GlobalLock(clipdata))) {
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
        return;
    }
    if (!(lock2 = GlobalLock(clipdata2))) {
        GlobalUnlock(clipdata);
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
        return;
    }

    memcpy(lock, data, len * sizeof(wchar_t));
    WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, NULL, NULL);

    if (conf_get_bool(wgs->conf, CONF_rtf_paste)) {
        wchar_t unitab[256];
        strbuf *rtf = strbuf_new();
        unsigned char *tdata = (unsigned char *)lock2;
        wchar_t *udata = (wchar_t *)lock;
        int uindex = 0, tindex = 0;
        int multilen, blen, alen, i;
        char before[16], after[4];
        int fgcolour,  lastfgcolour  = -1;
        int bgcolour,  lastbgcolour  = -1;
        COLORREF fg,   lastfg = -1;
        COLORREF bg,   lastbg = -1;
        int attrBold,  lastAttrBold  = 0;
        int attrUnder, lastAttrUnder = 0;
        int palette[OSC4_NCOLOURS];
        int numcolours;
        tree234 *rgbtree = NULL;
        FontSpec *font = conf_get_fontspec(wgs->conf, CONF_font);

        get_unitab(CP_ACP, unitab, 0);

        put_fmt(
            rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
            font->name, font->height*2);

        /*
         * Add colour palette
         * {\colortbl ;\red255\green0\blue0;\red0\green0\blue128;}
         */

        /*
         * First - Determine all colours in use
         *    o  Foregound and background colours share the same palette
         */
        if (attr) {
            memset(palette, 0, sizeof(palette));
            for (i = 0; i < (len-1); i++) {
                fgcolour = ((attr[i] & ATTR_FGMASK) >> ATTR_FGSHIFT);
                bgcolour = ((attr[i] & ATTR_BGMASK) >> ATTR_BGSHIFT);

                if (attr[i] & ATTR_REVERSE) {
                    int tmpcolour = fgcolour;   /* Swap foreground and background */
                    fgcolour = bgcolour;
                    bgcolour = tmpcolour;
                }

                if (wgs->bold_colours && (attr[i] & ATTR_BOLD)) {
                    if (fgcolour  <   8)        /* ANSI colours */
                        fgcolour +=   8;
                    else if (fgcolour >= 256)   /* Default colours */
                        fgcolour ++;
                }

                if ((attr[i] & ATTR_BLINK)) {
                    if (bgcolour  <   8)        /* ANSI colours */
                        bgcolour +=   8;
                    else if (bgcolour >= 256)   /* Default colours */
                        bgcolour ++;
                }

                palette[fgcolour]++;
                palette[bgcolour]++;
            }

            if (truecolour) {
                rgbtree = newtree234(cmpCOLORREF);
                for (i = 0; i < (len-1); i++) {
                    if (truecolour[i].fg.enabled) {
                        rgbindex *rgbp = snew(rgbindex);
                        rgbp->ref = RGB(truecolour[i].fg.r,
                                        truecolour[i].fg.g,
                                        truecolour[i].fg.b);
                        if (add234(rgbtree, rgbp) != rgbp)
                            sfree(rgbp);
                    }
                    if (truecolour[i].bg.enabled) {
                        rgbindex *rgbp = snew(rgbindex);
                        rgbp->ref = RGB(truecolour[i].bg.r,
                                        truecolour[i].bg.g,
                                        truecolour[i].bg.b);
                        if (add234(rgbtree, rgbp) != rgbp)
                            sfree(rgbp);
                    }
                }
            }

            /*
             * Next - Create a reduced palette
             */
            numcolours = 0;
            for (i = 0; i < OSC4_NCOLOURS; i++) {
                if (palette[i] != 0)
                    palette[i]  = ++numcolours;
            }

            if (rgbtree) {
                rgbindex *rgbp;
                for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
                    rgbp->index = ++numcolours;
            }

            /*
             * Finally - Write the colour table
             */
            put_datapl(rtf, PTRLEN_LITERAL("{\\colortbl ;"));

            for (i = 0; i < OSC4_NCOLOURS; i++) {
                if (palette[i] != 0) {
                    const PALETTEENTRY *pe = &wgs->logpal->palPalEntry[i];
                    put_fmt(rtf, "\\red%d\\green%d\\blue%d;",
                            pe->peRed, pe->peGreen, pe->peBlue);
                }
            }
            if (rgbtree) {
                rgbindex *rgbp;
                for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
                    put_fmt(rtf, "\\red%d\\green%d\\blue%d;",
                            GetRValue(rgbp->ref), GetGValue(rgbp->ref),
                            GetBValue(rgbp->ref));
            }
            put_datapl(rtf, PTRLEN_LITERAL("}"));
        }

        /*
         * We want to construct a piece of RTF that specifies the
         * same Unicode text. To do this we will read back in
         * parallel from the Unicode data in `udata' and the
         * non-Unicode data in `tdata'. For each character in
         * `tdata' which becomes the right thing in `udata' when
         * looked up in `unitab', we just copy straight over from
         * tdata. For each one that doesn't, we must WCToMB it
         * individually and produce a \u escape sequence.
         *
         * It would probably be more robust to just bite the bullet
         * and WCToMB each individual Unicode character one by one,
         * then MBToWC each one back to see if it was an accurate
         * translation; but that strikes me as a horrifying number
         * of Windows API calls so I want to see if this faster way
         * will work. If it screws up badly we can always revert to
         * the simple and slow way.
         */
        while (tindex < len2 && uindex < len &&
               tdata[tindex] && udata[uindex]) {
            if (tindex + 1 < len2 &&
                tdata[tindex] == '\r' &&
                tdata[tindex+1] == '\n') {
                tindex++;
                uindex++;
            }

            /*
             * Set text attributes
             */
            if (attr) {
                /*
                 * Determine foreground and background colours
                 */
                if (truecolour && truecolour[tindex].fg.enabled) {
                    fgcolour = -1;
                    fg = RGB(truecolour[tindex].fg.r,
                             truecolour[tindex].fg.g,
                             truecolour[tindex].fg.b);
                } else {
                    fgcolour = ((attr[tindex] & ATTR_FGMASK) >> ATTR_FGSHIFT);
                    fg = -1;
                }

                if (truecolour && truecolour[tindex].bg.enabled) {
                    bgcolour = -1;
                    bg = RGB(truecolour[tindex].bg.r,
                             truecolour[tindex].bg.g,
                             truecolour[tindex].bg.b);
                } else {
                    bgcolour = ((attr[tindex] & ATTR_BGMASK) >> ATTR_BGSHIFT);
                    bg = -1;
                }

                if (attr[tindex] & ATTR_REVERSE) {
                    int tmpcolour = fgcolour;       /* Swap foreground and background */
                    fgcolour = bgcolour;
                    bgcolour = tmpcolour;

                    COLORREF tmpref = fg;
                    fg = bg;
                    bg = tmpref;
                }

                if (wgs->bold_colours && (attr[tindex] & ATTR_BOLD) &&
                    (fgcolour >= 0)) {
                    if (fgcolour  <   8)            /* ANSI colours */
                        fgcolour +=   8;
                    else if (fgcolour >= 256)       /* Default colours */
                        fgcolour ++;
                }

                if ((attr[tindex] & ATTR_BLINK) && (bgcolour >= 0)) {
                    if (bgcolour  <   8)            /* ANSI colours */
                        bgcolour +=   8;
                    else if (bgcolour >= 256)       /* Default colours */
                        bgcolour ++;
                }

                /*
                 * Collect other attributes
                 */
                if (wgs->bold_font_mode != BOLD_NONE)
                    attrBold  = attr[tindex] & ATTR_BOLD;
                else
                    attrBold  = 0;

                attrUnder = attr[tindex] & ATTR_UNDER;

                /*
                 * Reverse video
                 *   o  If video isn't reversed, ignore colour attributes for default foregound
                 *      or background.
                 *   o  Special case where bolded text is displayed using the default foregound
                 *      and background colours - force to bolded RTF.
                 */
                if (!(attr[tindex] & ATTR_REVERSE)) {
                    if (bgcolour >= 256)            /* Default color */
                        bgcolour  = -1;             /* No coloring */

                    if (fgcolour >= 256) {          /* Default colour */
                        if (wgs->bold_colours && (fgcolour & 1) &&
                            bgcolour == -1)
                            attrBold = ATTR_BOLD;   /* Emphasize text with bold attribute */

                        fgcolour  = -1;             /* No coloring */
                    }
                }

                /*
                 * Write RTF text attributes
                 */
                if ((lastfgcolour != fgcolour) || (lastfg != fg)) {
                    lastfgcolour  = fgcolour;
                    lastfg        = fg;
                    if (fg == -1) {
                        put_fmt(rtf, "\\cf%d ",
                                (fgcolour >= 0) ? palette[fgcolour] : 0);
                    } else {
                        rgbindex rgb, *rgbp;
                        rgb.ref = fg;
                        if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
                            put_fmt(rtf, "\\cf%d ", rgbp->index);
                    }
                }

                if ((lastbgcolour != bgcolour) || (lastbg != bg)) {
                    lastbgcolour  = bgcolour;
                    lastbg        = bg;
                    if (bg == -1)
                        put_fmt(rtf, "\\highlight%d ",
                                (bgcolour >= 0) ? palette[bgcolour] : 0);
                    else {
                        rgbindex rgb, *rgbp;
                        rgb.ref = bg;
                        if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
                            put_fmt(rtf, "\\highlight%d ", rgbp->index);
                    }
                }

                if (lastAttrBold != attrBold) {
                    lastAttrBold  = attrBold;
                    put_datapl(rtf, attrBold ?
                               PTRLEN_LITERAL("\\b ") :
                               PTRLEN_LITERAL("\\b0 "));
                }

                if (lastAttrUnder != attrUnder) {
                    lastAttrUnder  = attrUnder;
                    put_datapl(rtf, attrUnder ?
                               PTRLEN_LITERAL("\\ul ") :
                               PTRLEN_LITERAL("\\ulnone "));
                }
            }

            if (unitab[tdata[tindex]] == udata[uindex]) {
                multilen = 1;
                before[0] = '\0';
                after[0] = '\0';
                blen = alen = 0;
            } else {
                multilen = WideCharToMultiByte(CP_ACP, 0, unitab+uindex, 1,
                                               NULL, 0, NULL, NULL);
                if (multilen != 1) {
                    blen = sprintf(before, "{\\uc%d\\u%d", (int)multilen,
                                   (int)udata[uindex]);
                    alen = 1; strcpy(after, "}");
                } else {
                    blen = sprintf(before, "\\u%d", (int)udata[uindex]);
                    alen = 0; after[0] = '\0';
                }
            }
            assert(tindex + multilen <= len2);

            put_data(rtf, before, blen);
            for (i = 0; i < multilen; i++) {
                if (tdata[tindex+i] == '\\' ||
                    tdata[tindex+i] == '{' ||
                    tdata[tindex+i] == '}') {
                    put_byte(rtf, '\\');
                    put_byte(rtf, tdata[tindex+i]);
                } else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A) {
                    put_datapl(rtf, PTRLEN_LITERAL("\\par\r\n"));
                } else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20) {
                    put_fmt(rtf, "\\'%02x", tdata[tindex+i]);
                } else {
                    put_byte(rtf, tdata[tindex+i]);
                }
            }
            put_data(rtf, after, alen);

            tindex += multilen;
            uindex++;
        }

        put_datapl(rtf, PTRLEN_LITERAL("}\0\0")); /* Terminate RTF stream */

        clipdata3 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, rtf->len);
        if (clipdata3 && (lock3 = GlobalLock(clipdata3)) != NULL) {
            memcpy(lock3, rtf->u, rtf->len);
            GlobalUnlock(clipdata3);
        }
        strbuf_free(rtf);

        if (rgbtree) {
            rgbindex *rgbp;
            while ((rgbp = delpos234(rgbtree, 0)) != NULL)
                sfree(rgbp);
            freetree234(rgbtree);
        }
    } else
        clipdata3 = NULL;

    GlobalUnlock(clipdata);
    GlobalUnlock(clipdata2);

    if (!must_deselect)
        SendMessage(wgs->term_hwnd, WM_IGNORE_CLIP, true, 0);

    if (OpenClipboard(wgs->term_hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, clipdata);
        SetClipboardData(CF_TEXT, clipdata2);
        if (clipdata3)
            SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
        CloseClipboard();
    } else {
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
    }

    if (!must_deselect)
        SendMessage(wgs->term_hwnd, WM_IGNORE_CLIP, false, 0);
}

static DWORD WINAPI clipboard_read_threadfunc(void *param)
{
    HWND hwnd = (HWND)param;
    HGLOBAL clipdata;

    if (OpenClipboard(NULL)) {
        if ((clipdata = GetClipboardData(CF_UNICODETEXT))) {
            SendMessage(hwnd, WM_GOT_CLIPDATA,
                        (WPARAM)true, (LPARAM)clipdata);
        } else if ((clipdata = GetClipboardData(CF_TEXT))) {
            SendMessage(hwnd, WM_GOT_CLIPDATA,
                        (WPARAM)false, (LPARAM)clipdata);
        }
        CloseClipboard();
    }

    return 0;
}

static void process_clipdata(WinGuiSeat *wgs, HGLOBAL clipdata, bool unicode)
{
    wchar_t *clipboard_contents = NULL;
    size_t clipboard_length = 0;

    if (unicode) {
        wchar_t *p = GlobalLock(clipdata);
        wchar_t *p2;

        if (p) {
            /* Unwilling to rely on Windows having wcslen() */
            for (p2 = p; *p2; p2++);
            clipboard_length = p2 - p;
            clipboard_contents = snewn(clipboard_length + 1, wchar_t);
            memcpy(clipboard_contents, p, clipboard_length * sizeof(wchar_t));
            clipboard_contents[clipboard_length] = L'\0';
            term_do_paste(wgs->term, clipboard_contents, clipboard_length);
        }
    } else {
        char *s = GlobalLock(clipdata);
        int i;

        if (s) {
            i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
            clipboard_contents = snewn(i, wchar_t);
            MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1,
                                clipboard_contents, i);
            clipboard_length = i - 1;
            clipboard_contents[clipboard_length] = L'\0';
            term_do_paste(wgs->term, clipboard_contents, clipboard_length);
        }
    }

    sfree(clipboard_contents);
}

static void wintw_clip_request_paste(TermWin *tw, int clipboard)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    assert(clipboard == CLIP_SYSTEM);

    /*
     * I always thought pasting was synchronous in Windows; the
     * clipboard access functions certainly _look_ synchronous,
     * unlike the X ones. But in fact it seems that in some
     * situations the contents of the clipboard might not be
     * immediately available, and the clipboard-reading functions
     * may block. This leads to trouble if the application
     * delivering the clipboard data has to get hold of it by -
     * for example - talking over a network connection which is
     * forwarded through this very PuTTY.
     *
     * Hence, we spawn a subthread to read the clipboard, and do
     * our paste when it's finished. The thread will send a
     * message back to our main window when it terminates, and
     * that tells us it's OK to paste.
     */
    DWORD in_threadid; /* required for Win9x */
    HANDLE hThread = CreateThread(NULL, 0, clipboard_read_threadfunc,
                                  wgs->term_hwnd, 0, &in_threadid);
    if (hThread)
        CloseHandle(hThread);          /* we don't need the thread handle */
}

/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(NULL, true);
    title = dupprintf("%s Fatal Error", appname);
    MessageBox(find_window_for_msgbox(), message, title,
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    cleanup_exit(1);
}

/*
 * Print a message box and don't close the connection.
 */
void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(NULL, true);
    title = dupprintf("%s Error", appname);
    MessageBox(find_window_for_msgbox(), message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
}

static bool flash_window_ex(WinGuiSeat *wgs, DWORD dwFlags,
                            UINT uCount, DWORD dwTimeout)
{
    if (p_FlashWindowEx) {
        FLASHWINFO fi;
        fi.cbSize = sizeof(fi);
        fi.hwnd = wgs->term_hwnd;
        fi.dwFlags = dwFlags;
        fi.uCount = uCount;
        fi.dwTimeout = dwTimeout;
        return (*p_FlashWindowEx)(&fi);
    }
    else
        return false; /* shrug */
}

/*
 * Timer for platforms where we must maintain window flashing manually
 * (e.g., Win95).
 */
static void flash_window_timer(void *vctx, unsigned long now)
{
    WinGuiSeat *wgs = (WinGuiSeat *)vctx;
    if (wgs->flashing && now == wgs->next_flash) {
        flash_window(wgs, 1);
    }
}

/*
 * Manage window caption / taskbar flashing, if enabled.
 * 0 = stop, 1 = maintain, 2 = start
 */
static void flash_window(WinGuiSeat *wgs, int mode)
{
    int beep_ind = conf_get_int(wgs->conf, CONF_beep_ind);
    if ((mode == 0) || (beep_ind == B_IND_DISABLED)) {
        /* stop */
        if (wgs->flashing) {
            wgs->flashing = false;
            if (p_FlashWindowEx)
                flash_window_ex(wgs, FLASHW_STOP, 0, 0);
            else
                FlashWindow(wgs->term_hwnd, false);
        }

    } else if (mode == 2) {
        /* start */
        if (!wgs->flashing) {
            wgs->flashing = true;
            if (p_FlashWindowEx) {
                /* For so-called "steady" mode, we use uCount=2, which
                 * seems to be the traditional number of flashes used
                 * by user notifications (e.g., by Explorer).
                 * uCount=0 appears to enable continuous flashing, per
                 * "flashing" mode, although I haven't seen this
                 * documented. */
                flash_window_ex(wgs, FLASHW_ALL | FLASHW_TIMER,
                                (beep_ind == B_IND_FLASH ? 0 : 2),
                                0 /* system cursor blink rate */);
                /* No need to schedule timer */
            } else {
                FlashWindow(wgs->term_hwnd, true);
                wgs->next_flash = schedule_timer(450, flash_window_timer, wgs);
            }
        }

    } else if ((mode == 1) && (beep_ind == B_IND_FLASH)) {
        /* maintain */
        if (wgs->flashing && !p_FlashWindowEx) {
            FlashWindow(wgs->term_hwnd, true);    /* toggle */
            wgs->next_flash = schedule_timer(450, flash_window_timer, wgs);
        }
    }
}

/*
 * Beep.
 */
static void wintw_bell(TermWin *tw, int mode)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (mode == BELL_DEFAULT) {
        /*
         * For MessageBeep style bells, we want to be careful of
         * timing, because they don't have the nice property of
         * PlaySound bells that each one cancels the previous
         * active one. So we limit the rate to one per 50ms or so.
         */
        long beepdiff;

        beepdiff = GetTickCount() - wgs->last_beep_time;
        if (beepdiff >= 0 && beepdiff < 50)
            return;
        MessageBeep(MB_OK);
        /*
         * The above MessageBeep call takes time, so we record the
         * time _after_ it finishes rather than before it starts.
         */
        wgs->last_beep_time = GetTickCount();
    } else if (mode == BELL_WAVEFILE) {
        Filename *bell_wavefile = conf_get_filename(
            wgs->conf, CONF_bell_wavefile);
        bool success = (
            p_PlaySoundW ? p_PlaySoundW(bell_wavefile->wpath, NULL,
                                        SND_ASYNC | SND_FILENAME) :
            p_PlaySoundA ? p_PlaySoundA(bell_wavefile->cpath, NULL,
                                        SND_ASYNC | SND_FILENAME) : false);
        if (!success) {
            char *buf, *otherbuf;
            show_mouseptr(wgs, true);
            buf = dupprintf(
                "Unable to play sound file\n%s\nUsing default sound instead",
                bell_wavefile->utf8path);
            otherbuf = dupprintf("%s Sound Error", appname);
            message_box(wgs->term_hwnd, buf, otherbuf,
                        MB_OK | MB_ICONEXCLAMATION, true, 0);
            sfree(buf);
            sfree(otherbuf);
            conf_set_int(wgs->conf, CONF_beep, BELL_DEFAULT);
        }
    } else if (mode == BELL_PCSPEAKER) {
        long beepdiff;

        beepdiff = GetTickCount() - wgs->last_beep_time;
        if (beepdiff >= 0 && beepdiff < 50)
            return;

        /*
         * We must beep in different ways depending on whether this
         * is a 95-series or NT-series OS.
         */
        if (osPlatformId == VER_PLATFORM_WIN32_NT)
            Beep(800, 100);
        else
            MessageBeep(-1);
        wgs->last_beep_time = GetTickCount();
    }
    /* Otherwise, either visual bell or disabled; do nothing here */
    if (!wgs->term->has_focus) {
        flash_window(wgs, 2);               /* start */
    }
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_minimised(TermWin *tw, bool minimised)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (IsIconic(wgs->term_hwnd)) {
        if (!minimised)
            ShowWindow(wgs->term_hwnd, SW_RESTORE);
    } else {
        if (minimised)
            ShowWindow(wgs->term_hwnd, SW_MINIMIZE);
    }
}

/*
 * Move the window in response to a server-side request.
 */
static void wintw_move(TermWin *tw, int x, int y)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    int resize_action = conf_get_int(wgs->conf, CONF_resize_action);
    if (resize_action == RESIZE_DISABLED ||
        resize_action == RESIZE_FONT ||
        IsZoomed(wgs->term_hwnd))
        return;

    SetWindowPos(wgs->term_hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
static void wintw_set_zorder(TermWin *tw, bool top)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (conf_get_bool(wgs->conf, CONF_alwaysontop))
        return;                        /* ignore */
    SetWindowPos(wgs->term_hwnd, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Refresh the window in response to a server-side request.
 */
static void wintw_refresh(TermWin *tw)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    InvalidateRect(wgs->term_hwnd, NULL, true);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_maximised(TermWin *tw, bool maximised)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (IsZoomed(wgs->term_hwnd)) {
        if (!maximised)
            ShowWindow(wgs->term_hwnd, SW_RESTORE);
    } else {
        if (maximised)
            ShowWindow(wgs->term_hwnd, SW_MAXIMIZE);
    }
}

/*
 * See if we're in full-screen mode.
 */
static bool is_full_screen(WinGuiSeat *wgs)
{
    if (!IsZoomed(wgs->term_hwnd))
        return false;
    if (GetWindowLongPtr(wgs->term_hwnd, GWL_STYLE) & WS_CAPTION)
        return false;
    return true;
}

/* Get a MONITORINFO structure for the nearest available monitor, if the
 * multimon API is available and returns success. Shared subroutine between
 * get_fullscreen_rect() and get_workingarea_rect(). */
static bool get_monitor_info(WinGuiSeat *wgs, MONITORINFO *mi)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
    if (p_GetMonitorInfoA && p_MonitorFromWindow) {
        HMONITOR mon;
        mon = p_MonitorFromWindow(wgs->term_hwnd, MONITOR_DEFAULTTONEAREST);
        mi->cbSize = sizeof(*mi);
        p_GetMonitorInfoA(mon, mi);
        return true;
    }
#endif
    return false;
}


/* Get the rect/size of a full-screen window on the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static bool get_fullscreen_rect(WinGuiSeat *wgs, RECT *ss)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
    MONITORINFO mi;
    if (get_monitor_info(wgs, &mi)) {
        /* structure copy */
        *ss = mi.rcMonitor;
        return true;
    }
#endif
/* could also use code like this:
        ss->left = ss->top = 0;
        ss->right = GetSystemMetrics(SM_CXSCREEN);
        ss->bottom = GetSystemMetrics(SM_CYSCREEN);
*/
    return GetClientRect(GetDesktopWindow(), ss);
}


/* Similar to get_fullscreen_rect, but retrieves the working area of the
 * monitor (minus the taskbar) instead of its full extent. */
static bool get_workingarea_rect(WinGuiSeat *wgs, RECT *ss)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
    MONITORINFO mi;
    if (get_monitor_info(wgs, &mi)) {
        /* structure copy */
        *ss = mi.rcWork;
        return true;
    }
#endif
    /* Fallback is the same as get_monitor_rect, which is good _enough_:
     * if the window overlaps the taskbar, that's not too bad a failure. */
    return GetClientRect(GetDesktopWindow(), ss);
}


/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void make_full_screen(WinGuiSeat *wgs)
{
    DWORD style;
    RECT ss;

    assert(IsZoomed(wgs->term_hwnd));

    if (is_full_screen(wgs))
        return;

    /* Remove the window furniture. */
    style = GetWindowLongPtr(wgs->term_hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    if (conf_get_bool(wgs->conf, CONF_scrollbar_in_fullscreen))
        style |= WS_VSCROLL;
    else
        style &= ~WS_VSCROLL;
    SetWindowLongPtr(wgs->term_hwnd, GWL_STYLE, style);

    /* Resize ourselves to exactly cover the nearest monitor. */
    get_fullscreen_rect(wgs, &ss);
    SetWindowPos(wgs->term_hwnd, HWND_TOP, ss.left, ss.top,
                 ss.right - ss.left, ss.bottom - ss.top, SWP_FRAMECHANGED);

    /* We may have changed size as a result */

    reset_window(wgs, 0);

    /* Tick the menu item in the System and context menus. */
    {
        int i;
        for (i = 0; i < lenof(wgs->popup_menus); i++)
            CheckMenuItem(wgs->popup_menus[i].menu,
                          IDM_FULLSCREEN, MF_CHECKED);
    }
}

/*
 * Clear the full-screen attributes.
 */
static void clear_full_screen(WinGuiSeat *wgs)
{
    DWORD oldstyle, style;

    /* Reinstate the window furniture. */
    style = oldstyle = GetWindowLongPtr(wgs->term_hwnd, GWL_STYLE);
    style |= WS_CAPTION | WS_BORDER;
    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_DISABLED)
        style &= ~WS_THICKFRAME;
    else
        style |= WS_THICKFRAME;
    if (conf_get_bool(wgs->conf, CONF_scrollbar))
        style |= WS_VSCROLL;
    else
        style &= ~WS_VSCROLL;
    if (style != oldstyle) {
        SetWindowLongPtr(wgs->term_hwnd, GWL_STYLE, style);
        SetWindowPos(wgs->term_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED);
    }

    /* Untick the menu item in the System and context menus. */
    {
        int i;
        for (i = 0; i < lenof(wgs->popup_menus); i++)
            CheckMenuItem(wgs->popup_menus[i].menu,
                          IDM_FULLSCREEN, MF_UNCHECKED);
    }
}

/*
 * Toggle full-screen mode.
 */
static void flip_full_screen(WinGuiSeat *wgs)
{
    if (is_full_screen(wgs)) {
        ShowWindow(wgs->term_hwnd, SW_RESTORE);
    } else if (IsZoomed(wgs->term_hwnd)) {
        make_full_screen(wgs);
    } else {
        SendMessage(wgs->term_hwnd, WM_FULLSCR_ON_MAX, 0, 0);
        ShowWindow(wgs->term_hwnd, SW_MAXIMIZE);
    }
}

static size_t win_seat_output(Seat *seat, SeatOutputType type,
                              const void *data, size_t len)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    return term_data(wgs->term, data, len);
}

static void wintw_unthrottle(TermWin *tw, size_t bufsize)
{
    WinGuiSeat *wgs = container_of(tw, WinGuiSeat, termwin);
    if (wgs->backend)
        backend_unthrottle(wgs->backend, bufsize);
}

static bool win_seat_eof(Seat *seat)
{
    return true;   /* do respond to incoming EOF with outgoing */
}

static SeatPromptResult win_seat_get_userpass_input(Seat *seat, prompts_t *p)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    SeatPromptResult spr;
    spr = cmdline_get_passwd_input(p, &wgs->cmdline_get_passwd_state, true);
    if (spr.kind == SPRK_INCOMPLETE)
        spr = term_get_userpass_input(wgs->term, p);
    return spr;
}

static void win_seat_set_trust_status(Seat *seat, bool trusted)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    term_set_trust_status(wgs->term, trusted);
}

static bool win_seat_can_set_trust_status(Seat *seat)
{
    return true;
}

static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    term_get_cursor_position(wgs->term, x, y);
    return true;
}

static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    RECT r;
    GetWindowRect(wgs->term_hwnd, &r);
    *x = r.right - r.left;
    *y = r.bottom - r.top;
    return true;
}
