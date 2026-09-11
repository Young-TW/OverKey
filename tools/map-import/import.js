#!/usr/bin/env node
// OverKey beatmap importer: pulls local osu!(lazer) and Quaver beatmaps into
// OverKey's maps/ folder, so the game sees them on next song-select scan.
//
//   osu!(lazer)  reads client.realm (Realm DB) + the content-addressed files/
//                store, and materialises each beatmap set as maps/Lazer/<set>/
//                (mania sets only by default; --all imports every ruleset).
//   Quaver       finds the Songs directory (Steam install or --quaver) and
//                mirrors each song folder into maps/Quaver/<song>/.
//
// Files are hard-linked by default (zero extra disk, instant) with a copy
// fallback for cross-filesystem setups; use --copy to force copying.
// Re-running is safe: folders that already exist are skipped, never modified.
//
// Usage: node import.js [options]   (see --help)

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

// ---------------------------------------------------------------- CLI parsing

const USAGE = `Usage: node import.js [options]

Sources (auto-detected when the flag is omitted):
  --lazer <dir>    osu!(lazer) data dir containing client.realm
                   (default: ~/.local/share/osu, honoring storage.ini FullPath)
  --quaver <dir>   Quaver install dir (the one with Songs/ and quaver.cfg)
                   (default: auto-detect under all Steam libraries)
  --no-lazer       skip osu!(lazer)
  --no-quaver      skip Quaver

What / how:
  --maps <dir>     OverKey maps dir (default: <repo>/maps)
  --all            lazer: import every ruleset, not just mania
  --copy           copy files instead of hard-linking
  --dry-run        report what would be imported without writing anything
  -h, --help       show this help
`;

function parseArgs(argv) {
  const opt = {
    lazer: null,
    quaver: null,
    maps: null,
    all: false,
    copy: false,
    dryRun: false,
    noLazer: false,
    noQuaver: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const needValue = (name) => {
      if (i + 1 >= argv.length) throw new Error(`${name} requires a value`);
      return argv[++i];
    };
    switch (a) {
      case "--lazer": opt.lazer = needValue(a); break;
      case "--quaver": opt.quaver = needValue(a); break;
      case "--maps": opt.maps = needValue(a); break;
      case "--all": opt.all = true; break;
      case "--copy": opt.copy = true; break;
      case "--dry-run": opt.dryRun = true; break;
      case "--no-lazer": opt.noLazer = true; break;
      case "--no-quaver": opt.noQuaver = true; break;
      case "-h":
      case "--help":
        console.log(USAGE);
        process.exit(0);
      default:
        throw new Error(`unknown option: ${a} (see --help)`);
    }
  }
  return opt;
}

// ------------------------------------------------------------- path utilities

