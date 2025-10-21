# XSC Attribution Strategy

## Goals

- Charge CPU/IO/memory usage to the submitting task’s cgroup and resource limits while a worker thread executes the SQE.
- Maintain accurate audit/tracing metadata (UID/GID, PID/TID, cgroup ID).
- Avoid leaking ownership metadata across submissions.

## Approach

1. Snapshot origin credentials at dequeue (`xsc_task_cred_snapshot`).
   - Grab ref on the submitting task.
   - Cache cgroup pointer, rlimits, UID/GID, pid/tgid, and audit context.
2. Execute via `xsc_run_with_attribution(ctx, ...)`: sets `current->xsc_origin`, swaps in the origin’s audit context, and temporarily reattaches the worker to the origin’s css_set before dispatch. After the handler returns, the previous audit/cgroup/origin state is restored.
3. Release snapshot once the CQE has been posted.

This keeps attribution state consistent even if multiple SQEs are in flight.
```
