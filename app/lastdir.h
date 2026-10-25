/* lastdir.h: the drawer a program last browsed, so its file requester opens there next time (the user,
 * 10 October 2026: "apps should remember the last folder they were browsing, right now they all suffer
 * amnesia"). Kept in ENV:OpenApps/<program>.drawer for the session and ENVARC: for the next boot; a drawer
 * that is no longer there (a volume taken out) is forgotten without a requester asking for it.
 * The same file in every Open app. MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENAPPS_LASTDIR_H
#define OPENAPPS_LASTDIR_H

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

/* buf: the drawer last used by app, or "" (none kept, or no longer there). */
static void lastdir_get(const char *app, char *buf, int size)
{
    static const char *const where[2] = { "ENV:OpenApps/", "ENVARC:OpenApps/" };
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    char name[80];
    BPTR f = 0, l;
    LONG n = 0;
    int i;
    buf[0] = 0;
    for (i = 0; i < 2 && !f; i++) {
        snprintf(name, sizeof name, "%s%s.drawer", where[i], app);
        f = Open((STRPTR)name, MODE_OLDFILE);
    }
    if (!f) return;
    n = Read(f, buf, size - 1);
    Close(f);
    if (n < 0) n = 0;
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
    if (!buf[0]) return;
    me->pr_WindowPtr = (APTR)-1;                    /* a volume no longer there: no "please insert" */
    l = Lock((STRPTR)buf, ACCESS_READ);
    me->pr_WindowPtr = was;
    if (l) UnLock(l); else buf[0] = 0;
}

/* Keeps drawer as app's last, in ENV: and ENVARC:. */
static void lastdir_put(const char *app, const char *drawer)
{
    static const char *const base[2] = { "ENV:OpenApps", "ENVARC:OpenApps" };
    char name[80];
    BPTR f, d;
    int i;
    if (!drawer || !drawer[0]) return;
    for (i = 0; i < 2; i++) {
        d = CreateDir((STRPTR)base[i]);
        if (d) UnLock(d);
        snprintf(name, sizeof name, "%s/%s.drawer", base[i], app);
        if ((f = Open((STRPTR)name, MODE_NEWFILE))) {
            Write(f, (APTR)drawer, strlen(drawer));
            Write(f, (APTR)"\n", 1);
            Close(f);
        }
    }
}

#endif
