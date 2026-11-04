//! The amalgamated flecs of this checkout, for a build script (take this crate as a build-dependency):
//! compile [`C`] with `-I` [`DIR`] and whatever `FLECS_*` defines you need. Pin it by git rev; the
//! rev is the flecs version.
#![no_std]

/// The directory holding `flecs.c` and `flecs.h`.
pub const DIR: &str = env!("CARGO_MANIFEST_DIR");
/// The amalgamated source.
pub const C: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/flecs.c");
/// The amalgamated header.
pub const H: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/flecs.h");
