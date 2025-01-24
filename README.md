# grug

<img src="https://github.com/user-attachments/assets/030798b8-88bb-4c45-ba42-b79bb1b1e12c" width=200 />

See [my blog post](https://mynameistrez.github.io/2024/02/29/creating-the-perfect-modding-language.html) for an introduction to the grug modding language.

You can find its test suite [here](https://github.com/MyNameIsTrez/grug-tests).

`grug.c` contains documentation at the top of the file, and the MIT license at the very bottom. `grug.h` refers to the MIT license in `grug.c`.

## Sections

`grug.c` is composed of sections, which you can jump between by searching for `////` in the file:

1. GRUG DOCUMENTATION
2. INCLUDES AND DEFINES
3. UTILS
4. RUNTIME ERROR HANDLING
5. JSON
6. PARSING MOD API JSON
7. READING
8. TOKENIZATION
9. PARSING
10. DUMPING AST
11. APPLYING AST
12. TYPE PROPAGATION
13. COMPILING
14. LINKING
15. HOT RELOADING

## Programs showcasing grug

- [Modding Minecraft with grug](https://github.com/MyNameIsTrez/grug-toys)
- [Box2D and raylib game](https://github.com/MyNameIsTrez/grug-box2d-and-raylib-game)
- [terminal game: C/C++](https://github.com/MyNameIsTrez/grug-terminal-game-c-cpp)
- [terminal game: Python](https://github.com/MyNameIsTrez/grug-terminal-game-python)
- [terminal game: Java](https://github.com/MyNameIsTrez/grug-terminal-game-java)
- [grug benchmarks](https://github.com/MyNameIsTrez/grug-benchmarks)

## Settings

Search for `#define` in `grug.c` (with Ctrl+F). All the defines are configurable.

If you want to allow your compiler to optimize `grug.c` extra hard, add the compiler flag `-DCRASH_ON_UNREACHABLE`.
