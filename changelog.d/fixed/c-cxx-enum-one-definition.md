- **`VmafVifNameSet` has the same size in C and in C++.** The internal header
  `core/src/feature/nonfinite_score.h` gave the enum a one-byte underlying
  type for C++ and left it `int`-sized in C. No value crossed between the two
  languages, so no score or output was affected; the enum has one definition
  now, and a device-free test rejects a C++-only narrow underlying type in
  any header a C source includes
  ([ADR-1470](docs/adr/1470-c-cxx-shared-enum-one-definition.md)).
