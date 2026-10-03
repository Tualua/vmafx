- The platform setup scripts (`scripts/setup/*.sh`, `scripts/setup/windows.ps1`)
  end with a configure command that works: `meson setup build core ...` from
  the repository root. They printed `meson setup build ...`, which Meson refuses
  because the root has no `meson.build`. The Windows script also prints the
  `/experimental:c11atomics` compiler flags every MSVC build needs.
