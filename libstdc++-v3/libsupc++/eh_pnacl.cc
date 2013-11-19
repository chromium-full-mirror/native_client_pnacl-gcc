// -*- C++ -*- Exception handling routines for PNaCl.
// Copyright (C) 2013
// Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <typeinfo>
#include "unwind-cxx.h"

using namespace __cxxabiv1;


// Exception info written by ExceptionInfoWriter.cpp.

struct action_table_entry
{
  int32_t clause_id;
  uint32_t next_clause_list_id;
};

extern const struct action_table_entry __pnacl_eh_action_table[];
extern const std::type_info *const __pnacl_eh_type_table[];
extern const int32_t __pnacl_eh_filter_table[];

// Data structures used by PNaClSjLjEH.cpp.

struct landing_pad_result
{
  void *exception_obj;
  uint32_t matched_clause_id;
};

struct exception_frame
{
  union
  {
    jmp_buf jmpbuf;
    struct landing_pad_result result;
  };
  struct exception_frame *next;
  uint32_t clause_list_id;
};

__thread struct exception_frame *__pnacl_eh_stack;


// This is based on get_adjusted_ptr() in eh_personality.cc.
//
// This returns whether an exception (specified by the pointer,
// *THROWN_PTR_P, to an exception of type THROW_TYPE) matches the type
// CATCH_TYPE.  That is, it returns whether THROW_TYPE is a subtype of
// CATCH_TYPE.
//
// If there's a match, get_adjusted_ptr() adjusts *THROWN_PTR_P to
// upcast it to be a pointer to type CATCH_TYPE.  (For example, if
// THROW_TYPE uses multiple inheritance and derives from multiple base
// classes, this might involve adding a constant offset to
// *THROWN_PTR_P.)
static bool
get_adjusted_ptr (const std::type_info *catch_type,
                  const std::type_info *throw_type,
                  void **thrown_ptr_p)
{
  if (catch_type == NULL)
    return true;

  void *thrown_ptr = *thrown_ptr_p;

  // Pointer types need to adjust the actual pointer, not
  // the pointer to pointer that is the exception object.
  // This also has the effect of passing pointer types
  // "by value" through the __cxa_begin_catch return value.
  if (throw_type->__is_pointer_p ())
    thrown_ptr = *(void **) thrown_ptr;

  if (catch_type->__do_catch (throw_type, &thrown_ptr, 1))
    {
      *thrown_ptr_p = thrown_ptr;
      return true;
    }

  return false;
}

// Returns whether the thrown exception (specified by THROW_TYPE and
// OBJ) matches one of the exception types in a C++ exception
// specification (specified by FILTER_ID).
static bool
check_exception_spec (const std::type_info *throw_type, void *obj,
                      int32_t filter_id)
{
  const int32_t *filter_ptr =
      &__pnacl_eh_filter_table[-filter_id - 1];
  for (; *filter_ptr != 0; ++filter_ptr)
    {
      const std::type_info *catch_type = __pnacl_eh_type_table[*filter_ptr - 1];
      // We ignore the modified value of obj here.
      if (get_adjusted_ptr (catch_type, throw_type, &obj))
        return true;
    }
  // No type matched, so we have an exception specification error.
  return false;
}

// Returns whether the thrown exception (specified by THROW_TYPE and
// OBJ) matches the given landingpad clause (CLAUSE_ID).
//
// If the exception matches and the clause is a "catch" clause, this
// adjusts *OBJ to upcast it to the type specified in the "catch"
// clause.
static bool
does_clause_match (const std::type_info *throw_type, void **obj,
                   int32_t clause_id)
{
  // Handle "cleanup" clause.
  if (clause_id == 0)
    return true;

  // Handle "filter" clause.
  if (clause_id < 0)
    return !check_exception_spec (throw_type, obj, clause_id);

  // Handle "catch" clause.
  const std::type_info *catch_type = __pnacl_eh_type_table[clause_id - 1];
  return get_adjusted_ptr (catch_type, throw_type, obj);
}

// Returns whether the given FRAME should be entered in order to
// handle the thrown exception (specified by THROW_TYPE and *OBJ).  If
// so, this adjusts *OBJ (see get_adjusted_ptr()) and sets
// *RESULT_CLAUSE_ID.
static bool
does_frame_match (const std::type_info *throw_type, void **obj,
                  struct exception_frame *frame, int32_t *result_clause_id)
{
  for (int32_t clause_list_id = frame->clause_list_id; clause_list_id != 0; )
    {
      const struct action_table_entry *list_node =
          &__pnacl_eh_action_table[clause_list_id - 1];
      if (does_clause_match (throw_type, obj, list_node->clause_id))
        {
          *result_clause_id = list_node->clause_id;
          return true;
        }
      clause_list_id = list_node->next_clause_list_id;
    }
  return false;
}

