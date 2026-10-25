/* OpenBase on a stack of its own size (SQLite's parser and planner recurse:
 * Design-OpenBase.md 11.2 asks for 64 KB at least).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OB_STACK_H
#define OB_STACK_H
int ob_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv);
#endif