// Forbidden on Windows / problematic everywhere; keep folder names portable.
function sanitizeName(name) {
  return name
    .replace(/\s+/g, " ")
    .replace(/[<>:"/\\|?*\u0000-\u001f\u007f]/g, "_")
    .replace(/^[ .]+|[ .]+$/g, "")
    .slice(0, 100);
}

// Beatmap-set file names may contain (sub)paths or odd characters; the game
// resolves audio/background by bare file name, so flatten to a safe basename.
function sanitizeFileName(name) {
  const base = name.replace(/\\/g, "/").split("/").pop() || "file";
  return sanitizeName(base) || "file";
}

function* walkFiles(dir) {
  for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    if (ent.isDirectory()) yield* walkFiles(p);
    else if (ent.isFile()) yield p;
  }
}

// Read `Key = Value` from a loose INI-ish file (osu! storage.ini, quaver.cfg).
function readIniValue(file, key) {
  try {
    const m = new RegExp(`^\\s*${key}\\s*=\\s*(.+?)\\s*$`, "im").exec(
      fs.readFileSync(file, "utf8")
    );
    return m ? m[1] : null;
  } catch {
    return null;
  }
}

function isDir(p) {
  try {
    return fs.statSync(p).isDirectory();
  } catch {
    return false;
  }
}

// --------------------------------------------------------------- file copying

function linkOrCopy(src, dst, forceCopy) {
  try {
    if (!forceCopy) fs.linkSync(src, dst);
    else fs.copyFileSync(src, dst, fs.constants.COPYFILE_EXCL);
    return "linked";
  } catch (e) {
    if (e.code === "EEXIST") return "exists";
    if (forceCopy) throw e;
    // Cross-device link etc.: fall back to a real copy.
    fs.copyFileSync(src, dst, fs.constants.COPYFILE_EXCL);
    return "copied";
  }
}

// ----------------------------------------------------------- osu!(lazer) part

function findLazerDir(cliDir) {
  if (cliDir) {
    if (!fs.existsSync(path.join(cliDir, "client.realm")))
      throw new Error(`${cliDir}: no client.realm found — not a lazer data dir?`);
    return cliDir;
  }
  const xdg = process.env.XDG_DATA_HOME || path.join(os.homedir(), ".local", "share");
  const def = path.join(xdg, "osu");
  if (!isDir(def)) throw new Error(`lazer data dir not found at ${def} (use --lazer)`);
  // A custom storage location is recorded in storage.ini inside the default dir.
  const fullPath = readIniValue(path.join(def, "storage.ini"), "FullPath");
  if (fullPath && isDir(fullPath)) return fullPath;
  return def;
}

function lazerBlobPath(filesDir, hash) {
  return path.join(filesDir, hash[0], hash.slice(0, 2), hash);
}

function setFolderName(set) {
  const md = set.Beatmaps.find((b) => b.Metadata)?.Metadata;
  const artist = (md && (md.Artist || md.ArtistUnicode)) || "Unknown Artist";
  const title = (md && (md.Title || md.TitleUnicode)) || "Unknown Title";
  const prefix = set.OnlineID > 0 ? `${set.OnlineID} ` : "";
  const name = sanitizeName(`${prefix}${artist} - ${title}`);
  if (name) return name;
  try {
    return `lazer-${set.ID.toHexString().slice(0, 8)}`;
  } catch {
    return "lazer-unknown";
  }
}

async function importLazer(opt, mapsDir, stats) {
  const lazerDir = findLazerDir(opt.lazer);
  console.log(`[lazer] data dir: ${lazerDir}`);
  const filesDir = path.join(lazerDir, "files");
  if (!isDir(filesDir)) throw new Error(`${filesDir} missing — no files store?`);

  let Realm;
  try {
    Realm = require("realm");
  } catch {
    throw new Error(
      "cannot load the 'realm' package. Run 'npm install' in tools/map-import " +
        "(if install scripts were blocked: npm install-scripts approve realm && npm rebuild realm)"
    );
  }
  // Dynamic open (schema read from the file) + read-only: never mutates lazer's DB.
  const realm = new Realm({ path: path.join(lazerDir, "client.realm"), readOnly: true });
  try {
    const sets = realm.objects("BeatmapSet");
    let done = 0;
    for (const set of sets) {
      done++;
      if (done % 100 === 0) console.log(`[lazer] scanned ${done}/${sets.length} sets…`);
      try {
        if (set.DeletePending) { stats.lazer.deleted++; continue; }
        if (!opt.all &&
            ![...set.Beatmaps].some((b) => b.Ruleset && b.Ruleset.ShortName === "mania")) {
          stats.lazer.nonMania++;
          continue;
        }
        const dest = path.join(mapsDir, "Lazer", setFolderName(set));
        if (isDir(dest)) { stats.lazer.exists++; continue; }

        for (const usage of set.Files) {
          const hash = usage.File && usage.File.Hash;
          const fileName = usage.Filename && sanitizeFileName(usage.Filename);
          if (!hash || !fileName) { stats.lazer.missing++; continue; }
          const src = lazerBlobPath(filesDir, hash);
          if (!fs.existsSync(src)) { stats.lazer.missing++; continue; }
          if (!opt.dryRun) {
            fs.mkdirSync(dest, { recursive: true });
            const r = linkOrCopy(src, path.join(dest, fileName), opt.copy);
            if (r === "copied") stats.lazer.copied++;
          }
          stats.lazer.files++;
        }
        stats.lazer.sets++;
      } catch (e) {
        stats.lazer.errors++;
        console.warn(`[lazer] ${e.message}`);
      }
    }
  } finally {
    realm.close();
  }
}

// ---------------------------------------------------------------- Quaver part

function steamLibraryRoots() {
  const home = os.homedir();
  const roots = [
    path.join(home, ".local", "share", "Steam"),
    path.join(home, ".steam", "steam"),
    path.join(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam"),
    path.join(home, "snap", "steam", "common", ".local", "share", "Steam"),
  ];
  const libs = [...roots];
  for (const root of roots) {
    const vdf = path.join(root, "steamapps", "libraryfolders.vdf");
    try {
      const txt = fs.readFileSync(vdf, "utf8");
      for (const m of txt.matchAll(/"path"\s+"((?:[^"\\]|\\.)*)"/g))
        libs.push(m[1].replace(/\\\\/g, "\\"));
    } catch { /* no extra libraries */ }
  }
  return [...new Set(libs)];
}

function findQuaverDir(cliDir) {
  if (cliDir) return cliDir;
  for (const lib of steamLibraryRoots()) {
    const candidate = path.join(lib, "steamapps", "common", "Quaver");
    if (isDir(candidate)) return candidate;
  }
  throw new Error("Quaver install not found under any Steam library (use --quaver)");
}

function quaverSongsDir(quaverDir) {
  const cfg = path.join(quaverDir, "quaver.cfg");
  const custom = readIniValue(cfg, "SongDirectory");
  if (custom) return custom.replace(/^["']|["']$/g, "");
  return path.join(quaverDir, "Songs");
}

function importQuaver(opt, mapsDir, stats) {
  const quaverDir = findQuaverDir(opt.quaver);
  const songsDir = quaverSongsDir(quaverDir);
  console.log(`[quaver] songs dir: ${songsDir}`);
  if (!isDir(songsDir)) throw new Error(`${songsDir} is not a directory`);

  for (const ent of fs.readdirSync(songsDir, { withFileTypes: true })) {
    if (!ent.isDirectory()) continue;
    const srcDir = path.join(songsDir, ent.name);
    let files;
    try {
      files = [...walkFiles(srcDir)];
    } catch (e) {
      stats.quaver.errors++;
      console.warn(`[quaver] ${ent.name}: ${e.message}`);
      continue;
    }
    stats.quaver.sets++; // every subfolder is a mapset (Quaver guarantees this)
    if (!files.some((f) => f.toLowerCase().endsWith(".qua"))) {
      stats.quaver.noQua++;
      continue;
    }
    const dest = path.join(mapsDir, "Quaver", sanitizeName(ent.name));
    if (isDir(dest)) { stats.quaver.exists++; continue; }
    for (const src of files) {
      const rel = path.relative(srcDir, src);
      const dst = path.join(dest, path.dirname(rel), sanitizeFileName(path.basename(rel)));
      if (!opt.dryRun) {
        fs.mkdirSync(path.dirname(dst), { recursive: true });
        if (linkOrCopy(src, dst, opt.copy) === "copied") stats.quaver.copied++;
      }
      stats.quaver.files++;
    }
    stats.quaver.imported++;
  }
}

// ---------------------------------------------------------------------- main

async function main() {
  const opt = parseArgs(process.argv.slice(2));
  const mapsDir = path.resolve(opt.maps || path.join(__dirname, "..", "..", "maps"));

  const stats = {
    lazer: { sets: 0, files: 0, copied: 0, missing: 0, exists: 0, nonMania: 0, deleted: 0, errors: 0 },
    quaver: { sets: 0, imported: 0, files: 0, copied: 0, exists: 0, noQua: 0, errors: 0 },
  };

  console.log(
    `OverKey map import → ${mapsDir}${opt.dryRun ? "  (dry-run)" : ""}${opt.copy ? "  (copy mode)" : ""}`
  );
  if (!opt.dryRun) fs.mkdirSync(mapsDir, { recursive: true });

  if (!opt.noLazer) {
    try {
      await importLazer(opt, mapsDir, stats);
    } catch (e) {
      console.warn(`[lazer] skipped: ${e.message}`);
    }
  }
  if (!opt.noQuaver) {
    try {
      importQuaver(opt, mapsDir, stats);
    } catch (e) {
      console.warn(`[quaver] skipped: ${e.message}`);
    }
  }

  const l = stats.lazer, q = stats.quaver;
  console.log(`
[lazer]  imported ${l.sets} set(s), ${l.files} file(s)${l.copied ? `, ${l.copied} copied (link failed)` : ""}
         skipped: ${l.exists} already imported, ${l.nonMania} non-mania, ${l.deleted} deleted
         missing blobs: ${l.missing}, errors: ${l.errors}
[quaver] imported ${q.imported}/${q.sets} mapset(s), ${q.files} file(s)${q.copied ? `, ${q.copied} copied (link failed)` : ""}
         skipped: ${q.exists} already imported, ${q.noQua} without .qua, errors: ${q.errors}`);
  if (opt.dryRun) console.log("(dry-run: nothing was written)");
}

// realm's native layer keeps Node's event loop alive even after close(), so
// flush stdout and exit explicitly once the work is done.
function flushAndExit(code) {
  process.stdout.write("", () => process.exit(code));
}

main()
  .then(() => flushAndExit(0))
  .catch((e) => {
    console.error(`error: ${e.message}`);
    flushAndExit(1);
  });
