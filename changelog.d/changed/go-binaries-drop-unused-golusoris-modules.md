- The Go binaries take the golusoris modules they use from
  `internal/app/bootstrap` (`bootstrap.Core`, `bootstrap.HTTP`) instead of the
  golusoris root package, which imports every module golusoris has. This drops
  59 unused modules from the build, among them the mail client that pulled
  `golang.org/x/crypto/md4` and the password-hashing helper that pulled
  `golang.org/x/crypto/argon2` (ADR-1899). Configuration keys, endpoints and
  behaviour are unchanged.
