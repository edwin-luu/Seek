# seek

**Instant, ranked search over your notes and PDFs, on your own machine.**

```console
$ seek index ~/Downloads
Indexed 1 folder in 659 ms
  58 files: 58 added, 0 updated, 0 removed, 0 unchanged

$ seek neural network -n 2
1. ~/Downloads/CAI_4863/1_Intro to Deep Learning and Generative AI.pdf  p. 22
   … deep belief networks What Is a Neural Network? • A neural network consists of a series of stacked layers • Each layer contains units connected to previous layer's units …

2. ~/Downloads/CAI_4863/VAE and GANs.pdf  p. 11
   … typically from normal distribution) • Architecture: Deep neural network (often with upsampling layers) • Output: Synthetic data in the target domain • For image generation: • Use transposed convolutions (deconvolutions) or upsampling …

Top 2 of 14 matches - ranked 58 files in 0.02 ms
```

You remember reading something ("which lecture explained the Kalman filter?") but not where.
`grep` can't look inside PDFs and lists matches in whatever order it finds them. Uploading
everything to a chatbot is slow and sends your files to someone else's server. seek reads your
files once, builds an index, and then answers in milliseconds, best match first, with the
page number and a highlighted snippet. Nothing leaves your computer.

## Install

Needs a C++20 compiler (GCC 13+ or Clang 17+), CMake 3.25+, Poppler's C++ library for PDFs, and
GoogleTest for the tests.

```sh
# Ubuntu / Debian
sudo apt install build-essential cmake pkg-config libpoppler-cpp-dev libgtest-dev
# macOS (Homebrew)
brew install cmake pkg-config poppler googletest

cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix ~/.local   # puts `seek` in ~/.local/bin
```

## Usage

```sh
seek index ~/notes ~/Documents   # add folders and read their files
seek kalman filter               # search: files with more of these words rank higher
seek "kalman filter"             # quoted: the exact phrase is required
seek "point cloud" lidar         # phrase required, "lidar" boosts the ranking
seek -n 20 transformer           # show 20 results instead of 10
seek status                      # what's indexed, and how big the index is
seek forget ~/Documents          # stop indexing a folder
seek -- index                    # search for a word that is also a command name
```

You don't need to re-run `seek index` after editing files. **Every search first picks up new,
edited, and deleted files**, and when nothing changed that check takes a few milliseconds.

| Searched for | Means |
| --- | --- |
| `seek kalman filter` | either word; files with both, and with rarer words, rank first |
| `seek "kalman filter"` | the two words next to each other, in that order |
| `seek kalman-filter` | same as the quoted phrase (one argument with several words) |
| `seek '"lidar" fusion'` | `lidar` is required, `fusion` only boosts the ranking |

Reads `.txt`, `.md`, `.markdown`, and `.pdf`. Skips hidden files and folders (`.git`, `.cache`,
...) and `node_modules`. The index is stored at `~/.cache/seek/index.bin` (override with
`--index FILE` or `SEEK_INDEX`). Exit status is 0 when something matched, 1 when nothing did, and
2 on errors, so seek works in scripts like `grep` does. Colors turn off when output isn't a
terminal or `NO_COLOR` is set.

## How it works

```
 seek index ~/notes                          seek "kalman filter" lidar
        |                                                |
        v                                                v
 +---------------+                               +---------------+
 | sync          |  walk folders, compare        | parse_query   |  phrase: kalman filter
 |               |  mtime + size per file        +---------------+  word:   lidar
 +---------------+                                       |
   |         |                                           v
   | deleted | new or changed                    +---------------+
   | files   v                                   | search        |  BM25 score per file,
   |  +---------------+                          | (in memory)   |  every phrase must match
   |  | extract       |  .md / .txt as is,       +---------------+
   |  |               |  PDF page by page                |  top 10 file ids
   |  +---------------+  (Poppler)                       v
   |         | text                              +---------------+
   v         v                                   | snippet       |  re-read only those files,
 +---------------+                               |               |  pick the best window,
 | index         |  tokenize, update posting     +---------------+  highlight the matches
 |               |  lists, save atomically
 +---------------+
         |
         v
 ~/.cache/seek/index.bin  (compact binary format)
```

