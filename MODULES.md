# On-demand / side-loadable module packs

RackDroid ships a set of modules in the base APK. Extra module packs can be
added at runtime, without rebuilding or updating the app, by dropping a pack
file into a user-visible folder on the device.

## Where the user drops packs

```
Android/data/org.rackdroid/files/Modules/
```

Visible in any file manager, no storage permission required. A `README.txt`
is written there on first run. Drop `.rdmod` files, then restart the app —
a toast confirms how many extra packs loaded.

## Pack format (`.rdmod`)

A `.rdmod` is a plain **zip** whose top level contains:

```
plugin.json          # the pack manifest (must have "slug")
res/…                # panel SVGs and other assets it loads at runtime
libplugin_<name>.so  # the native library, built for THIS RackDroid engine
```

The `.so` must be compiled against the same `rack_engine` ABI as the app:
`arm64-v8a` or `x86_64`. Each `.rdmod` contains one ABI, so distribute the two
sets in separate architecture-labelled folders. A mismatched or third-party
`.so` will fail to load and is skipped; a pack whose slug is already loaded
(e.g. bundled) is skipped too.

### Security boundary

A module library is executable native code and runs with RackDroid's app
permissions. RackDroid validates archive paths, manifest/slug syntax, the
single root-level `libplugin_*.so`, 64-bit ELF architecture, entry count and
compressed/uncompressed size limits before loading, but these checks do not
prove who authored the code. Install packs only from a trusted source. The
installer displays this warning before opening the file picker.

Current hard limits are 256 MB per `.rdmod`, 10,000 entries, 128 MB per entry,
512 MB total extracted data, and 1 MB for `plugin.json`. Plugin slugs must match
`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`.

### Building a pack

Build the plugin target as usual, then zip the stripped `.so` together with
its `plugin.json` and `res/`:

```sh
ABI=x86_64 # or arm64-v8a
mkdir pack && cd pack
cp .../obj/$ABI/libplugin_foo.so .
cp path/to/foo/plugin.json .
cp -r path/to/foo/res .
zip -r ../Foo.rdmod .
```

For all official packs, build and package a matching set with:

```sh
./gradlew assembleRelease -PdevKeystore -PallPlugins -PtargetAbis=x86_64
scripts/make_rdmods.sh /tmp/rdmods-x86_64 x86_64
```

## How it loads (native-code constraint)

Android forbids `dlopen()` of a `.so` straight from shared storage on API 24+
(linker namespaces + W^X). So the app:

1. extracts each pack into app-private storage (`filesDir/user/plugins/<slug>`),
2. marks the `.so` read-only and calls Java `System.load()` on it — the one
   path that satisfies the app classloader's linker namespace,
3. hands the already-loaded library to the native side
   (`nativeLoadUserPlugin` → `dlopen(RTLD_NOLOAD)` → register).

See `app/.../ModuleInstaller.kt` and `native/port/static_plugins.cpp`.

## Google Play note

Distributing native code that executes from **outside** Google Play violates
Play policy. This side-load folder is intended for the GitHub/sideload build.
A Play build should deliver extra packs via **Play asset packs / feature
delivery** (Play serves the `.so`), reusing the same install-into-private-
storage + `System.load` loader.
