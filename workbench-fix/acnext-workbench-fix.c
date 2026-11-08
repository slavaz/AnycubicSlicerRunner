/*
 * LD_PRELOAD compatibility and bugfix shim for AnycubicSlicerNext on Linux (Fedora 44 / WebKit2GTK 4.1)
 *
 * Fixes addressed:
 * 1. Skia / Fontconfig infinite loop bug (SkFontMgr_fontconfig::GetFamilyNames):
 *    Intercepts FcPatternGetString() to break out of runaway iterations on missing FC_FAMILY,
 *    preventing WebKitWebProcess CPU hang (100% core pegging, blank WebViews, and unkillable process).
 * 2. WebKit2GTK 2.42+ DMABUF/SHM rendering:
 *    Prevents application from calling setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1") which destroys
 *    the backing store in modern WebKit2GTK, and forces SHM buffer transport with software compositing.
 * 3. Workbench tab missing MIME types (issue #64):
 *    Intercepts webkit_uri_scheme_request_finish_with_response() and injects correct Content-Type
 *    for custom pages:// URIs to avoid "Frame load interrupted" errors.
 * 4. CORS and security manager registration for custom URI schemes:
 *    Registers custom schemes as CORS-enabled and secure with WebKitSecurityManager.
 * 5. WebKitSettings hardware acceleration policy:
 *    Forces WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER (2) and allows file/CORS access on all views.
 * 6. Casdoor OAuth / about:blank initialization race:
 *    Re-issues target URI if about:blank finishes after the target was requested during view initialization.
 * 7. GTK widget size clamp:
 *    Clamps negative dimensions < -1 in gtk_widget_set_size_request() to prevent GTK critical assertions.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef void WebKitURIRequest;
typedef void WebKitURISchemeRequest;
typedef void WebKitURISchemeResponse;
typedef void WebKitSettings;
typedef void WebKitWebView;
typedef void WebKitWebContext;
typedef void WebKitSecurityManager;
typedef void GtkWidget;
typedef int gboolean;
typedef unsigned long gulong;
typedef void (*GCallback)(void);
typedef void (*WebKitURISchemeRequestCallback)(WebKitURISchemeRequest *, void *);

static int debug_enabled = -1;
static int log_fd = -2;

static int is_debug(void) {
    if (debug_enabled == -1) {
        const char *env = getenv("ACNEXT_FIX_DEBUG");
        debug_enabled = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return debug_enabled;
}

static void acnext_log(const char *fmt, ...) {
    if (!is_debug()) {
        return;
    }

    if (log_fd == -2) {
        const char *log_path = getenv("ACNEXT_FIX_LOG");
        if (log_path && *log_path) {
            log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
        } else {
            log_fd = -1;
        }
    }

    char buf[1024];
    int prefix_len = snprintf(buf, sizeof(buf), "[acnext-fix pid=%d %s] ",
                              getpid(), program_invocation_short_name ? program_invocation_short_name : "unknown");
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(buf)) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len, fmt, ap);
    va_end(ap);

    if (body_len < 0) {
        return;
    }
    size_t total = (size_t)prefix_len + (size_t)body_len;
    if (total >= sizeof(buf)) {
        total = sizeof(buf) - 1;
    }

    fwrite(buf, 1, total, stderr);
    if (log_fd >= 0) {
        ssize_t unused = write(log_fd, buf, total);
        (void)unused;
    }
}

/*
 * 1. Environment & DMABUF SHM initialization
 */
typedef int (*setenv_fn)(const char *name, const char *value, int overwrite);
static setenv_fn real_setenv = NULL;

__attribute__((constructor))
static void acnext_fix_init(void) {
    real_setenv = (setenv_fn)dlsym(RTLD_NEXT, "setenv");
    if (program_invocation_short_name &&
        strcmp(program_invocation_short_name, "AnycubicSlicerNext") == 0) {
        unsetenv("WEBKIT_DISABLE_DMABUF_RENDERER");
        if (real_setenv) {
            real_setenv("WEBKIT_DMABUF_RENDERER_FORCE_SHM", "1", 1);
            real_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
            real_setenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1", 1);
        }
        acnext_log("initialized in AnycubicSlicerNext (SHM backing store)\n");
    }
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!real_setenv) {
        real_setenv = (setenv_fn)dlsym(RTLD_NEXT, "setenv");
    }
    if (name && strcmp(name, "WEBKIT_DISABLE_DMABUF_RENDERER") == 0) {
        acnext_log("blocked setenv(%s, %s, %d)\n", name, value, overwrite);
        return 0;
    }
    return real_setenv ? real_setenv(name, value, overwrite) : -1;
}

