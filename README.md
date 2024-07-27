# grug

See [my blog post](https://mynameistrez.github.io/2024/02/29/creating-the-perfect-modding-language.html) for an introduction to the grug modding language.

You can find its test suite [here](https://github.com/MyNameIsTrez/grug-tests).

`grug.c` contains an MIT license at the very bottom, so you don't need to copy the `LICENSE` file at the root of this project to your repository. `grug.h` has the same MIT license.

## Sections

`grug.c` is composed of sections, which you can jump between by searching for `////` in the file:

1. GRUG DOCUMENTATION
2. INCLUDES AND DEFINES
3. UTILS
4. OPENING RESOURCES
5. JSON
6. PARSING MOD API JSON
7. READING
8. TOKENIZATION
9. VERIFY AND TRIM SPACES
10. PARSING
11. PRINTING AST
12. COMPILING
13. LINKING
14. HOT RELOADING

## Small example programs

- [terminal fighting game](https://github.com/MyNameIsTrez/grug-terminal-fighting-game)
- [grug benchmarks](https://github.com/MyNameIsTrez/grug-benchmarks)

## Options

Search for `#define` in `grug.c` (with Ctrl+F). All the defines are configurable.

If you want to allow your compiler to optimize `grug.c` extra hard, add `-DCRASH_ON_UNREACHABLE` during compilation.
