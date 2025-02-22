/*
  Part of: Vicare
  Contents: interface to POSIX functions
  Date: Sun Nov	 6, 2011

  Abstract

	This  file is  without	license notice	in  the original  Ikarus
	distribution  for no  reason I	can know  (Marco Maggi;	 Nov 26,
	2011).

  Copyright (C) 2011, 2012, 2013, 2015 Marco Maggi <marco.maggi-ipsu@poste.it>
  Copyright (C) 2006,2007,2008	Abdulaziz Ghuloum

  This program is  free software: you can redistribute	it and/or modify
  it under the	terms of the GNU General Public	 License as published by
  the Free Software Foundation, either	version 3 of the License, or (at
  your option) any later version.

  This program	is distributed in the  hope that it will  be useful, but
  WITHOUT   ANY	 WARRANTY;   without  even   the  implied   warranty  of
  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See	 the GNU
  General Public License for more details.

  You  should have received  a copy  of the  GNU General  Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


/** --------------------------------------------------------------------
 ** Headers.
 ** ----------------------------------------------------------------- */

#include "internals.h"
#include <dlfcn.h>
#include <complex.h>

#ifndef RTLD_LOCAL
#  define RTLD_LOCAL	0 /* for cygwin, possibly incorrect */
#endif

#define IK_P_OFFSET(OBJ)	(IK_IS_FIXNUM(OBJ)? IK_UNFIX(OBJ) : ik_integer_to_ssize_t(OBJ))
#define IK_P_INDEX(OBJ)		(IK_IS_FIXNUM(OBJ)? IK_UNFIX(OBJ) : ik_integer_to_ssize_t(OBJ))


