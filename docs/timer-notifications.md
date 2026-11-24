# Timer and Pomodoro notification recovery

Completion saves a pending notification deadline before attempting delivery.
If the host's four-slot notification queue is full, the stored deadline stays
unchanged and an alarm retries for up to 60 seconds after the original end.
The pending notification survives background termination, reloads, and package
updates, even if storage refuses writes throughout those deliveries.

Queue acceptance clears the deadline in memory, cancels the notification alarm,
and attempts to save the receipt. A refused receipt write is retried by the
foreground retry timer and at termination, without posting again in that
instance. Ordinary successful receipt persistence prevents replay after updates.

The host has no atomic operation combining queue acceptance and receipt storage,
and cannot query or deduplicate notifications. If acceptance succeeds but receipt
storage keeps failing through termination, an update within the remaining retry
window can replay the accepted notification. Recovery favors delivering a
pending notice over silently losing one. Pre-saving the receipt cannot remove
this ambiguity: it loses refused notifications if the queue is full and the
rollback write also fails. A reboot discards notices tied to the previous boot.

Each app's `test/notification_update.c` covers full-queue failures through real
background termination and reload, package replacement, expiry, and the accepted
delivery / refused receipt boundary. `test/notification_retry.c` also checks that
receipt retries in a surviving instance do not post again.
