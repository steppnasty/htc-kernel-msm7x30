/*
 * drivers/base/power/wakeup.c - System wakeup events framework
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/suspend.h>

#include "power.h"

#define TIMEOUT		100

/*
 * If set, the suspend/hibernate code will abort transitions to a sleep state
 * if wakeup events are registered during or immediately before the transition.
 */
bool events_check_enabled;

/* The counter of registered wakeup events. */
static atomic_t event_count = ATOMIC_INIT(0);
/* A preserved old value of event_count. */
static unsigned int saved_count;
/* The counter of wakeup events being processed. */
static atomic_t events_in_progress = ATOMIC_INIT(0);

static DEFINE_SPINLOCK(events_lock);

static void pm_wakeup_timer_fn(unsigned long data);

static LIST_HEAD(wakeup_sources);

/**
 * wakeup_source_create - Create a struct wakeup_source object.
 * @name: Name of the new wakeup source.
 */
struct wakeup_source *wakeup_source_create(const char *name)
{
	struct wakeup_source *ws;

	ws = kzalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return NULL;

	spin_lock_init(&ws->lock);
	if (name)
		ws->name = kstrdup(name, GFP_KERNEL);

	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_create);

/**
 * wakeup_source_destroy - Destroy a struct wakeup_source object.
 * @ws: Wakeup source to destroy.
 */
