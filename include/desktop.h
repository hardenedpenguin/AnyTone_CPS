#ifndef ANYTONE_DESKTOP_H
#define ANYTONE_DESKTOP_H

/* Open an embedded WebKitGTK window loading url. Blocks until the window
 * is closed. Returns 0 on success, non-zero if desktop UI is unavailable. */
int at_desktop_run(const char *url, const char *title);

/* 1 if this build was linked with WebKitGTK. */
int at_desktop_available(void);

#endif
