- **A feature score of the newest picture can be read with worker threads.**
  With `n_threads > 0`, `vmaf_feature_score_at_index()` and
  `vmaf_feature_score_pooled()` answered `-EINVAL` for a picture already read
  while a worker thread was still computing its score and the score store had
  no slot for it yet (the first picture of a feature, or the ninth). They now
  wait for the worker threads first, as they already did for `-EAGAIN`, and
  return the score. A feature no extractor writes is still `-EINVAL`.
