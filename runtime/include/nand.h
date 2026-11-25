#ifndef RUNTIME_NAND_H
#define RUNTIME_NAND_H
/* Foreground-only raw NAND diagnostics; no program/erase API. */
int nand_command(const char *args);
#endif
