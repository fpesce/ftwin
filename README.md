# ftwin - Find Twin Files

`ftwin` is a command-line utility for finding and reporting duplicate files. It can identify identical files based on their content or find similar images using the `libpuzzle` library.

**Visit our cool project page at: [fpesce.github.io/ftwin](https://fpesce.github.io/ftwin)**

## Features

- **Find duplicate files by content:** `ftwin` can recursively scan directories and identify files with the same content.
- **Find similar images:** Using `libpuzzle`, `ftwin` can find images that are visually similar, even if they have been resized, recompressed, or slightly modified.
- **Flexible filtering:** You can include or exclude files based on regular expressions, file size, and more.
- **Git-compatible ignore support:** `ftwin` automatically respects `.gitignore` files in your directory tree and includes sensible default ignore patterns for common VCS directories, build artifacts, and temporary files.
- **Customizable output:** The output format can be adjusted to suit your needs, making it easy to pipe the results to other scripts for further processing.
- **Priority path:** You can prioritize a specific path to ensure that files from that location are listed first in the duplicate report, which is useful for cleaning up newly imported files.
- **Archive support:** `ftwin` can search for duplicates within `.tar`, `.gz`, and `.bz2` archives.

## Dependencies

- **[APR (Apache Portable Runtime)](https://apr.apache.org/):** A supporting library for the Apache web server, providing a set of APIs that interface with the underlying operating system.
- **[PCRE (Perl Compatible Regular Expressions)](http://www.pcre.org/):** A library for implementing regular expression pattern matching.
- **[libpuzzle](https://github.com/jedisct1/libpuzzle):** Vendored as a git submodule for finding visually similar images.
- **[libarchive](https://github.com/libarchive/libarchive):** Vendored as a git submodule for reading and writing streaming archive files.

## Installation

To build and install `ftwin`, you will need to have the development headers for the dependencies listed above installed on your system. Then, you can run the following commands:

```bash
# Initialize and update git submodules
git submodule update --init --recursive

# Apply patches to third-party dependencies
./setup.sh

# Configure (If configure script is missing, you may need to run: autoreconf -isf)
./configure
make
sudo make install
```

## Usage

```bash
ftwin [OPTION]... [FILES or DIRECTORIES]...
```

## .gitignore Support

`ftwin` automatically respects `.gitignore` files in your directory tree, providing intelligent filtering of files that you typically don't want to scan for duplicates. This feature works hierarchically, meaning that `.gitignore` files in subdirectories inherit and extend patterns from parent directories.

### Default Ignore Patterns

By default, `ftwin` ignores common files and directories that are rarely useful for duplicate detection:

- **Version Control Systems:** `.git/`, `.hg/`, `.svn/`
- **Build Artifacts:** `build/`, `dist/`, `out/`, `target/`, `bin/`, `*.o`, `*.class`, `*.pyc`, `*.pyo`
- **Dependency Directories:** `node_modules/`, `vendor/`, `.venv/`
- **OS & Editor Files:** `.DS_Store`, `Thumbs.db`, `*.swp`, `*~`, `.idea/`, `.vscode/`

### Custom .gitignore Files

Place `.gitignore` files in any directory to define custom ignore patterns. `ftwin` supports standard Git glob patterns including:

- `*.ext` - Wildcard matching
- `**/*.ext` - Recursive wildcard matching
- `/pattern` - Patterns rooted at the directory containing the `.gitignore`
- `dir/` - Directory-only patterns
- `!exception` - Negation patterns to whitelist files

Example `.gitignore`:
```gitignore
# Ignore all logs except important ones
*.log
!important.log

# Ignore build directory at this level only
/build/

# Ignore all temporary files recursively
**/*.tmp
```

## Output Format

`ftwin` provides a modernized, easy-to-read output format.

*   **Grouping:** Duplicate files are clearly grouped together, separated by blank lines.
*   **Human-Readable Sizes:** When using the `-d` option, file sizes are displayed using standard binary prefixes (KiB, MiB, GiB, etc.).
*   **Automatic Colorization:** When run in an interactive terminal (TTY), the output is colorized to enhance readability (e.g., bold blue paths, bold cyan sizes). Colors are automatically disabled if the output is piped or redirected (e.g., `ftwin . > duplicates.txt`).

## Examples

### Find duplicate pictures

The following command will find duplicate pictures in your home directory, with a minimum size of 8192 bytes, and display the progress:

```bash
ftwin -m 8192 -v -I ${HOME} | less
```

### Find duplicate text files, ignoring `.svn` directories

This command will find all `.txt` files in your home directory while ignoring any files in `.svn` directories:

```bash
ftwin -e ".*/\.svn/.*" -w ".*\.txt$" -v ${HOME}
```

### Clean up imported pictures

If you have imported pictures into a temporary directory and want to remove duplicates, you can use the following commands:

```bash
mkdir "${HOME}/tmppix"
cp /media/SDCARD/*JPG "${HOME}/tmppix"
ftwin -v -w ".*\.(jpe?g)$" -c -p "${HOME}/tmppix" -s "," "${HOME}" | tee log
```

This will find all JPG files in your home directory, prioritize the ones in `tmppix`, and save the list of duplicates to a file named `log`. You can then use the following command to remove the duplicates from `tmppix`:

```bash
cut -d"," -f1 -s < log | grep "tmppix" | while read FILE; do rm -f "${FILE}" ; done
```

### Options

  - **-a, --hidden:** Do not ignore hidden files.
  - **-c, --case-unsensitive:** Make regex matching case-insensitive.
  - **-d, --display-size:** Display the size of the files before listing duplicates.
  - **-e, --regex-ignore-file `REGEX`:** Ignore files with names that match the given regular expression.
  - **-f, --follow-symlink:** Follow symbolic links.
  - **-h, --help:** Display the usage information.
  - **-I, --image-cmp:** Run in image comparison mode (requires `libpuzzle`).
  - **-T, --image-threshold `LEVEL`:** Set the image similarity threshold (1-5, default is 1). A lower value means more similar.
  - **-i, --ignore-list `LIST`:** A comma-separated list of file names to ignore.
  - **-m, --minimal-length `SIZE`:** The minimum size of files to process.
  - **-o, --optimize-memory:** Reduce memory usage at the cost of increased processing time.
  - **-p, --priority-path `PATH`:** Prioritize files from this path in the duplicate report.
  - **-r, --recurse-subdir:** Recursively search subdirectories (default: on).
  - **-R, --no-recurse:** Do not recurse in subdirectories.
  - **-s, --separator `CHAR`:** The character to use as a separator between duplicate file paths (default is `\n`).
  - **-t, --tar-cmp:** Process files within `.tar`, `.gz`, and `.bz2` archives (requires `libarchive`).
  - **-v, --verbose:** Display a progress bar.
  - **-V, --version:** Display the version information.
  - **-w, --whitelist-regex-file `REGEX`:** Only process files with names that match the given regular expression.
  - **-x, --excessive-size `SIZE`:** The file size at which to switch off `mmap` usage.

## Contributing

Please report any bugs or feature requests on the [GitHub issue tracker](https://github.com/fpesce/ftwin/issues).

## License

This project is licensed under the Apache License, Version 2.0. See the `LICENSE` file for more details.
