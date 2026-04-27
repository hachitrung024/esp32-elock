#ifndef ELOCK_BUTTON_H_
#define ELOCK_BUTTON_H_

/*
 * Owns GPIO0 with internal pull-up. A dedicated polling thread
 * (button_thread) debounces 50ms in software and calls lock_ctrl_lock()
 * on press.
 */

int button_init(void);

#endif /* ELOCK_BUTTON_H_ */