/*
 * 2. Skia / Fontconfig infinite loop workaround
 */
typedef int FcResult;
typedef void FcPattern;
typedef unsigned char FcChar8;
typedef FcResult (*fc_pattern_get_string_fn)(FcPattern *, const char *, int, FcChar8 **);

static fc_pattern_get_string_fn real_FcPatternGetString = NULL;

FcResult FcPatternGetString(FcPattern *p, const char *object, int n, FcChar8 **s) {
    if (!real_FcPatternGetString) {
        real_FcPatternGetString = (fc_pattern_get_string_fn)dlsym(RTLD_NEXT, "FcPatternGetString");
    }
    if (!real_FcPatternGetString) {
        return 1; /* FcResultNoMatch */
    }

    FcResult res = real_FcPatternGetString(p, object, n, s);
    /*
     * Neutralize Skia / WebKitGTK infinite loop bug in SkFontMgr_fontconfig::GetFamilyNames:
     * When iterating font family names (id = 0, 1, ...), Skia expects FcResultNoId (3) to terminate
     * the loop. If a font pattern lacks FC_FAMILY, Fontconfig returns FcResultNoMatch (1).
     * Skia does 'if (result != FcResultMatch) continue;', entering an infinite 100% CPU loop.
     * We convert FcResultNoMatch -> FcResultNoId when n > 0 (or if n >= 32) to break out safely.
     */
    if (n > 0 && res == 1 /* FcResultNoMatch */) {
        return 3; /* FcResultNoId */
    }
    if (n >= 32 && res != 0 /* not FcResultMatch */) {
        return 3; /* FcResultNoId */
    }
    return res;
}

/*
 * 3. GTK widget size clamp
 */
void gtk_widget_set_size_request(GtkWidget *widget, int width, int height) {
    static void (*real_fn)(GtkWidget *, int, int) = NULL;
    if (!real_fn) {
        real_fn = (void (*)(GtkWidget *, int, int))dlsym(RTLD_NEXT, "gtk_widget_set_size_request");
    }
    if (!real_fn) return;

    if (width < -1) width = -1;
    if (height < -1) height = -1;
    real_fn(widget, width, height);
}

/*
 * 4. WebKit WebView settings and about:blank reload safeguard
 */
#define MAX_TRACKED_VIEWS 64
typedef struct {
    WebKitWebView *view;
    char last_target_uri[2048];
    int retried_after_blank;
} ViewTrack;

static ViewTrack tracked_views[MAX_TRACKED_VIEWS];
static int num_tracked_views = 0;

static ViewTrack *get_view_track(WebKitWebView *view) {
    for (int i = 0; i < num_tracked_views; i++) {
        if (tracked_views[i].view == view) {
            return &tracked_views[i];
        }
    }
    if (num_tracked_views < MAX_TRACKED_VIEWS) {
        ViewTrack *vt = &tracked_views[num_tracked_views++];
        memset(vt, 0, sizeof(*vt));
        vt->view = view;
        return vt;
    }
    return NULL;
}

void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *settings, int policy) {
    static void (*real_fn)(WebKitSettings *, int) = NULL;
    if (!real_fn) {
        real_fn = (void (*)(WebKitSettings *, int))dlsym(RTLD_NEXT, "webkit_settings_set_hardware_acceleration_policy");
    }
    if (!real_fn) return;
    (void)policy;
    real_fn(settings, 2 /* WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER */);
}

