# fuse8

Ensembl website's new cache thing.

See README.hacking for current status.

v0.01 is currently undergoing live testing on mirror sites. Prior to release the following (at least) will be fixed as bugs found during testing.

* Track number of read/write requests to caches.
* Ability not to clear cachefile between restarts?
* Why are stats quantised to second when tracked in us. Fix that.
* Read/Write locking for file locks
* hit log.
* better lru

Following will be fixed at some point, but probably post initial deployment:

* speculative pulls (warm caches)
* pull metadata
