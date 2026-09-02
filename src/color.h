#ifndef COLOR_H
#define COLOR_H

/*
 * Minimal ANSI colour support for terminal output.
 *
 * Colours are applied only when ALL of these hold:
 *   - stdout is a TTY (interactive terminal)
 *   - the NO_COLOR environment variable is not set
 *   - colours have not been manually disabled with --no-color
 * They can be force-enabled with --color even when stdout is redirected.
 */

/* Whether colour output is currently enabled. */
int color_enabled(void);

/* Force-disable colour output (e.g. from a --no-color flag). */
void color_disable(void);

/* Force-enable colour output (e.g. from a --color flag). */
void color_enable(void);

/* Re-evaluate colour enablement from the terminal/environment defaults.
 * Returns 1 if colours are enabled, else 0. */
int color_init(void);

/*
 * Return the ANSI escape sequence string that starts the given named colour,
 * or an empty string if colour output is disabled. The returned pointer is a
 * static string; `color_reset()` yields the matching "reset" escape sequence.
 * The returned value is owned by the module.
 *
 * Names (mapped to a relevant 256-colour/ANSI code):
 *   "red", "green", "yellow", "blue", "magenta", "cyan", "white", "bold",
 *   plus numeric form as "fg:<n>".
 */
const char *color_start(const char *name);

/* Return the ANSI "reset" escape sequence, or "" when colour is disabled. */
const char *color_reset(void);

#endif /* COLOR_H */