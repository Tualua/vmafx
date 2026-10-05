- **The process log level is safe to set and read from several threads.**
  `vmaf_init()` sets the level (and the stderr tty flag) that `vmaf_log()`
  reads on every thread, worker threads included; both were plain globals, so
  creating contexts on two threads, or one while another context logs, was a
  data race that ThreadSanitizer reports. They are atomic now; the level a
  caller sees and the log output are unchanged.
