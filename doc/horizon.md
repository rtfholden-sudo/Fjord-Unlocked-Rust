# Horizon (Minecraft in Rust)

Fjord Launcher can create and run instances of
[Horizon](https://github.com/horizon-rust/horizon), a from-scratch
reimplementation of Minecraft: Java Edition written in Rust. Horizon replaces
the Java client with a native program; your worlds, saves and servers work the
same.

> NOT AN OFFICIAL MINECRAFT PRODUCT. NOT APPROVED BY OR ASSOCIATED WITH
> MOJANG OR MICROSOFT.

## Creating a Horizon instance

Choose **Add Instance**, then the **Horizon** tab and press OK. The launcher
downloads the newest Horizon build (the *Nightly*, ~75 MB) from the project's
GitHub releases, checks it against the release's `SHA256SUMS`, unpacks it into
the instance's `horizon` folder, and asks the build which Minecraft version
it needs (currently 26.3). That version is added to the instance as a normal
component, so its files are shared with your other instances.

Each instance keeps the Horizon build it was created with. To move to a newer
Nightly, create a new instance and copy your saves over (worlds live in the
usual `minecraft/saves` folder of the instance).

## First launch: preparing resources

Horizon ships none of Mojang's code, textures or sounds: it prepares them
from the files of **your own copy of Minecraft: Java Edition**. The first
time you press Play on a Horizon instance, the launcher:

1. downloads the pinned Minecraft version's client jar, libraries and assets
   with the usual machinery (the same files a Java instance would use),
2. downloads that version's **server jar** and version JSON from Mojang
   (`piston-meta.mojang.com`), checked against Mojang's SHA-1 hashes,
3. runs Horizon's own `horizon-prepare --prepare --complete`, which turns
   those files into the resources in `horizon/prepared`.

This takes a few minutes and happens once per build. Later launches check
the resources (`horizon-prepare --check`) and start immediately.

Preparation runs Mojang's data generators on Java, so the instance needs a
working Java of the version the build asks for (the Java steps run as usual
before the first launch; Mojang's downloadable runtimes work). The game
itself needs no Java at all.

## Playing

The instance launches like any other: the account you are signed in with is
handed to the game as its session (through standard input, never on the
command line, where other programs could read the token). Window size,
fullscreen, pre/post-launch commands, wrapper commands, "join server/world
on launch" and quick play behave as they do for Java instances and are
translated to Horizon's options. Only the options the installed build
advertises are used, as its Launch API contract requires.

To play online you must be signed in with a Microsoft account that owns
Minecraft: Java Edition, exactly as with the Java game; single-player also
works offline, in which case online-mode servers will refuse the connection.

Worlds, options, resource packs and screenshots live in the instance's
`minecraft` folder, so the Worlds, Servers and Screenshots instance tabs
work as usual. Horizon mods go in `minecraft/horizon-mods` and are managed
by Horizon itself; the launcher's Java mod management does not apply.

## Under the hood (for pack and launcher developers)

The integration implements Horizon's public
[Launch API](https://github.com/horizon-rust/horizon/blob/main/docs/api/launch-guide.md)
(0.1.x): builds come from `horizon-rust/horizon` GitHub releases, `horizon-client
--launch-api` / `horizon-prepare --launch-api` tell the launcher what a build
accepts, and resources are prepared and checked with `horizon-prepare`.
Relevant code:

- `launcher/ui/pages/modplatform/HorizonPage` — the "Horizon" tab.
- `launcher/modplatform/horizon/HorizonInstanceCreationTask` — download,
  checksum verification, unpacking, and instance creation.
- `launcher/minecraft/launch/PrepareHorizonResources` — first-launch
  preparation (`--check` / `--prepare --progress json`).
- `launcher/minecraft/launch/HorizonPartLaunch` — runs `horizon-client`
  with `--resources`, `--game-dir`, `--home` and `--session-stdin`.
- `horizon/` in the instance root holds the build, the prepared resources
  and the Mojang input files (`horizon/minecraft`).

Checksums downloaded over HTTPS are verified for both the Horizon build
(SHA-256 from the release's `SHA256SUMS`) and Mojang's files (SHA-1 from
Mojang's version JSON). Horizon's releases also carry an Ed25519 signature
of `SHA256SUMS`; verifying it is a possible future addition.
