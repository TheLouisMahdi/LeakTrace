/* SPDX-License-Identifier: MIT */
#ifndef LEAKTRACE_H743_FAULT_CONFIG_H
#define LEAKTRACE_H743_FAULT_CONFIG_H
/* Apply overrides to the C and preprocessed assembly files alike. */
#ifndef LT_H743_FAULT_STACK_BYTES
#define LT_H743_FAULT_STACK_BYTES 512
#endif
#ifndef LT_H743_FAULT_AUTO_RESET
#define LT_H743_FAULT_AUTO_RESET 1
#endif
/* Opt in only after removing the four conflicting Cube-generated handlers. */
#ifndef LT_H743_FAULT_OWN_HANDLERS
#define LT_H743_FAULT_OWN_HANDLERS 0
#endif
#endif
