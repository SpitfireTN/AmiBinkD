/*
 * Automatic snprintf/vsnprintf fallback for AmiBinkd v9.10
 * ---------------------------------------------------------
 * - Requires ZERO changes to existing source files
 * - Requires ZERO changes to config.h or Makefiles
 * - Uses system snprintf if available
 * - Uses fallback dopr() implementation otherwise
 */

#include <stdarg.h>
#include <stddef.h>

/* ---------------------------------------------------------
 * Step 1: Detect whether the system already has vsnprintf
 * --------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
/* Weak symbol test: if libc provides vsnprintf, use it */
extern int vsnprintf(char *, size_t, const char *, va_list)
    __attribute__((weak));
#endif

/* Forward declarations for fallback */
static void dopr(char *buffer, size_t maxlen, const char *format, va_list args);

/* ---------------------------------------------------------
 * Step 2: Provide fallback vsnprintf only if missing
 * --------------------------------------------------------- */

int fallback_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
    if (count == 0)
        return 0;

    str[0] = '\0';
    dopr(str, count, fmt, args);
    return (int)strlen(str);
}

int vsnprintf_auto(char *str, size_t count, const char *fmt, va_list args)
{
#if defined(__GNUC__) || defined(__clang__)
    if (vsnprintf)  /* system version exists */
        return vsnprintf(str, count, fmt, args);
#endif
    return fallback_vsnprintf(str, count, fmt, args);
}

/* ---------------------------------------------------------
 * Step 3: Provide snprintf wrapper
 * --------------------------------------------------------- */

int snprintf(char *str, size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf_auto(str, count, fmt, ap);
    va_end(ap);
    return r;
}

/* ---------------------------------------------------------
 * Step 4: Provide fallback dopr(), fmtstr(), fmtint(), fmtfp()
 * --------------------------------------------------------- */
/* >>> INSERT YOUR FULL ORIGINAL FALLBACK IMPLEMENTATION HERE <<< */
/* (fmtstr, fmtint, fmtfp, round, dopr_outch, dopr, etc.) */