static void on_load_changed(WebKitWebView *web_view, int load_event, void *user_data) {
    (void)user_data;
    static const char *(*get_uri_fn)(WebKitWebView *) = NULL;
    static void (*load_uri_fn)(WebKitWebView *, const char *) = NULL;

    if (!get_uri_fn)
        get_uri_fn = (const char *(*)(WebKitWebView *))dlsym(RTLD_NEXT, "webkit_web_view_get_uri");
    if (!load_uri_fn)
        load_uri_fn = (void (*)(WebKitWebView *, const char *))dlsym(RTLD_NEXT, "webkit_web_view_load_uri");

    /*
     * If about:blank finished loading, but a real target URL (e.g. Casdoor OAuth)
     * was requested during initialization, reload the target URL.
     */
    if (load_event == 3 /* FINISHED */ && get_uri_fn && load_uri_fn) {
        const char *uri = get_uri_fn(web_view);
        if (uri && strcmp(uri, "about:blank") == 0) {
            ViewTrack *vt = get_view_track(web_view);
            if (vt && vt->last_target_uri[0] != '\0' && !vt->retried_after_blank) {
                vt->retried_after_blank = 1;
                acnext_log("WebView %p finished about:blank; re-issuing target URI: %s\n",
                           (void *)web_view, vt->last_target_uri);
                load_uri_fn(web_view, vt->last_target_uri);
            }
        }
    }
}

WebKitWebView *webkit_web_view_new_with_context(WebKitWebContext *context) {
    static WebKitWebView *(*real_new)(WebKitWebContext *) = NULL;
    static gulong (*real_signal_connect)(void *, const char *, GCallback, void *, void *, int) = NULL;
    static WebKitSettings *(*real_get_settings)(WebKitWebView *) = NULL;
    static void (*real_set_file_access)(WebKitSettings *, gboolean) = NULL;
    static void (*real_set_univ_access)(WebKitSettings *, gboolean) = NULL;

    if (!real_new)
        real_new = (WebKitWebView *(*)(WebKitWebContext *))dlsym(RTLD_NEXT, "webkit_web_view_new_with_context");
    if (!real_signal_connect)
        real_signal_connect = (gulong (*)(void *, const char *, GCallback, void *, void *, int))dlsym(RTLD_NEXT, "g_signal_connect_data");
    if (!real_get_settings)
        real_get_settings = (WebKitSettings *(*)(WebKitWebView *))dlsym(RTLD_NEXT, "webkit_web_view_get_settings");
    if (!real_set_file_access)
        real_set_file_access = (void (*)(WebKitSettings *, gboolean))dlsym(RTLD_NEXT, "webkit_settings_set_allow_file_access_from_file_urls");
    if (!real_set_univ_access)
        real_set_univ_access = (void (*)(WebKitSettings *, gboolean))dlsym(RTLD_NEXT, "webkit_settings_set_allow_universal_access_from_file_urls");

    WebKitWebView *view = real_new ? real_new(context) : NULL;
    if (view) {
        get_view_track(view);
        if (real_get_settings) {
            WebKitSettings *settings = real_get_settings(view);
            if (settings) {
                webkit_settings_set_hardware_acceleration_policy(settings, 2);
                if (real_set_file_access) real_set_file_access(settings, 1);
                if (real_set_univ_access) real_set_univ_access(settings, 1);
            }
        }
        if (real_signal_connect) {
            real_signal_connect(view, "load-changed", (GCallback)on_load_changed, NULL, NULL, 0);
        }
    }
    return view;
}

void webkit_web_view_load_uri(WebKitWebView *web_view, const char *uri) {
    static void (*real_load_uri)(WebKitWebView *, const char *) = NULL;
    if (!real_load_uri) {
        real_load_uri = (void (*)(WebKitWebView *, const char *))dlsym(RTLD_NEXT, "webkit_web_view_load_uri");
    }

    if (web_view && uri && *uri && strcmp(uri, "about:blank") != 0) {
        ViewTrack *vt = get_view_track(web_view);
        if (vt) {
            snprintf(vt->last_target_uri, sizeof(vt->last_target_uri), "%s", uri);
            vt->retried_after_blank = 0;
        }
    }

    if (real_load_uri) {
        real_load_uri(web_view, uri);
    }
}

/*
 * 5. Custom URI schemes: CORS / security manager registration
 */
