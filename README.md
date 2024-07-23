# grug

See [my blog post](https://mynameistrez.github.io/2024/02/29/creating-the-perfect-modding-language.html) for an introduction to the grug modding language.

You can find its test suite [here](https://github.com/MyNameIsTrez/grug-tests).

## Sections

`grug.c` is composed of sections, which you can jump between by searching for `////` in the file:

1. UTILS
2. OPENING RESOURCES
3. JSON
4. PARSING MOD API JSON
5. READING
6. TOKENIZATION
7. VERIFY AND TRIM SPACES
8. PARSING
9. PRINTING AST
10. COMPILING
11. LINKING
12. HOT RELOADING

## Small example programs

- [terminal fighting game](https://github.com/MyNameIsTrez/grug-terminal-fighting-game)
- [grug benchmarks](https://github.com/MyNameIsTrez/grug-benchmarks)

## Options

Search for `#define` in `grug.c` (with Ctrl+F). All the defines are configurable.

If you want to allow your compiler to optimize `grug.c` extra hard, add `-DCRASH_ON_UNREACHABLE` during compilation.
