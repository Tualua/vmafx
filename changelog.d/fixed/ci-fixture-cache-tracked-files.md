- **The CI fixture cache no longer replaces tracked test fixtures with an
  older revision.** The `python/test/resource` cache of the Builds, Build and
  Tests workflows also held the files git tracks there; a `restore-keys` hit
  wrote the revision of the run that saved the cache over the checkout, so a
  dataset fixture changed by a later commit came back without its new fields
  and every Ubuntu tox leg failed (`KeyError: 'dis_enc_width'`). The step that
  prunes unusable restored fixtures now takes `--restore-tracked` and puts
  every tracked file back to the checked-out revision.
