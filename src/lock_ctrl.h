#ifndef ELOCK_LOCK_CTRL_H_
#define ELOCK_LOCK_CTRL_H_

/*
 * Owns GPIO48 lock output and an internal "lock_thread" that handles the
 * timed unlock sequence (HIGH for LOCK_UNLOCK_DURATION_S, then LOW). All
 * public calls are non-blocking.
 */

#include <stdbool.h>

int  lock_ctrl_init(void);
void lock_ctrl_unlock(void);
void lock_ctrl_lock(void);
bool lock_ctrl_is_locked(void);

#endif /* ELOCK_LOCK_CTRL_H_ */