// Search for a stack frame that will handle the given exception,
// starting from FRAME.  The exception is specified by THROW_TYPE and
// *OBJ.
//
// If a frame is found that will handle the exception, this adjusts
// *OBJ (to upcast it to the "catch" type, if there is one), sets
// *RESULT_FRAME and *RESULT_CLAUSE_ID to the frame and clause ID that
// matched the exception, and returns true.
static bool
find_match (const std::type_info *throw_type, void **obj,
            struct exception_frame *frame,
            struct exception_frame **result_frame, int32_t *result_clause_id)
{
  for (; frame != NULL; frame = frame->next)
    {
      if (does_frame_match (throw_type, obj, frame, result_clause_id))
        {
          *result_frame = frame;
          return true;
        }
    }
  return false;
}

// Search for a non-cleanup stack frame that will handle the given
// exception, starting from FRAME.  Returns whether a matching frame
// was found.
static bool
is_exception_caught (const std::type_info *throw_type, void *obj,
                     struct exception_frame *frame)
{
  for (; frame != NULL; frame = frame->next)
    {
      int32_t clause_id;
      if (does_frame_match (throw_type, &obj, frame, &clause_id)
          && clause_id != 0)
        return true;
    }
  return false;
}

// handle_exception() is called by _Unwind_RaiseException().  It
// unwinds the stack, looking for the first C++ destructor or catch()
// block to pass control to.  In LLVM terms, it searches for the first
// matching invoke/landingpad instruction.  When it finds a match,
// this implementation passes control to the landingpad block by
// longjmp()'ing to it.
//
// In a traditional implementation (based on the Itanium ABI),
// _Unwind_RaiseException() is implemented in a library (libgcc_eh)
// that is separate from libsupc++.  It calls back to libsupc++'s
// personality function (__gxx_personality_v0()) to determine whether
// a call on the stack has a handler for the exception.
// __gxx_personality_v0() knows that UE_HEADER was allocated by
// libsupc++'s __cxa_allocate_exception() and can downcast it to
// libsupc++'s __cxa_exception type.
//
// In contrast, in the implementation below, the functionality of the
// personality function is folded into _Unwind_RaiseException(), so
// this implements C++-specific matching of exceptions and downcasts
// UE_HEADER to __cxa_exception immediately.
//
// This function returns if stack unwinding did not find any stack
// frames that match the exception being thrown.
static void
handle_exception (struct _Unwind_Exception *ue_header, bool check_for_catch)
{
  __cxa_exception *xh = __get_exception_header_from_ue (ue_header);

  void *obj = __get_object_from_ue (ue_header);
  struct exception_frame *frame;
  int32_t clause_id;
  if (find_match (xh->exceptionType, &obj, __pnacl_eh_stack,
                  &frame, &clause_id))
    {
      // Check that there is a non-cleanup handler for the exception.
      // If not, we should abort before running cleanup handlers
      // (i.e. destructors).
      //
      // This is mainly a convenience for debugging.  It means that if
      // the program throws an uncaught exception, the location of the
      // "throw" will be on the stack when the program aborts.  If we
      // ran cleanup handlers before aborting, this context would be
      // lost.
      //
      // This is optional in the C++ standard, which says "If no
      // matching handler is found, the function std::terminate() is
      // called; whether or not the stack is unwound before this call
      // to std::terminate() is implementation-defined".
      if (check_for_catch && clause_id == 0 &&
          !is_exception_caught (xh->exceptionType, obj, frame->next))
        return;

      __pnacl_eh_stack = frame->next;

      // Save adjusted exception pointer so that it can be returned by
      // __cxa_begin_catch() when entering a catch() block.
      xh->adjustedPtr = obj;

      // Save the clause ID so that if the landingpad block calls
      // __cxa_call_unexpected() and the std::set_unexpected() handler
      // throws an exception, we can re-check that exception against
      // the exception specification.
      xh->handlerSwitchValue = clause_id;

      // exception_frame uses the same location for storing the
      // jmp_buf and the landing_pad_result, so we must make a copy of
      // the jmp_buf first.
      jmp_buf jmpbuf_copy;
      memcpy (&jmpbuf_copy, &frame->jmpbuf, sizeof (jmpbuf_copy));

      // Return to the landingpad block, passing it two values.
      frame->result.exception_obj = ue_header;
      frame->result.matched_clause_id = clause_id;
      longjmp (jmpbuf_copy, 1);
    }
}


// Each __pnacl_eh_sjlj_Unwind_*() function below provides the
// definition of _Unwind_*().
//
// The "__pnacl_eh_sjlj" prefix is added so that PNaCl's SJLJ
// (setjmp()/longjmp()-based) implementation of C++ exception handling
// can coexist with other implementations in the same build of
// libstdc++/libsupc++.  When SJLJ EH is enabled, each
// __pnacl_eh_sjlj_Unwind_*() symbol will get renamed to _Unwind_*()
// when linking a PNaCl pexe.