void webkit_web_context_register_uri_scheme(WebKitWebContext *context, const char *scheme,
                                            WebKitURISchemeRequestCallback callback,
                                            void *user_data, void *destroy_notify) {
    static void (*real_reg)(WebKitWebContext *, const char *, WebKitURISchemeRequestCallback, void *, void *) = NULL;
    static WebKitSecurityManager *(*get_sec_mgr)(WebKitWebContext *) = NULL;
    static void (*reg_cors)(WebKitSecurityManager *, const char *) = NULL;
    static void (*reg_secure)(WebKitSecurityManager *, const char *) = NULL;

    if (!real_reg)
        real_reg = (void (*)(WebKitWebContext *, const char *, WebKitURISchemeRequestCallback, void *, void *))dlsym(RTLD_NEXT, "webkit_web_context_register_uri_scheme");
    if (!get_sec_mgr)
        get_sec_mgr = (WebKitSecurityManager *(*)(WebKitWebContext *))dlsym(RTLD_NEXT, "webkit_web_context_get_security_manager");
    if (!reg_cors)
        reg_cors = (void (*)(WebKitSecurityManager *, const char *))dlsym(RTLD_NEXT, "webkit_security_manager_register_uri_scheme_as_cors_enabled");
    if (!reg_secure)
        reg_secure = (void (*)(WebKitSecurityManager *, const char *))dlsym(RTLD_NEXT, "webkit_security_manager_register_uri_scheme_as_secure");

    if (context && scheme && get_sec_mgr) {
        WebKitSecurityManager *sec = get_sec_mgr(context);
        if (sec) {
            if (reg_cors) reg_cors(sec, scheme);
            if (reg_secure) reg_secure(sec, scheme);
        }
    }

    if (real_reg) {
        real_reg(context, scheme, callback, user_data, destroy_notify);
    }
}

/*
 * 6. Workbench tab pages:// MIME type injection (upstream issue #64)
 */
static const char *mime_for_target(const char *target) {
    if (!target || !*target) return NULL;
    const char *q = strpbrk(target, "?#");
    size_t len = q ? (size_t)(q - target) : strlen(target);

    #define HAS_EXT(ext) (len >= sizeof(ext) - 1 && strncasecmp(target + len - (sizeof(ext) - 1), ext, sizeof(ext) - 1) == 0)
    if (HAS_EXT(".html") || HAS_EXT(".htm")) return "text/html";
    if (HAS_EXT(".js") || HAS_EXT(".mjs")) return "text/javascript";
    if (HAS_EXT(".css")) return "text/css";
    if (HAS_EXT(".svg")) return "image/svg+xml";
    if (HAS_EXT(".json") || HAS_EXT(".map")) return "application/json";
    if (HAS_EXT(".png")) return "image/png";
    if (HAS_EXT(".jpg") || HAS_EXT(".jpeg")) return "image/jpeg";
    if (HAS_EXT(".gif")) return "image/gif";
    if (HAS_EXT(".webp")) return "image/webp";
    if (HAS_EXT(".ico")) return "image/x-icon";
    if (HAS_EXT(".woff2")) return "font/woff2";
    if (HAS_EXT(".woff")) return "font/woff";
    if (HAS_EXT(".ttf")) return "font/ttf";
    #undef HAS_EXT
    return NULL;
}

void webkit_uri_scheme_request_finish_with_response(
    WebKitURISchemeRequest *request,
    WebKitURISchemeResponse *response
) {
    static void (*real_finish)(WebKitURISchemeRequest *, WebKitURISchemeResponse *) = NULL;
    static const char *(*real_get_uri)(WebKitURISchemeRequest *) = NULL;
    static const char *(*real_get_path)(WebKitURISchemeRequest *) = NULL;
    static void (*real_set_ct)(WebKitURISchemeResponse *, const char *) = NULL;

    if (!real_finish)
        real_finish = (void (*)(WebKitURISchemeRequest *, WebKitURISchemeResponse *))dlsym(RTLD_NEXT, "webkit_uri_scheme_request_finish_with_response");
    if (!real_get_uri)
        real_get_uri = (const char *(*)(WebKitURISchemeRequest *))dlsym(RTLD_NEXT, "webkit_uri_scheme_request_get_uri");
    if (!real_get_path)
        real_get_path = (const char *(*)(WebKitURISchemeRequest *))dlsym(RTLD_NEXT, "webkit_uri_scheme_request_get_path");
    if (!real_set_ct)
        real_set_ct = (void (*)(WebKitURISchemeResponse *, const char *))dlsym(RTLD_NEXT, "webkit_uri_scheme_response_set_content_type");

    const char *path = (request && real_get_path) ? real_get_path(request) : NULL;
    const char *uri = (request && real_get_uri) ? real_get_uri(request) : NULL;
    const char *mime = mime_for_target(path ? path : uri);

    if (response && real_set_ct && mime) {
        real_set_ct(response, mime);
    }

    if (real_finish) {
        real_finish(request, response);
    }
}