void wakeup_source_destroy(struct wakeup_source *ws)
{
	if (!ws)
		return;

	spin_lock_irq(&ws->lock);
	while (ws->active) {
		spin_unlock_irq(&ws->lock);

		schedule_timeout_interruptible(msecs_to_jiffies(TIMEOUT));

		spin_lock_irq(&ws->lock);
	}
	spin_unlock_irq(&ws->lock);

	kfree(ws->name);
	kfree(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_destroy);

/**
 * wakeup_source_add - Add given object to the list of wakeup sources.
 * @ws: Wakeup source object to add to the list.
 */
void wakeup_source_add(struct wakeup_source *ws)
{
	if (WARN_ON(!ws))
		return;

	setup_timer(&ws->timer, pm_wakeup_timer_fn, (unsigned long)ws);
	ws->active = false;

	spin_lock_irq(&events_lock);
	list_add_rcu(&ws->entry, &wakeup_sources);
	spin_unlock_irq(&events_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(wakeup_source_add);

/**
 * wakeup_source_remove - Remove given object from the wakeup sources list.
 * @ws: Wakeup source object to remove from the list.
 */
void wakeup_source_remove(struct wakeup_source *ws)
{
	if (WARN_ON(!ws))
		return;

	spin_lock_irq(&events_lock);
	list_del_rcu(&ws->entry);
	spin_unlock_irq(&events_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(wakeup_source_remove);

/**
 * wakeup_source_register - Create wakeup source and add it to the list.
 * @name: Name of the wakeup source to register.
 */
struct wakeup_source *wakeup_source_register(const char *name)
{
	struct wakeup_source *ws;

	ws = wakeup_source_create(name);
	if (ws)
		wakeup_source_add(ws);

	return ws;
}
EXPORT_SYMBOL_GPL(wakeup_source_register);

/**
 * wakeup_source_unregister - Remove wakeup source from the list and remove it.
 * @ws: Wakeup source object to unregister.
 */
void wakeup_source_unregister(struct wakeup_source *ws)
{
	wakeup_source_remove(ws);
	wakeup_source_destroy(ws);
}
EXPORT_SYMBOL_GPL(wakeup_source_unregister);

/**
 * device_wakeup_attach - Attach a wakeup source object to a device object.
 * @dev: Device to handle.
 * @ws: Wakeup source object to attach to @dev.
 *
 * This causes @dev to be treated as a wakeup device.
 */
static int device_wakeup_attach(struct device *dev, struct wakeup_source *ws)
{
	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		spin_unlock_irq(&dev->power.lock);
		return -EEXIST;
	}
	dev->power.wakeup = ws;
	spin_unlock_irq(&dev->power.lock);
	return 0;
}

/**
 * device_wakeup_enable - Enable given device to be a wakeup source.
 * @dev: Device to handle.
 *
 * Create a wakeup source object, register it and attach it to @dev.
 */
int device_wakeup_enable(struct device *dev)
{
	struct wakeup_source *ws;
	int ret;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = wakeup_source_register(dev_name(dev));
	if (!ws)
		return -ENOMEM;

	ret = device_wakeup_attach(dev, ws);
	if (ret)
		wakeup_source_unregister(ws);

	return ret;
}
EXPORT_SYMBOL_GPL(device_wakeup_enable);

/**
 * device_wakeup_detach - Detach a device's wakeup source object from it.
 * @dev: Device to detach the wakeup source object from.
 *
 * After it returns, @dev will not be treated as a wakeup device any more.
 */
static struct wakeup_source *device_wakeup_detach(struct device *dev)
{
	struct wakeup_source *ws;

	spin_lock_irq(&dev->power.lock);
	ws = dev->power.wakeup;
	dev->power.wakeup = NULL;
	spin_unlock_irq(&dev->power.lock);
	return ws;
}

/**
 * device_wakeup_disable - Do not regard a device as a wakeup source any more.
 * @dev: Device to handle.
 *
 * Detach the @dev's wakeup source object from it, unregister this wakeup source
 * object and destroy it.
 */
int device_wakeup_disable(struct device *dev)
{
	struct wakeup_source *ws;

	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	ws = device_wakeup_detach(dev);
	if (ws)
		wakeup_source_unregister(ws);

	return 0;
}
EXPORT_SYMBOL_GPL(device_wakeup_disable);

/**
 * device_init_wakeup - Device wakeup initialization.
 * @dev: Device to handle.
 * @enable: Whether or not to enable @dev as a wakeup device.
 *
 * By default, most devices should leave wakeup disabled.  The exceptions are
 * devices that everyone expects to be wakeup sources: keyboards, power buttons,
 * possibly network interfaces, etc.
 */
int device_init_wakeup(struct device *dev, bool enable)
{
	int ret = 0;

	if (enable) {
		device_set_wakeup_capable(dev, true);
		ret = device_wakeup_enable(dev);
	} else {
		device_set_wakeup_capable(dev, false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(device_init_wakeup);

/**
 * device_set_wakeup_enable - Enable or disable a device to wake up the system.
 * @dev: Device to handle.
 */
int device_set_wakeup_enable(struct device *dev, bool enable)
{
	if (!dev || !dev->power.can_wakeup)
		return -EINVAL;

	return enable ? device_wakeup_enable(dev) : device_wakeup_disable(dev);
}
EXPORT_SYMBOL_GPL(device_set_wakeup_enable);

/*
 * The functions below use the observation that each wakeup event starts a
 * period in which the system should not be suspended.  The moment this period
 * will end depends on how the wakeup event is going to be processed after being
 * detected and all of the possible cases can be divided into two distinct
 * groups.
 *
 * First, a wakeup event may be detected by the same functional unit that will
 * carry out the entire processing of it and possibly will pass it to user space
 * for further processing.  In that case the functional unit that has detected
 * the event may later "close" the "no suspend" period associated with it
 * directly as soon as it has been dealt with.  The pair of pm_stay_awake() and
 * pm_relax(), balanced with each other, is supposed to be used in such
 * situations.
 *
 * Second, a wakeup event may be detected by one functional unit and processed
 * by another one.  In that case the unit that has detected it cannot really
 * "close" the "no suspend" period associated with it, unless it knows in
 * advance what's going to happen to the event during processing.  This
 * knowledge, however, may not be available to it, so it can simply specify time
 * to wait before the system can be suspended and pass it as the second
 * argument of pm_wakeup_event().
 *
 * It is valid to call pm_relax() after pm_wakeup_event(), in which case the
 * "no suspend" period will be ended either by the pm_relax(), or by the timer
 * function executed when the timer expires, whichever comes first.
 */

/**
 * wakup_source_activate - Mark given wakeup source as active.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and, if @ws has just been activated, notify the PM
 * core of the event by incrementing the counter of of wakeup events being
 * processed.
 */
static void wakeup_source_activate(struct wakeup_source *ws)
{
	ws->active = true;
	ws->active_count++;
	ws->timer_expires = jiffies;
	ws->last_time = ktime_get();

	atomic_inc(&events_in_progress);
}

/**
 * __pm_stay_awake - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_stay_awake(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);
	ws->event_count++;
	if (!ws->active)
		wakeup_source_activate(ws);
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_stay_awake);

/**
 * pm_stay_awake - Notify the PM core that a wakeup event is being processed.
 * @dev: Device the wakeup event is related to.
 *
 * Notify the PM core of a wakeup event (signaled by @dev) by calling
 * __pm_stay_awake for the @dev's wakeup source object.
 *
 * Call this function after detecting of a wakeup event if pm_relax() is going
 * to be called directly after processing the event (and possibly passing it to
 * user space for further processing).
 */
void pm_stay_awake(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_stay_awake(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_stay_awake);

/**
 * wakup_source_deactivate - Mark given wakeup source as inactive.
 * @ws: Wakeup source to handle.
 *
 * Update the @ws' statistics and notify the PM core that the wakeup source has
 * become inactive by decrementing the counter of wakeup events being processed
 * and incrementing the counter of registered wakeup events.
 */
static void wakeup_source_deactivate(struct wakeup_source *ws)
{
	ktime_t duration;
	ktime_t now;

	ws->relax_count++;
	/*
	 * __pm_relax() may be called directly or from a timer function.
	 * If it is called directly right after the timer function has been
	 * started, but before the timer function calls __pm_relax(), it is
	 * possible that __pm_stay_awake() will be called in the meantime and
	 * will set ws->active.  Then, ws->active may be cleared immediately
	 * by the __pm_relax() called from the timer function, but in such a
	 * case ws->relax_count will be different from ws->active_count.
	 */
	if (ws->relax_count != ws->active_count) {
		ws->relax_count--;
		return;
	}

	ws->active = false;

	now = ktime_get();
	duration = ktime_sub(now, ws->last_time);
	ws->total_time = ktime_add(ws->total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(ws->max_time))
		ws->max_time = duration;

	del_timer(&ws->timer);

	/*
	 * event_count has to be incremented before events_in_progress is
	 * modified, so that the callers of pm_check_wakeup_events() and
	 * pm_save_wakeup_count() don't see the old value of event_count and
	 * events_in_progress equal to zero at the same time.
	 */
	atomic_inc(&event_count);
	smp_mb__before_atomic_dec();
	atomic_dec(&events_in_progress);
}

/**
 * __pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @ws: Wakeup source object associated with the source of the event.
 *
 * Call this function for wakeup events whose processing started with calling
 * __pm_stay_awake().
 *
 * It is safe to call it from interrupt context.
 */
void __pm_relax(struct wakeup_source *ws)
{
	unsigned long flags;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);
	if (ws->active)
		wakeup_source_deactivate(ws);
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_relax);

/**
 * pm_relax - Notify the PM core that processing of a wakeup event has ended.
 * @dev: Device that signaled the event.
 *
 * Execute __pm_relax() for the @dev's wakeup source object.
 */
void pm_relax(struct device *dev)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_relax(dev->power.wakeup);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_relax);

/**
 * pm_wakeup_timer_fn - Delayed finalization of a wakeup event.
 * @data: Address of the wakeup source object associated with the event source.
 *
 * Call __pm_relax() for the wakeup source whose address is stored in @data.
 */
static void pm_wakeup_timer_fn(unsigned long data)
{
	__pm_relax((struct wakeup_source *)data);
}

/**
 * __pm_wakeup_event - Notify the PM core of a wakeup event.
 * @ws: Wakeup source object associated with the event source.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Notify the PM core of a wakeup event whose source is @ws that will take
 * approximately @msec milliseconds to be processed by the kernel.  If @ws is
 * not active, activate it.  If @msec is nonzero, set up the @ws' timer to
 * execute pm_wakeup_timer_fn() in future.
 *
 * It is safe to call this function from interrupt context.
 */
void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	unsigned long flags;
	unsigned long expires;

	if (!ws)
		return;

	spin_lock_irqsave(&ws->lock, flags);

	ws->event_count++;
	if (!ws->active)
		wakeup_source_activate(ws);

	if (!msec) {
		wakeup_source_deactivate(ws);
		goto unlock;
	}

	expires = jiffies + msecs_to_jiffies(msec);
	if (!expires)
		expires = 1;

	if (time_after(expires, ws->timer_expires)) {
		mod_timer(&ws->timer, expires);
		ws->timer_expires = expires;
	}

 unlock:
	spin_unlock_irqrestore(&ws->lock, flags);
}
EXPORT_SYMBOL_GPL(__pm_wakeup_event);


/**
 * pm_wakeup_event - Notify the PM core of a wakeup event.
 * @dev: Device the wakeup event is related to.
 * @msec: Anticipated event processing time (in milliseconds).
 *
 * Call __pm_wakeup_event() for the @dev's wakeup source object.
 */
void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	__pm_wakeup_event(dev->power.wakeup, msec);
	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_wakeup_event);

/**
 * pm_wakeup_update_hit_counts - Update hit counts of all active wakeup sources.
 */
static void pm_wakeup_update_hit_counts(void)
{
	unsigned long flags;
	struct wakeup_source *ws;

	rcu_read_lock();
	list_for_each_entry_rcu(ws, &wakeup_sources, entry) {
		spin_lock_irqsave(&ws->lock, flags);
		if (ws->active)
			ws->hit_count++;
		spin_unlock_irqrestore(&ws->lock, flags);
	}
	rcu_read_unlock();
}

/**
 * pm_check_wakeup_events - Check for new wakeup events.
 *
 * Compare the current number of registered wakeup events with its preserved
 * value from the past to check if new wakeup events have been registered since
 * the old value was stored.  Check if the current number of wakeup events being
 * processed is zero.
 */
bool pm_check_wakeup_events(void)
{
	unsigned long flags;
	bool ret = true;

	spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		ret = ((unsigned int)atomic_read(&event_count) == saved_count)
			&& !atomic_read(&events_in_progress);
		events_check_enabled = ret;
	}
	spin_unlock_irqrestore(&events_lock, flags);
	if (!ret)
		pm_wakeup_update_hit_counts();
	return ret;
}

/**
 * pm_get_wakeup_count - Read the number of registered wakeup events.
 * @count: Address to store the value at.
 *
 * Store the number of registered wakeup events at the address in @count.  Block
 * if the current number of wakeup events being processed is nonzero.
 *
 * Return false if the wait for the number of wakeup events being processed to
 * drop down to zero has been interrupted by a signal (and the current number
 * of wakeup events being processed is still nonzero).  Otherwise return true.
 */
bool pm_get_wakeup_count(unsigned int *count)
{
	bool ret;

	if (capable(CAP_SYS_ADMIN))
		events_check_enabled = false;

	while (atomic_read(&events_in_progress) && !signal_pending(current)) {
		pm_wakeup_update_hit_counts();
		schedule_timeout_interruptible(msecs_to_jiffies(TIMEOUT));
	}

	ret = !atomic_read(&events_in_progress);
	*count = atomic_read(&event_count);
	return ret;
}

/**
 * pm_save_wakeup_count - Save the current number of registered wakeup events.
 * @count: Value to compare with the current number of registered wakeup events.
 *
 * If @count is equal to the current number of registered wakeup events and the
 * current number of wakeup events being processed is zero, store @count as the
 * old number of registered wakeup events to be used by pm_check_wakeup_events()
 * and return true.  Otherwise return false.
 */
bool pm_save_wakeup_count(unsigned int count)
{
	bool ret = false;

	spin_lock_irq(&events_lock);
	if (count == (unsigned int)atomic_read(&event_count)
	    && !atomic_read(&events_in_progress)) {
		saved_count = count;
		events_check_enabled = true;
		ret = true;
	}
	spin_unlock_irq(&events_lock);
	if (!ret)
		pm_wakeup_update_hit_counts();
	return ret;
}
