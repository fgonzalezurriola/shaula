#ifndef SHAULA_ERRORS_TAXONOMY_H
#define SHAULA_ERRORS_TAXONOMY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *data;
  size_t length;
} ShaulaErrorSpan;

typedef int32_t ShaulaFailureClass;
enum {
  SHAULA_FAILURE_CLASS_INVALID = -1,
  SHAULA_FAILURE_CLASS_CLI = 0,
  SHAULA_FAILURE_CLASS_COMPOSITOR = 1,
  SHAULA_FAILURE_CLASS_IPC = 2,
  SHAULA_FAILURE_CLASS_BACKEND = 3,
  SHAULA_FAILURE_CLASS_CLIPBOARD = 4,
  SHAULA_FAILURE_CLASS_OUTPUT = 5,
  SHAULA_FAILURE_CLASS_UNKNOWN = 6,
};

typedef int32_t ShaulaRecoveryAction;
enum {
  SHAULA_RECOVERY_ACTION_INVALID = -1,
  SHAULA_RECOVERY_ACTION_FAIL_FAST = 0,
  SHAULA_RECOVERY_ACTION_RETRY_LIMITED = 1,
  SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE = 2,
  SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL = 3,
};

typedef struct {
  ShaulaErrorSpan code;
  ShaulaErrorSpan message;
  uint8_t retryable;
  ShaulaFailureClass failure_class;
  ShaulaRecoveryAction action;
  uint8_t exit_code;
} ShaulaErrorSpec;

_Static_assert(sizeof(ShaulaFailureClass) == 4,
               "ShaulaFailureClass must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaRecoveryAction) == 4,
               "ShaulaRecoveryAction must remain a 32-bit C ABI value");
_Static_assert(SHAULA_FAILURE_CLASS_CLI == 0 &&
                   SHAULA_FAILURE_CLASS_COMPOSITOR == 1 &&
                   SHAULA_FAILURE_CLASS_IPC == 2 &&
                   SHAULA_FAILURE_CLASS_BACKEND == 3 &&
                   SHAULA_FAILURE_CLASS_CLIPBOARD == 4 &&
                   SHAULA_FAILURE_CLASS_OUTPUT == 5 &&
                   SHAULA_FAILURE_CLASS_UNKNOWN == 6,
               "failure-class ABI values changed");
_Static_assert(SHAULA_RECOVERY_ACTION_FAIL_FAST == 0 &&
                   SHAULA_RECOVERY_ACTION_RETRY_LIMITED == 1 &&
                   SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE == 2 &&
                   SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL == 3,
               "recovery-action ABI values changed");

/*
 * Taxonomy records and all returned text spans borrow immutable process-lifetime
 * literals. Callers must not free or modify them. Input spans are borrowed only
 * for the synchronous call and are compared byte-for-byte with explicit lengths;
 * no trimming, case folding, prefix matching, locale processing, or embedded-NUL
 * truncation is performed. A NULL pointer with nonzero length is invalid.
 */
size_t shaula_error_taxonomy_count(void);
const ShaulaErrorSpec *shaula_error_taxonomy_at(size_t index);
const ShaulaErrorSpec *shaula_error_taxonomy_find(ShaulaErrorSpan code);
const ShaulaErrorSpec *shaula_error_taxonomy_spec_for(ShaulaErrorSpan code);
const ShaulaErrorSpec *shaula_error_taxonomy_unknown(void);

ShaulaErrorSpan shaula_failure_class_token(ShaulaFailureClass failure_class);
ShaulaErrorSpan shaula_recovery_action_token(ShaulaRecoveryAction action);

/* Unknown, malformed, empty, or otherwise unmapped codes use the canonical
 * ERR_UNKNOWN_UNMAPPED record. */
uint8_t shaula_error_exit_code_for(ShaulaErrorSpan code);
uint8_t shaula_error_retry_budget_for(ShaulaErrorSpan code);

#ifdef __cplusplus
}
#endif

#endif
