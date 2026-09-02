#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stddef.h>

/* Max number of concurrent worker rows that can be tracked. */
#define DASH_MAX_SLOTS 128

/* Static launch/config fields shown in the "Launch" panel. */
typedef struct {
    char version[64];
    char source_url[1024];
    char destination[1024];
    char identifier[512];
    char user[128];
    char filter[512];
    int  threads;
} DashLaunch;

/* Live aggregate counters handed to the dashboard each tick. */
typedef struct {
    long downloaded;
    long skipped;
    long restricted;
    long filtered;
    long failed;
    long bytes_done;
} DashCounters;

/* Enable the interactive dashboard. Must be called once before any workers
   start. Safe no-op if stdout is not a terminal. The dashboard is opt-in:
   when inactive all status/LOG output goes straight to stdout as before. */
void dash_init(void);

/* Populate the static "Launch" fields shown in the header/launch panel.
   Call once before the download starts. */
void dash_set_launch(const DashLaunch *launch);

/* Non-zero when the dashboard is active (--dashboard given and stdout is a
   terminal). */
int dash_active(void);

/* Register a worker slot about to download a file. */
void dash_begin_worker(int slot, const char *name, int index, int total);

/* Update a worker slot with progress (bytes done, expected size, resume offset,
   instantaneous speed, ETA in seconds). -1 speed/eta hides them. */
void dash_set_worker(int slot, long done, long total, long resume,
                     double speed, double eta);

/* Mark a worker slot finished (its top row is removed on next render). */
void dash_end_worker(int slot);

/* Append a line to the bottom panel (result of the latest operations). */
void dash_log(const char *line);

/* Request a repaint, honoring an internal throttle interval. */
void dash_tick(void);

/* Update the live aggregate counters shown in the footer/summary line. */
void dash_set_agg(const DashCounters *agg);

/* Restore the terminal and print any trailing content. Call after all workers
   complete and before program exit. */
void dash_shutdown(void);

#endif /* DASHBOARD_H */