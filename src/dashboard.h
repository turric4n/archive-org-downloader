#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stddef.h>

/* Max number of concurrent worker rows the top panel can show. */
#define DASH_MAX_SLOTS 128

/* Enable the interactive dashboard. Must be called once before any workers
   start. Safe no-op if stdout is not a terminal. The dashboard is opt-in:
   when inactive all status/LOG output goes straight to stdout as before. */
void dash_init(void);

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

/* Restore the terminal and print any trailing content. Call after all workers
   complete and before program exit. */
void dash_shutdown(void);

#endif /* DASHBOARD_H */