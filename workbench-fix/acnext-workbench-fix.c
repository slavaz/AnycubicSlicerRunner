/* LD_PRELOAD shim for AnycubicSlicerNext: fixes blank Workbench tab.
 *
 * Root cause (issue #64): the app serves Workbench SPA through a custom
 * pages:// URI scheme but never calls webkit_uri_scheme_response_set_content_type().
 * WebKitGTK falls back to guessing "application/x-extension-html", which it
 * treats as unsupported, and aborts with "Frame load interrupted".
 *
 * Fix: intercept webkit_uri_scheme_request_finish_with_response() and inject the
 * correct content type for pages:// responses before passing through to WebKit.
 *
 * Build:
 *   gcc -shared -fPIC -O2 -o acnext-workbench-fix.so acnext-workbench-fix.c -ldl
 *
 * Use:
 *   LD_PRELOAD=/path/to/acnext-workbench-fix.so AnycubicSlicerNext
 *   ACNEXT_FIX_DEBUG=1 LD_PRELOAD=... AnycubicSlicerNext   # verbose logging
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef void WebKitURISchemeRequest;
typedef void WebKitURISchemeResponse;

typedef void        (*fn_finish)(WebKitURISchemeRequest *, WebKitURISchemeResponse *);
typedef void        (*fn_set_ct)(WebKitURISchemeResponse *, const char *);
typedef const char *(*fn_get_uri)(WebKitURISchemeRequest *);

static fn_finish  real_finish  = NULL;
static fn_set_ct  real_set_ct  = NULL;
static fn_get_uri real_get_uri = NULL;

static const char *mime_for_ext(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcmp(ext, ".html") || !strcmp(ext, ".htm")) return "text/html";
    if (!strcmp(ext, ".js"))    return "application/javascript";
    if (!strcmp(ext, ".css"))   return "text/css";
    if (!strcmp(ext, ".json"))  return "application/json";
    if (!strcmp(ext, ".png"))   return "image/png";
    if (!strcmp(ext, ".svg"))   return "image/svg+xml";
    if (!strcmp(ext, ".woff2")) return "font/woff2";
    if (!strcmp(ext, ".woff"))  return "font/woff";
    if (!strcmp(ext, ".ttf"))   return "font/ttf";
    return "application/octet-stream";
}

/* Strip query string and fragment; return pointer into static buffer */
static const char *path_only(const char *uri)
{
    static char buf[4096];
    /* skip scheme://host */
    const char *p = strstr(uri, "://");
    p = p ? strchr(p + 3, '/') : uri;
    if (!p) p = uri;
    strncpy(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *q = strchr(buf, '?'); if (q) *q = '\0';
    char *f = strchr(buf, '#'); if (f) *f = '\0';
    return buf;
}

void webkit_uri_scheme_request_finish_with_response(
    WebKitURISchemeRequest  *request,
    WebKitURISchemeResponse *response)
{
    if (!real_finish)
        real_finish  = (fn_finish) dlsym(RTLD_NEXT, "webkit_uri_scheme_request_finish_with_response");
    if (!real_set_ct)
        real_set_ct  = (fn_set_ct) dlsym(RTLD_NEXT, "webkit_uri_scheme_response_set_content_type");
    if (!real_get_uri)
        real_get_uri = (fn_get_uri)dlsym(RTLD_NEXT, "webkit_uri_scheme_request_get_uri");

    if (real_get_uri && real_set_ct) {
        const char *uri = real_get_uri(request);
        if (uri && strncmp(uri, "pages://", 8) == 0) {
            const char *mime = mime_for_ext(path_only(uri));
            if (getenv("ACNEXT_FIX_DEBUG"))
                fprintf(stderr, "[acnext-fix] pages:// %s -> %s\n", uri, mime);
            real_set_ct(response, mime);
        }
    }

    real_finish(request, response);
}