**Inverted index.** The core data structure works like the index at the back of a textbook: for
every word, a list of the files it appears in and the positions inside each file
(`"kalman" -> [file 3 @ 17, 240], [file 9 @ 4]`). A search looks up a few lists instead of
reading every file. Positions are what make phrase search work: `"kalman filter"` matches
where `filter` sits exactly one position after `kalman`.

**Ranking with BM25.** BM25 is the classic formula behind search engines like Lucene and
Elasticsearch. A file scores higher when:

- it mentions your words more often, with diminishing returns (the tenth "kalman" adds less
  than the second);
- the words are rare across your files ("kalman" in 3 of 400 files counts far more than
  "notes" in 300 of them);
- it is shorter, so a long textbook doesn't win just because it contains every word somewhere.

**Incremental updates.** For each file the index remembers its modification time and size. On
every sync, unchanged files are skipped without being opened, changed files are re-read, and
deleted files are dropped. Removing a document renumbers the rest in a single pass over the
index, so posting lists stay sorted and can be binary-searched.

**Compact, crash-safe file format.** Numbers are written as varints (small numbers take one
byte) and sorted id/position lists are stored as gaps from the previous value (`1040, 1043,
1051` becomes `1040, 3, 8`), so most entries fit in a single byte. The file is written to a
temporary file and then renamed over the old one, which is atomic, so a crash or power loss
mid-save leaves the previous index intact. Loading checks every length, id, and position, so a
damaged file produces a clear error instead of a crash. The output is deterministic: the same
index always saves to the same bytes.

**Snippets come from the original file.** The index stores words and positions, not your text.
For the top results only, seek re-reads the file, finds the window with the most distinct query
words, and highlights them. That keeps the index small (about 6 KB per indexed file in practice).
Control characters are stripped from snippets, so a file can't inject terminal escape codes into
your screen.

**Text handling.** Words are split on punctuation and case-folded. Accented letters stay part
of words (`café`), typographic punctuation common in PDFs (curly quotes, dashes, bullets)
separates words, and PDF ligatures are expanded (`ﬁlter` -> `filter`), so copy-pasted search
terms match.

## Project layout

```
include/seek/     public headers: one module per file, documented in place
  tokenizer.hpp     text -> normalized words with byte offsets
  extract.hpp       file -> text (plain text, or PDF via Poppler, page by page)
  index.hpp         the inverted index, its persistence, and incremental removal
  sync.hpp          folder walk + "what changed since last time"
  search.hpp        query parsing and BM25 ranking
  snippet.hpp       best excerpt + highlight ranges
src/              implementations, plus main.cpp (the CLI) and binary_io.hpp (varint I/O)
tests/            GoogleTest suite; test PDFs are generated in code, no binary fixtures
```

The engine is a library (`seek_core`) and the CLI is a thin layer on top, so the tests exercise
exactly the code the CLI runs.

## Development

```sh
cmake --preset dev            # Debug + AddressSanitizer + UBSan + warnings as errors
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset compiles with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`
as errors, and runs every test under AddressSanitizer and UndefinedBehaviorSanitizer, which
catch memory bugs (use-after-free, out-of-bounds reads) and undefined behavior at runtime.

Code style is enforced with `clang-format` (`.clang-format` in the repo root). Memory is
managed with RAII throughout: no `new`/`delete`, and the raw pointers Poppler hands back are
wrapped in `std::unique_ptr` right away.

## Limitations

- **Exact word matching.** `run` doesn't match `running`, and typos don't match. See the roadmap.
- **Case folding covers ASCII and Latin-1 letters.** Other scripts (Greek, Cyrillic, CJK) are
  indexed and searchable, but case-sensitively.
- **Scanned PDFs** (images of text) have no text layer, so they're indexed as empty. That needs OCR.
- **Files that fail to read** (password-protected or damaged PDFs) are reported by `seek index`
  and retried on the next refresh.
- The whole index is loaded into memory per search. That's fine for tens of thousands of notes,
  and the format can be memory-mapped later if needed.

## Roadmap

- [ ] Stemming (`running` -> `run`) with the Porter stemmer
- [ ] Typo tolerance (edit distance against the index vocabulary)
- [ ] Interactive mode: results update as you type
- [ ] Read changed files on several threads during indexing
- [ ] `.docx` and source-code files
- [ ] Benchmark suite (Google Benchmark) with a reproducible synthetic corpus
