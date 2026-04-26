# PubSub master harness task settings

The current master PubSub integration harness uses PubSub node task priority `3`, `useNotify = true`, `noCatchup = true`, and free core affinity. Do not reintroduce fixed core affinity for the PubSub node tasks in `makePubSubConfig()`: that was the confirmed source of boot-time queue backlog/timeouts in the single-device simulator. Priority changes are unproven performance experiments and should be measured independently.