/** --------------------------------------------------------------------
 ** Interface to "errno".
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_set_errno (ikptr_t code)
{
  if (IK_FALSE_OBJECT == code)
    errno = 0;
  else if (IK_TRUE_OBJECT == code)
    errno = EFAULT;
  else
    errno = -(IK_FIX(code));
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_last_errno(ikpcb_t* pcb)
{
  int	negated_errno_code = - pcb->last_errno;
  return IK_FIX(negated_errno_code);
}


/** --------------------------------------------------------------------
 ** Shared libraries interface.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_dlerror (ikpcb_t* pcb)
{
  char* str = dlerror();
  if (NULL == str)
    return IK_FALSE_OBJECT;
  else {
    int		len  = strlen(str);
    ikptr_t	bv   = ika_bytevector_alloc(pcb, len);
    void *	data = IK_BYTEVECTOR_DATA_VOIDP(bv);
    memcpy(data, str, len);
    return bv;
  }
}
ikptr_t
ikrt_dlopen (ikptr_t library_name_bv, ikptr_t load_lazy, ikptr_t load_global, ikpcb_t* pcb)
{
  int		flags;
  char *	name;
  void *	memory;
  flags	 =
    ((load_lazy	  == IK_FALSE_OBJECT) ? RTLD_NOW	: RTLD_LAZY) |
    ((load_global == IK_FALSE_OBJECT) ? RTLD_LOCAL : RTLD_GLOBAL);
  name	 = (IK_FALSE_OBJECT == library_name_bv)? NULL : IK_BYTEVECTOR_DATA_CHARP(library_name_bv);
  memory = dlopen(name, flags);
  return (NULL == memory)? IK_FALSE_OBJECT : ika_pointer_alloc(pcb, (ikuword_t)memory);
}
ikptr_t
ikrt_dlclose (ikptr_t x /*, ikpcb_t* pcb*/)
{
  int	rv = dlclose(IK_POINTER_DATA_VOIDP(x));
  return (0 == rv) ? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_dlsym (ikptr_t handle, ikptr_t sym, ikpcb_t* pcb)
{
  void *  memory = dlsym(IK_POINTER_DATA_VOIDP(handle), IK_BYTEVECTOR_DATA_CHARP(sym));
  return (NULL == memory)? IK_FALSE_OBJECT : ika_pointer_alloc(pcb, (ikuword_t)memory);
}


/** --------------------------------------------------------------------
 ** Pointer objects.
 ** ----------------------------------------------------------------- */

ikptr_t
ika_pointer_alloc (ikpcb_t * pcb, ikuword_t memory)
{
  ikptr_t	s_pointer = ik_safe_alloc(pcb, pointer_size) | vector_tag;
  IK_REF(s_pointer, off_pointer_tag)  = pointer_tag;
  IK_REF(s_pointer, off_pointer_data) = (ikptr_t)memory;
  return s_pointer;
}
ikptr_t
iku_pointer_alloc (ikpcb_t * pcb, ikuword_t memory)
{
  ikptr_t	s_pointer = ik_unsafe_alloc(pcb, pointer_size) | vector_tag;
  IK_REF(s_pointer, off_pointer_tag)  = pointer_tag;
  IK_REF(s_pointer, off_pointer_data) = (ikptr_t)memory;
  return s_pointer;
}
ikptr_t
ikrt_pointer_clone (ikptr_t s_orig, ikpcb_t * pcb)
{
  return ika_pointer_alloc(pcb, IK_POINTER_DATA(s_orig));
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_pointer_size (void)
{
  return IK_FIX(sizeof(void *));
}
ikptr_t
ikrt_is_pointer (ikptr_t x)
{
  return ((IK_TAGOF(x) == vector_tag) &&
	  (IK_REF(x, -vector_tag) == pointer_tag))? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
int
ik_is_pointer (ikptr_t X)
{
  return ((IK_TAGOF(X) == vector_tag) &&
	  (IK_REF(X, -vector_tag) == pointer_tag));
}
ikptr_t
ikrt_pointer_is_null (ikptr_t x /*, ikpcb_t* pcb*/)
{
  return IK_POINTER_DATA(x)? IK_FALSE_OBJECT : IK_TRUE_OBJECT;
}
ikptr_t
ikrt_pointer_set_null (ikptr_t pointer)
{
  IK_POINTER_SET_NULL(pointer);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_pointer_to_int (ikptr_t pointer, ikpcb_t* pcb)
{
  void *	memory;
  memory = IK_POINTER_DATA_VOIDP(pointer);
#if (VICARE_SIZE_OF_VOIDP == 8)
  return ika_integer_from_ullong(pcb, (ik_ullong)memory);
#else
  return ika_integer_from_uint32(pcb, ((uint32_t)memory));
#endif
}
ikptr_t
ikrt_fx_to_pointer(ikptr_t x, ikpcb_t* pcb)
{
  return ika_pointer_alloc(pcb, IK_UNFIX(x));
}
ikptr_t
ikrt_bn_to_pointer (ikptr_t x, ikpcb_t* pcb)
{
  if (IK_BNFST_NEGATIVE(IK_REF(x, -vector_tag)))
    return ika_pointer_alloc(pcb, -IK_REF(x, off_bignum_data));
  else
    return ika_pointer_alloc(pcb, +IK_REF(x, off_bignum_data));
}

/* ------------------------------------------------------------------ */

/* NOTE The  Scheme function POINTER-DIFF  is implemented at  the Scheme
   level because converting pointers  to Scheme exact integer objects is
   the simplest	 and safest  way to correctly  handle the full	range of
   possible pointer values. */

ikptr_t
ikrt_pointer_add (ikptr_t ptr, ikptr_t delta, ikpcb_t * pcb)
{
  ik_uword_t	memory;
  ik_sword_t	ptrdiff;
  memory  = IK_POINTER_DATA_WORD(ptr);
#ifdef IK_32BIT_PLATFORM
  ptrdiff = ik_integer_to_sint32(delta);
#else
  ptrdiff = ik_integer_to_sint64(delta);
#endif
  if (0 <= ptrdiff) {
    /* fprintf(stderr, "%s: %lu, %ld\n", __func__, (IK_UWORD_MAX - memory), ptrdiff); */
    if ((IK_UWORD_MAX - memory) < ptrdiff) /* => IK_UWORD_MAX < ptrdiff + memory */
      return IK_FALSE_OBJECT;
  } else {
    if (-ptrdiff > memory) /* => 0 > ptrdiff + memory */
      return IK_FALSE_OBJECT;
  }
  return ika_pointer_alloc(pcb, (ikuword_t)(memory + ptrdiff));
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_pointer_eq (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 == memory2)? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_pointer_neq (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 == memory2)? IK_FALSE_OBJECT : IK_TRUE_OBJECT;
}
ikptr_t
ikrt_pointer_lt (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 < memory2)? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_pointer_gt (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 > memory2)? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_pointer_le (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 <= memory2)? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_pointer_ge (ikptr_t ptr1, ikptr_t ptr2)
{
  void *	memory1 = IK_POINTER_DATA_VOIDP(ptr1);
  void *	memory2 = IK_POINTER_DATA_VOIDP(ptr2);
  return (memory1 >= memory2)? IK_TRUE_OBJECT : IK_FALSE_OBJECT;
}


/** --------------------------------------------------------------------
 ** C language level memory allocation.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_malloc (ikptr_t s_number_of_bytes, ikpcb_t* pcb)
{
  void *	p = malloc(ik_integer_to_size_t(s_number_of_bytes));
  return (p)? ika_pointer_alloc(pcb, (ikuword_t) p) : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_realloc (ikptr_t s_memory, ikptr_t s_number_of_bytes, ikpcb_t* pcb)
{
  int		isptr		= IK_IS_POINTER(s_memory);
  ikptr_t	s_pointer	= (isptr)? s_memory : IK_MBLOCK_POINTER(s_memory);
  void *	memory		= IK_POINTER_DATA_VOIDP(s_pointer);
  void *	new_memory;
  if (memory) {
    new_memory = realloc(memory, ik_integer_to_size_t(s_number_of_bytes));
    /* fprintf(stderr, "%s: %p, %p\n", __func__, memory, new_memory); */
    if (new_memory) {
      IK_REF(s_pointer, off_pointer_data) = (ikptr_t)new_memory;
      if (!isptr)
	IK_MBLOCK_SIZE(s_memory) = s_number_of_bytes;
      return s_memory;
    } else
      return IK_FALSE_OBJECT;
  } else
    return IK_FALSE_OBJECT;
}
ikptr_t
ikrt_calloc (ikptr_t s_number_of_elements, ikptr_t s_element_size, ikpcb_t* pcb)
{
  void * memory = calloc(ik_integer_to_size_t(s_number_of_elements),
			 ik_integer_to_size_t(s_element_size));
  return (memory)? ika_pointer_alloc(pcb, (ikuword_t)memory) : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_free (ikptr_t s_memory)
{
  int		isptr		= IK_IS_POINTER(s_memory);
  ikptr_t		s_pointer	= (isptr)? s_memory : IK_MBLOCK_POINTER(s_memory);
  void *	memory		= IK_POINTER_DATA_VOIDP(s_pointer);
  if (memory) {
    free(memory);
    IK_POINTER_SET_NULL(s_pointer);
    if (!isptr)
      IK_MBLOCK_SIZE(s_memory) = IK_FIX(0);
  }
  return IK_VOID_OBJECT;
}


/** --------------------------------------------------------------------
 ** C language level memory operations.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_memcpy (ikptr_t dst, ikptr_t src, ikptr_t size)
{
  memcpy(IK_POINTER_DATA_VOIDP(dst),
	 IK_POINTER_DATA_VOIDP(src),
	 ik_integer_to_size_t(size));
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_memmove (ikptr_t dst, ikptr_t src, ikptr_t size)
{
  memmove(IK_POINTER_DATA_VOIDP(dst),
	  IK_POINTER_DATA_VOIDP(src),
	  ik_integer_to_size_t(size));
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_memset (ikptr_t ptr, ikptr_t byte, ikptr_t size)
{
  memset(IK_POINTER_DATA_VOIDP(ptr), IK_UNFIX(byte), IK_UNFIX(size));
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_memcmp (ikptr_t pointer1, ikptr_t pointer2, ikptr_t count)
{
  int	rv;
  rv = memcmp(IK_POINTER_DATA_VOIDP(pointer1),
	      IK_POINTER_DATA_VOIDP(pointer2),
	      ik_integer_to_size_t(count));
  return IK_FIX(rv);
}


/** --------------------------------------------------------------------
 ** Raw memory and Scheme bytevector operations.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_memcpy_to_bv (ikptr_t s_dst, ikptr_t s_dst_off, ikptr_t s_src, ikptr_t s_count)
{
  void * src = IK_POINTER_DATA_VOIDP(s_src);
  void * dst = (void *)(IK_BYTEVECTOR_DATA(s_dst) + IK_UNFIX(s_dst_off));
  memcpy(dst, src, IK_UNFIX(s_count));
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_memcpy_from_bv (ikptr_t s_dst, ikptr_t s_src, ikptr_t s_src_off, ikptr_t s_count)
{
  void * src = (void *)(IK_BYTEVECTOR_DATA(s_src) + IK_UNFIX(s_src_off));
  void * dst = IK_POINTER_DATA_VOIDP(s_dst);
  memcpy(dst, src, IK_UNFIX(s_count));
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_bytevector_from_memory (ikptr_t s_pointer, ikptr_t s_length, ikpcb_t * pcb)
{
  void *	memory = IK_POINTER_DATA_VOIDP(s_pointer);
  size_t	size   = (size_t)IK_UNFIX(s_length);
  return ika_bytevector_from_memory_block(pcb, memory, size);
}
ikptr_t
ikrt_bytevector_to_memory (ikptr_t s_bv, ikpcb_t * pcb)
{
  size_t	length = (size_t)IK_BYTEVECTOR_LENGTH(s_bv);
  void *	memory = malloc(length);
  if (memory) {
    void *	data;
    data = IK_BYTEVECTOR_DATA_VOIDP(s_bv);
    memcpy(memory, data, length);
    return ika_pointer_alloc(pcb, (ikuword_t)memory);
  } else
    return IK_FALSE_OBJECT;
}


/** --------------------------------------------------------------------
 ** C strings.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_bytevector_to_cstring (ikptr_t bv, ikpcb_t * pcb)
{
  char *	pointer = IK_BYTEVECTOR_DATA_CHARP(bv);
  size_t	length	= (size_t)IK_BYTEVECTOR_LENGTH(bv);
  char *	cstr;
  cstr = malloc(1+length);
  if (cstr) {
    /* Notice that:

          strncpy(cstr, pointer, length);

       does the wrong thing here when there are zero bytes in the middle
       of the  bytevector: it copies the  bytes up until the  first zero
       byte, then  pads the  destination with  zeros.  We  want verbatim
       copying  here,  in  case  the   input  bytevector  is  in  UTF-16
       encoding. */
    /* int		i; */
    /* for (i=0; i<length; ++i) */
    /*   cstr[i] = pointer[i]; */
    memcpy(cstr, pointer, length);
    cstr[length] = '\0';
    return ika_pointer_alloc(pcb, (ikuword_t)cstr);
  } else
    return IK_FALSE_OBJECT;
}
ikptr_t
ikrt_bytevector_from_cstring (ikptr_t s_pointer, ikptr_t s_count, ikpcb_t * pcb)
{
  char *	memory	= IK_POINTER_DATA_VOIDP(s_pointer);
  iksword_t	count	= IK_UNFIX(s_count);
  ikptr_t	s_bv	= ika_bytevector_alloc(pcb, count);
  char *	data	= IK_BYTEVECTOR_DATA_CHARP(s_bv);
  memcpy(data, memory, count);
  return s_bv;
}
ikptr_t
ikrt_bytevector_from_cstring16 (ikptr_t s_pointer, ikpcb_t * pcb)
{
  return ika_bytevector_from_utf16z(pcb, IK_POINTER_DATA_VOIDP(s_pointer));
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_strlen (ikptr_t s_pointer, ikpcb_t * pcb)
{
  return ika_integer_from_long(pcb, strlen(IK_POINTER_DATA_VOIDP(s_pointer)));
}
ikptr_t
ikrt_strcmp (ikptr_t s_pointer1, ikptr_t s_pointer2)
{
  char *	ptr1 = IK_POINTER_DATA_VOIDP(s_pointer1);
  char *	ptr2 = IK_POINTER_DATA_VOIDP(s_pointer2);
  return IK_FIX(strcmp(ptr1, ptr2));
}
ikptr_t
ikrt_strncmp (ikptr_t s_pointer1, ikptr_t s_pointer2, ikptr_t s_count)
{
  char *	ptr1 = IK_POINTER_DATA_VOIDP(s_pointer1);
  char *	ptr2 = IK_POINTER_DATA_VOIDP(s_pointer2);
  return IK_FIX(strncmp(ptr1, ptr2, ik_integer_to_size_t(s_count)));
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_strdup (ikptr_t s_pointer, ikpcb_t * pcb)
{
  char *	src = IK_POINTER_DATA_VOIDP(s_pointer);
  char *	dst = strdup(src);
  return (dst)? ika_pointer_alloc(pcb, (ikuword_t)dst) : IK_FALSE_OBJECT;
}
ikptr_t
ikrt_strndup (ikptr_t s_pointer, ikptr_t s_count, ikpcb_t * pcb)
{
  char *	src = IK_POINTER_DATA_VOIDP(s_pointer);
  char *	dst = strndup(src, ik_integer_to_size_t(s_count));
  return (dst)? ika_pointer_alloc(pcb, (ikuword_t)dst) : IK_FALSE_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_argv_from_bytevectors (ikptr_t s_bvs, ikpcb_t * pcb)
/* Convert a list of bytevectors  into a NULL-terminated array of ASCIIZ
   strings.   Return a pointer referencing the malloc-ed array. */
{
  int		argc = ik_list_length(s_bvs);
  size_t	total_length;
  char *	bv_data[1+argc];
  long		bv_len[argc];
  char **	argv;
  int		len;
  char *	str;
  int		i;
  ik_list_to_argv_and_argc(s_bvs, bv_data, bv_len);
  for (i=0, total_length=0; i<argc; total_length += sizeof(char*)+1+bv_len[i++]);
  total_length += sizeof(char*);
  argv = malloc(total_length);
  if (argv) {
    str = (char*)(((uint8_t*)argv) + sizeof(char*) * (1+argc));
    for (i=0; i<argc; ++i) {
      argv[i] = str;
      len     = bv_len[i];
      strncpy(str, bv_data[i], len);
      str[len] = '\0';
      str += 1+len;
    }
    argv[argc] = NULL;
    return ika_pointer_alloc(pcb, (ikuword_t)argv);
  } else
    return IK_FALSE_OBJECT;
}
ikptr_t
ikrt_argv_to_bytevectors (ikptr_t s_pointer, ikpcb_t * pcb)
{
  return ika_list_from_argv(pcb, IK_POINTER_DATA_VOIDP(s_pointer));
}
ikptr_t
ikrt_argv_length (ikptr_t s_pointer)
{
  char **	argv = IK_POINTER_DATA_VOIDP(s_pointer);
  long		length;
  for (length=0; argv[length]; ++length);
  return IK_FIX(length);
}


/** --------------------------------------------------------------------
 ** Local storage.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_with_local_storage (ikptr_t s_lengths, ikptr_t s_thunk, ikpcb_t * pcb)
{
  ikptr_t		code_entry	= IK_REF(s_thunk, off_closure_code);
  /* S_CODE  is a  tagged pointer  to the  code object  implementing the
     closure S_THUNK. */
  ikptr_t		s_code		= code_entry - off_code_data;
  int		arity		= IK_VECTOR_LENGTH(s_lengths);
  long		total_length	= 0;
  long		lengths[arity];
  int		i;
  ikptr_t		s_result;
  for (i=0; i<arity; ++i)
    total_length += lengths[i] = IK_UNFIX(IK_ITEM(s_lengths, i));
  ik_enter_c_function(pcb);
  {
    /* When  arriving  here  the  Scheme   stack  has  been  frozen  by
     * "ik_enter_c_function()", so the situation is as follows:
     *
     *          high memory
     *   |                      |
     *   |----------------------|
     *   |                      | <- pcb->frame_base
     *   |----------------------|
     *   | ik_underflow_handler | <- pcb->frame_pointer
     *   |----------------------|
     *   |                      |
     *           low memory
     */
    assert(IK_UNDERFLOW_HANDLER == *((ikptr_t*)pcb->frame_pointer));
    /* As example:  let's assume  there are 2  pointer arguments  to the
     * code object, we want to prepare the Scheme stack as follows:
     *
     *          high memory
     *   |                      |
     *   |----------------------|
     *   |                      | <- pcb->frame_base = pcb->frame_pointer
     *   |----------------------|
     *   | ik_underflow_handler |
     *   |----------------------|
     *   |  pointer argument 1  |
     *   |----------------------|
     *   |  pointer argument 2  |
     *   |----------------------|
     *   |                      |
     *           low memory
     *
     * setting "frame_pointer"  equal to  "frame_base" is needed  by the
     * assembly subroutine calling the code in S_THUNK.
     */
    uint8_t	buffer[total_length];
    long	offset;
    pcb->frame_pointer = pcb->frame_base;
    /* Push the arguments on the Scheme stack. */
    for (i=0, offset=0; i<arity; offset+=lengths[i], ++i) {
      IK_REF(pcb->frame_pointer, -2*wordsize-i*wordsize) =
	/* Allocation without garbage collection!!! */
	iku_pointer_alloc(pcb, (ikuword_t)&(buffer[offset]));
    }
    s_result = ik_exec_code(pcb, s_code, IK_FIX(-arity), s_thunk);
  }
  ik_leave_c_function(pcb);
  return s_result;
}


/** --------------------------------------------------------------------
 ** Raw memory getters through pointers.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_ref_uint8 (ikptr_t s_pointer, ikptr_t s_offset)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint8_t *	data   = (uint8_t *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_sint8 (ikptr_t s_pointer, ikptr_t s_offset)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int8_t *	data   = (int8_t *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_uint16 (ikptr_t s_pointer, ikptr_t s_offset)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint16_t *	data   = (uint16_t *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_sint16 (ikptr_t s_pointer, ikptr_t s_offset)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int16_t *	data   = (int16_t *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_uint32 (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t * pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint32_t *	data   = (uint32_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_uint32(pcb, *data);
}
ikptr_t
ikrt_ref_sint32 (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t * pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int32_t *	data   = (int32_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_sint32(pcb, *data);
}
ikptr_t
ikrt_ref_uint64 (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t * pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint64_t *	data   = (uint64_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_uint64(pcb, *data);
}
ikptr_t
ikrt_ref_sint64 (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t * pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int64_t *	data   = (int64_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_sint64(pcb, *data);
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_ref_float (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  float *	data   = (float *)(memory + IK_P_OFFSET(s_offset));
  return ika_flonum_from_double(pcb, (double)(*data));
}
ikptr_t
ikrt_ref_double (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  double *	data   = (double *)(memory + IK_P_OFFSET(s_offset));
  return ika_flonum_from_double(pcb, *data);
}
ikptr_t
ikrt_ref_double_complex (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  double *	data   = (double *)(memory + IK_P_OFFSET(s_offset));
  return ika_cflonum_from_doubles(pcb, *data, *(data + 1));
}
ikptr_t
ikrt_ref_pointer (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  void **	data   = (void **)(memory + IK_P_OFFSET(s_offset));
  return ika_pointer_alloc(pcb, (ikuword_t)(*data));
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_ref_char(ikptr_t s_pointer, ikptr_t s_offset /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  signed char *	data   = (signed char *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_uchar(ikptr_t s_pointer, ikptr_t s_offset /*, ikpcb_t* pcb*/)
{
  uint8_t *		memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned char *	data   = (unsigned char *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_short(ikptr_t s_pointer, ikptr_t s_offset /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  short int *	data   = (short int *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_ushort(ikptr_t s_pointer, ikptr_t s_offset /*, ikpcb_t* pcb*/)
{
  uint8_t *		memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned short int *	data   = (unsigned short int *)(memory + IK_P_OFFSET(s_offset));
  return IK_FIX(*data);
}
ikptr_t
ikrt_ref_int (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int *		data   = (int *)(memory + IK_P_OFFSET(s_offset));
  return (wordsize == 8)? IK_FIX(*data) : ika_integer_from_int(pcb, *data);
}
ikptr_t
ikrt_ref_uint(ikptr_t s_pointer, ikptr_t s_offset , ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned int *data   = (unsigned int *)(memory + IK_P_OFFSET(s_offset));
  return (wordsize == 8)? IK_FIX(*data) : ika_integer_from_uint(pcb, *data);
}
ikptr_t
ikrt_ref_long(ikptr_t s_pointer, ikptr_t s_offset , ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  long *	data   = (long *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_long(pcb, *data);
}
ikptr_t
ikrt_ref_ulong(ikptr_t s_pointer, ikptr_t s_offset , ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ulong *	data   = (ik_ulong *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_ulong(pcb, *data);
}
ikptr_t
ikrt_ref_longlong(ikptr_t s_pointer, ikptr_t s_offset , ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_llong *	data   = (ik_llong *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_llong(pcb, *data);
}
ikptr_t
ikrt_ref_ulonglong(ikptr_t s_pointer, ikptr_t s_offset , ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ullong *	data   = (ik_ullong *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_ullong(pcb, *data);
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_ref_size_t (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  size_t *	data   = (size_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_size_t(pcb, *data);
}
ikptr_t
ikrt_ref_ssize_t (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ssize_t *	data   = (ssize_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_ssize_t(pcb, *data);
}
ikptr_t
ikrt_ref_off_t (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  off_t *	data   = (off_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_off_t(pcb, *data);
}
ikptr_t
ikrt_ref_ptrdiff_t (ikptr_t s_pointer, ikptr_t s_offset, ikpcb_t* pcb)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ptrdiff_t *	data   = (ptrdiff_t *)(memory + IK_P_OFFSET(s_offset));
  return ika_integer_from_ptrdiff_t(pcb, *data);
}


/** --------------------------------------------------------------------
 ** Raw memory setters through pointers.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_set_uint8 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint8_t *	data	= (uint8_t *)(memory + IK_P_OFFSET(s_offset));
  *data = IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_sint8 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int8_t *	data   = (int8_t *)(memory + IK_P_OFFSET(s_offset));
  *data = IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_uint16 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint16_t *	data   = (uint16_t *)(memory + IK_P_OFFSET(s_offset));
  *data = (uint16_t)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_sint16 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int16_t *	data   = (int16_t *)(memory + IK_P_OFFSET(s_offset));
  *data = (int16_t)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_uint32 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint32_t *	data   = (uint32_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_uint32(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_sint32 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int32_t *	data   = (int32_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_sint32(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_uint64 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint64_t *	data   = (uint64_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_uint64(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_sint64 (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value)
{
  uint8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int64_t *	data   = (int64_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_sint64(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_set_float (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  float *	data	= (float *)(memory + IK_P_OFFSET(s_offset));
  *data = (float)IK_FLONUM_DATA(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_double (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  double *	data	= (double *)(memory + IK_P_OFFSET(s_offset));
  *data = IK_FLONUM_DATA(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_double_complex (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  double *	data	= (double *)(memory + IK_P_OFFSET(s_offset));
  ikptr_t		s_real	= IK_CFLONUM_REAL(s_value);
  ikptr_t		s_imag	= IK_CFLONUM_IMAG(s_value);
  *data     = IK_FLONUM_DATA(s_real);
  *(data+1) = IK_FLONUM_DATA(s_imag);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_pointer (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  void **	data	= (void **)(memory + IK_P_OFFSET(s_offset));
  *data = IK_POINTER_DATA_VOIDP(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_set_char (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  char *	data	= (char *)(memory + IK_P_OFFSET(s_offset));
  *data = (char)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_uchar (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *		memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned char *	data	= (unsigned char *)(memory + IK_P_OFFSET(s_offset));
  *data = (unsigned char)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_set_short (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  short int *	data	= (short int *)(memory + IK_P_OFFSET(s_offset));
  *data = (short int)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_ushort (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *		memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned short int *	data	= (unsigned short int *)(memory + IK_P_OFFSET(s_offset));
  *data = (unsigned short int)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_set_int (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int *		data	= (int *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_int(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_uint (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned int *data	= (unsigned int *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_uint(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_set_long (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  long *	data	= (long *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_long(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_ulong (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ulong *	data	= (ik_ulong *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_ulong(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_set_longlong (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_llong *	data	= (ik_llong *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_llong(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_ulonglong (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ullong *	data	= (ik_ullong *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_ullong(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_set_size_t (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  size_t *	data	= (size_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_size_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_ssize_t (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ssize_t *	data	= (ssize_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_ssize_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_off_t (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  off_t *	data	= (off_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_off_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_ptrdiff_t (ikptr_t s_pointer, ikptr_t s_offset, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ptrdiff_t *	data	= (ptrdiff_t *)(memory + IK_P_OFFSET(s_offset));
  *data = ik_integer_to_ptrdiff_t(s_value);
  return IK_VOID_OBJECT;
}


/** --------------------------------------------------------------------
 ** Raw memory array getters through pointers.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_array_ref_uint8 (ikptr_t s_pointer, ikptr_t s_index)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint8_t	data	= memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_sint8 (ikptr_t s_pointer, ikptr_t s_index)
{
  int8_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int8_t	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_uint16 (ikptr_t s_pointer, ikptr_t s_index)
{
  uint16_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint16_t	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_sint16 (ikptr_t s_pointer, ikptr_t s_index)
{
  int16_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int16_t	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_uint32 (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t * pcb)
{
  uint32_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint32_t	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_uint32(pcb, data);
}
ikptr_t
ikrt_array_ref_sint32 (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t * pcb)
{
  int32_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int32_t	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_sint32(pcb, data);
}
ikptr_t
ikrt_array_ref_uint64 (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t * pcb)
{
  uint64_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  uint64_t	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_uint64(pcb, data);
}
ikptr_t
ikrt_array_ref_sint64 (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t * pcb)
{
  int64_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int64_t	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_sint64(pcb, data);
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_ref_float (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  float *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  float		data   = memory[IK_P_INDEX(s_index)];
  return ika_flonum_from_double(pcb, (double)data);
}
ikptr_t
ikrt_array_ref_double (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  double *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  double	data   = memory[IK_P_INDEX(s_index)];
  return ika_flonum_from_double(pcb, data);
}
ikptr_t
ikrt_array_ref_double_complex (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  double complex *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ssize_t		idx    = IK_P_INDEX(s_index);
  double complex	Z      = memory[idx];
  return ika_cflonum_from_doubles(pcb, creal(Z), cimag(Z));
}
ikptr_t
ikrt_array_ref_pointer (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  void **	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  void *	data   = memory[IK_P_INDEX(s_index)];
  return ika_pointer_alloc(pcb, (ikuword_t)data);
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_ref_char(ikptr_t s_pointer, ikptr_t s_index /*, ikpcb_t* pcb*/)
{
  signed char *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  signed char	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_uchar(ikptr_t s_pointer, ikptr_t s_index /*, ikpcb_t* pcb*/)
{
  unsigned char *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned char		data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_short(ikptr_t s_pointer, ikptr_t s_index /*, ikpcb_t* pcb*/)
{
  short int *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  short int	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_ushort(ikptr_t s_pointer, ikptr_t s_index /*, ikpcb_t* pcb*/)
{
  unsigned short int *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned short int	data   = memory[IK_P_INDEX(s_index)];
  return IK_FIX(data);
}
ikptr_t
ikrt_array_ref_int (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  int *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  int 	data   = memory[IK_P_INDEX(s_index)];
  return (wordsize == 8)? IK_FIX(data) : ika_integer_from_int(pcb, data);
}
ikptr_t
ikrt_array_ref_uint(ikptr_t s_pointer, ikptr_t s_index , ikpcb_t* pcb)
{
  unsigned int *memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  unsigned int	data   = memory[IK_P_INDEX(s_index)];
  return (wordsize == 8)? IK_FIX(data) : ika_integer_from_uint(pcb, data);
}
ikptr_t
ikrt_array_ref_long(ikptr_t s_pointer, ikptr_t s_index , ikpcb_t* pcb)
{
  long *memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  long	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_long(pcb, data);
}
ikptr_t
ikrt_array_ref_ulong(ikptr_t s_pointer, ikptr_t s_index , ikpcb_t* pcb)
{
  ik_ulong *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ulong	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_ulong(pcb, data);
}
ikptr_t
ikrt_array_ref_longlong(ikptr_t s_pointer, ikptr_t s_index , ikpcb_t* pcb)
{
  ik_llong *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_llong	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_llong(pcb, data);
}
ikptr_t
ikrt_array_ref_ulonglong(ikptr_t s_pointer, ikptr_t s_index , ikpcb_t* pcb)
{
  ik_ullong *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ik_ullong	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_ullong(pcb, data);
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_ref_size_t (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  size_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  size_t 	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_size_t(pcb, data);
}
ikptr_t
ikrt_array_ref_ssize_t (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  ssize_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ssize_t 	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_ssize_t(pcb, data);
}
ikptr_t
ikrt_array_ref_off_t (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  off_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  off_t 	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_off_t(pcb, data);
}
ikptr_t
ikrt_array_ref_ptrdiff_t (ikptr_t s_pointer, ikptr_t s_index, ikpcb_t* pcb)
{
  ptrdiff_t *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ptrdiff_t 	data   = memory[IK_P_INDEX(s_index)];
  return ika_integer_from_ptrdiff_t(pcb, data);
}


/** --------------------------------------------------------------------
 ** Raw memory array setters through pointers.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_array_set_uint8 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  uint8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_sint8 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  int8_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_uint16 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  uint16_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (uint16_t)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_sint16 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  int16_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (int16_t)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_uint32 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  uint32_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_uint32(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_sint32 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  int32_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_sint32(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_uint64 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  uint64_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_uint64(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_sint64 (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value)
{
  uint64_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_sint64(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_set_float (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  float *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (float)IK_FLONUM_DATA(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_double (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  double *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = IK_FLONUM_DATA(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_double_complex (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  double complex *	memory = IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  ssize_t	idx	= IK_P_OFFSET(s_index);
  double	re	= IK_FLONUM_DATA(IK_CFLONUM_REAL(s_value));
  double	im	= IK_FLONUM_DATA(IK_CFLONUM_IMAG(s_value));
  memory[idx]   = re + im * _Complex_I;
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_pointer (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  void **	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = IK_POINTER_DATA_VOIDP(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_set_char (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  char *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (char)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_uchar (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  unsigned char *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (unsigned char)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_array_set_short (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  short int *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (short int)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_ushort (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  unsigned short int *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = (unsigned short int)IK_UNFIX(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_array_set_int (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  int *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_int(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_uint (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  unsigned int *memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_uint(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_array_set_long (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  long *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_long(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_ulong (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  ik_ulong *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_ulong(s_value);
  return IK_VOID_OBJECT;
}

ikptr_t
ikrt_array_set_longlong (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  ik_llong *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_llong(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_ulonglong (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  ik_ullong *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_ullong(s_value);
  return IK_VOID_OBJECT;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_array_set_size_t (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  size_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_size_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_ssize_t (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  ssize_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_ssize_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_off_t (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  off_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_off_t(s_value);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_array_set_ptrdiff_t (ikptr_t s_pointer, ikptr_t s_index, ikptr_t s_value /*, ikpcb_t* pcb*/)
{
  ptrdiff_t *	memory	= IK_POINTER_FROM_POINTER_OR_MBLOCK(s_pointer);
  memory[IK_P_INDEX(s_index)] = ik_integer_to_ptrdiff_t(s_value);
  return IK_VOID_OBJECT;
}

/* end of file */