// This implements _Unwind_RaiseException().  This is called when
// raising an exception for the first time, i.e. for the statement
// "throw EXPR;".  The compiler lowers "throw EXPR;" to:
//  * a call to __cxa_allocate_exception() to allocate memory;
//  * a call to __cxa_throw() which throws the exception by calling
//    _Unwind_RaiseException().
extern "C" _Unwind_Reason_Code
__pnacl_eh_sjlj_Unwind_RaiseException (struct _Unwind_Exception *ue_header)
{
  handle_exception (ue_header, true);
  return _URC_END_OF_STACK;
}

// This is the equivalent of _Unwind_Resume() from libgcc_eh, but we
// use a different name for PNaCl SJLJ to avoid accidental collisions
// with libgcc_eh.
//
// This is called by a landingpad block as a final step after it has
// run C++ destructors.  This is only called by a landingpad if it did
// not enter a catch() block.
//
// This function never returns.
extern "C" void
__pnacl_eh_resume (struct _Unwind_Exception *ue_header)
{
  // Pass check_for_catch=false so that unwinding does not take O(n^2)
  // time in the number of cleanup landingpads entered before entering
  // the catch() block.
  handle_exception (ue_header, false);

  // We've run C++ destructors (cleanup handlers), but no further
  // handlers were found, so abort.  We should not reach here, because
  // __pnacl_eh_sjlj_Unwind_RaiseException() already checked that
  // there was a handler for this exception other than cleanup
  // handlers.
  __cxa_begin_catch (ue_header);
  std::terminate ();
}

// _Unwind_Resume_or_Rethrow() is called when rethrowing an
// exception, i.e. for the statement "throw;" (with no arguments).
// The compiler lowers "throw;" to a call to __cxa_rethrow(), which
// calls this function.
extern "C" _Unwind_Reason_Code
__pnacl_eh_sjlj_Unwind_Resume_or_Rethrow (struct _Unwind_Exception *ue_header)
{
  return __pnacl_eh_sjlj_Unwind_RaiseException (ue_header);
}

// A convenience function that calls the exception_cleanup field.
// Based on the definition in libgcc_eh's unwind.inc.
//
// This is called when a catch() block that handles an exception exits
// without rethrowing the exception.  This is called by
// __cxa_end_catch().  The compiler generates a call to
// __cxa_end_catch() at the end of a catch() block.
extern "C" void
__pnacl_eh_sjlj_Unwind_DeleteException (struct _Unwind_Exception *exc)
{
  if (exc->exception_cleanup)
    (*exc->exception_cleanup) (_URC_FOREIGN_EXCEPTION_CAUGHT, exc);
}

// This is based on __cxa_call_unexpected() in eh_personality.cc,
// adapted to use PNaCl's exception info instead of the non-portable
// LSDA.
//
// __cxa_call_unexpected() is called by landingpad blocks when an
// exception has been thrown that doesn't match a function's exception
// specification.  Calls to __cxa_call_unexpected() are generated by
// the compiler.
//
// __cxa_call_unexpected() calls the handler registered via
// std::set_unexpected().  This handler is allowed to throw, in which
// case we must re-check the resulting exception against the original
// exception specification.
//
// The reason that __cxa_call_unexpected() is called by landingpad
// code rather than by the personality function is so that the
// landingpad code can run destructors first.
//
// This function never returns.
extern "C" void
__pnacl_eh_sjlj_cxa_call_unexpected (_Unwind_Exception *ue_header)
{
  __cxa_exception *xh = __get_exception_header_from_ue (ue_header);
  int xh_switch_value = xh->handlerSwitchValue;
  std::terminate_handler xh_terminate_handler = xh->terminateHandler;

  __cxa_begin_catch (ue_header);

  // This function is a handler for our exception argument.  If we exit
  // by throwing a different exception, we'll need the original cleaned up.
  struct end_catch_protect
  {
    end_catch_protect() { }
    ~end_catch_protect() { __cxa_end_catch(); }
  } end_catch_protect_obj;

  // We use the set_unexpected() handler function that was cached by
  // __cxa_throw().
  try
    { __unexpected (xh->unexpectedHandler); }
  catch (...)
    {
      __cxa_eh_globals *globals = __cxa_get_globals_fast ();
      __cxa_exception *new_xh = globals->caughtExceptions;
      void *new_ptr = __get_object_from_ambiguous_exception (new_xh);

      // If this new exception meets the exception spec, allow it.
      if (check_exception_spec (
              __get_exception_header_from_obj (new_ptr)->exceptionType,
              new_ptr, xh_switch_value))
        throw;

      // If the exception spec allows std::bad_exception, throw that.
      // We don't have a thrown object to compare against, but since
      // bad_exception doesn't have virtual bases, that's OK; just pass 0.
      const std::type_info *bad_exc = &typeid (std::bad_exception);
      if (check_exception_spec (bad_exc, NULL, xh_switch_value))
        throw std::bad_exception ();

      __terminate (xh_terminate_handler);
    }
}
