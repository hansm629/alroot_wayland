# Zink S44 submission changes

These patches apply to the accepted S42 Mesa 27 source, not an arbitrary
upstream checkout. See [S44 results](../../../docs/gpu-s44-submit-overhead.md).

- `0001-s44-submit-compaction-profile.patch` removes only an empty FD-wait
  header after a nonempty acquire-wait header. Other wait/command/signal
  headers, extension chains, queue calls, fences and ordering remain.
  `S44_ZINK_SUBMIT_STATS=1` enables aggregate diagnostic output at teardown.
- `0002-zink-submit-default-on.patch` enables compaction by default.
  `S44_ZINK_COMPACT_SUBMITS=0` selects the baseline path for a new process.

The measured GLX submission has three headers instead of four. Native EGL's
probe had no eligible header; neither result implies fewer rendering commands
or removal of mandatory fence waiting. Profiling stays off by default.
