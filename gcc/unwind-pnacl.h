#ifndef UNWIND_PNACL_H
#define UNWIND_PNACL_H

#define STACK_GROWS_DOWNWARD 1

/* Define: __builtin_dwarf_sp_column */
#if defined(__x86_64__)
#define __builtin_dwarf_sp_column() 7
#elif defined(__i386__)
#define __builtin_dwarf_sp_column() 4
#elif defined(__arm__)
#define __builtin_dwarf_sp_column() 13
#elif defined(__mips__)
#define __builtin_dwarf_sp_column() 29
#else
#error "Unknown platform"
#endif

/* Define: DWARF_FRAME_REGISTERS */
#ifdef DWARF_FRAME_REGISTERS
#undef DWARF_FRAME_REGISTERS
#endif

#if defined(__x86_64__) || defined(__i386__)
#define DWARF_FRAME_REGISTERS 17
#elif defined(__arm__)
#define DWARF_FRAME_REGISTERS 16
#elif defined(__mips__)
/*
 * Number of hardware registers we use in PNaCl. We have:
 *
 *  - 32 integer registers
 *  - 32 floating point registers
 *  - 8 condition code registers
 *  - 2 accumulator registers (hi and lo)
 */
#define DWARF_FRAME_REGISTERS 74
#else
#error "Unknown platform"
#endif


/* Define:__builtin_init_dwarf_reg_size_table */

#if defined(__x86_64__)
  /* This should be 8 (64-bits) for all registers.
   * However, unwind-dw2.c has the deeply-ingrained assumption
   * that sizeof(ptr) >= sizeof(machine word).
   * Until this is fixed, we cannot restore the upper 32-bits of
   * registers, and this must be set to 4 to avoid triggering asserts.
   * BUG= http://code.google.com/p/nativeclient/issues/detail?id=1933
   */
#define __builtin_init_dwarf_reg_size_table(table) do { \
  int i; \
  for (i=0; i < DWARF_FRAME_REGISTERS; i++) table[i] = 4; \
} while (0)

#elif defined(__i386__)
/* TODO(robertm): find better documentation for these values */
#define __builtin_init_dwarf_reg_size_table(table) do { \
  int i; \
  for (i = 0; i <= 9; ++i) table[i] = 4; \
  table[10] = 0;   /* not a typo! register not used? */ \
  for (i = 11; i < DWARF_FRAME_REGISTERS; ++i) \
    table[i] = 12;  /* fp registers ? */ \
} while (0)

#elif defined(__arm__)
/* All regs are 32bit = 4 bytes */
#define __builtin_init_dwarf_reg_size_table(table) do { \
  int i; \
  for (i = 0; i < DWARF_FRAME_REGISTERS; ++i) \
    table[i] = 4; \
} while (0)

#elif defined(__mips__)
/* All regs are 32bit = 4 bytes */
#define __builtin_init_dwarf_reg_size_table(table) do { \
  int i; \
  for (i = 0; i < DWARF_FRAME_REGISTERS; ++i) \
    table[i] = 4; \
} while (0)

#else
#error "Unknown platform"
#endif

/* This corresponds to:  __builtin_eh_return_data_regno(0)  */
#if defined(__x86_64__)
#define pnacl_unwind_result0_reg() 0
#elif defined(__i386__)
#define pnacl_unwind_result0_reg() 0
#elif defined(__arm__)
#define pnacl_unwind_result0_reg() 4
#elif defined(__mips__)
#define pnacl_unwind_result0_reg() 4
#else
#error "unknown platform"
#endif

/* This corresponds to:   __builtin_eh_return_data_regno(1) */
#if defined(__x86_64__)
#define pnacl_unwind_result1_reg() 1
#elif defined(__i386__)
#define pnacl_unwind_result1_reg() 2
#elif defined(__arm__)
#define pnacl_unwind_result1_reg() 5
#elif defined(__mips__)
#define pnacl_unwind_result1_reg() 5
#else
#error "unknown platform"
#endif

#endif /* UNWIND_PNACL_H */

