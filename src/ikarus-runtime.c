/*
 * Ikarus Scheme -- A compiler for R6RS Scheme.
 * Copyright (C) 2006,2007,2008  Abdulaziz Ghuloum
 * Modified by Marco Maggi <marco.maggi-ipsu@poste.it>
 *
 * This program is free software:  you can redistribute it and/or modify
 * it under  the terms of  the GNU General  Public License version  3 as
 * published by the Free Software Foundation.
 *
 * This program is  distributed in the hope that it  will be useful, but
 * WITHOUT  ANY   WARRANTY;  without   even  the  implied   warranty  of
 * MERCHANTABILITY  or FITNESS FOR  A PARTICULAR  PURPOSE.  See  the GNU
 * General Public License for more details.
 *
 * You should  have received  a copy of  the GNU General  Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/** --------------------------------------------------------------------
 ** Headers.
 ** ----------------------------------------------------------------- */

#include "internals.h"
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>


/** --------------------------------------------------------------------
 ** Prototypes and internal definitions.
 ** ----------------------------------------------------------------- */

/* Total number of Vicare pages currently allocated with "ik_mmap()". */
static int total_allocated_pages = 0;

/* Total number of bytes currently allocated with "ik_malloc()". */
static int total_malloced = 0;

int		ik_garbage_collection_is_forbidden	= 0;

ikuword_t	ik_customisable_heap_nursery_size	= IK_HEAPSIZE;
ikuword_t	ik_customisable_stack_size		= IK_STACKSIZE;

/* When true: internals inspection messages  are enabled.  It is used by
   the preprocessor macro "IK_RUNTIME_MESSAGE()". */
int		ik_enabled_runtime_messages		= 0;


/** --------------------------------------------------------------------
 ** C language like memory allocation.
 ** ----------------------------------------------------------------- */

void *
ik_malloc (int size)
{
  void* x = malloc(size);
  if (NULL == x)
    ik_abort("malloc failed: %s", strerror(errno));
  total_malloced += size;
  return x;
}
void
ik_free (void* x, int size)
{
  total_malloced -= size;
  free(x);
}


/** --------------------------------------------------------------------
 ** Scheme language memory allocation, basic memory mapping.
 ** ----------------------------------------------------------------- */

ikptr_t
ik_mmap (ikuword_t size)
/* Allocate new  memory pages.   All memory  allocation is  performed by
   this function.  The allocated memory  is initialised to a sequence of
   IK_FORWARD_PTR words.

   If  the allocated  memory  is  used for  the  Scheme  stack or  the
   generational pages:  we must initialise  every word to a  safe value.
   If the  allocated memory is  used for the  Scheme heap: we  can leave
   some words uninitialised, because the heap is not a garbage collector
   root. */
{
  char *	mem;
  ikuword_t	npages   = IK_MINIMUM_PAGES_NUMBER_FOR_SIZE(size);
  ikuword_t	mapsize  = npages * IK_PAGESIZE;
  total_allocated_pages += npages;
  if (0) {
    ik_debug_message("%s: size=%lu, pages=%lu, mapsize=%lu, size/PGSIZE=%lu, mapsize/PGSIZE=%lu\n",
		     __func__, size, npages, mapsize, size/IK_PAGESIZE, mapsize/IK_PAGESIZE);
  }
  assert(size == mapsize);
#if ((defined __CYGWIN__) || (defined __FAKE_CYGWIN__))
  mem = win_mmap(mapsize);
#else
  mem = mmap(0, mapsize, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
  /* FIXME Check if in range.  (Abdulaziz Ghuloum) */
  if (mem == MAP_FAILED)
    ik_abort("mapping (0x%lx bytes) failed: %s", size, strerror(errno));
#endif
  /* Notice that when the garbage  collector scans, word by word, memory
     that should contain the data area of a Scheme object: it interprets
     every machine word with all the bits set to 1 as IK_FORWARD_PTR.

     Here we initialise  every allocated memory page to  a sequence of
     IK_FORWARD_PTR words, which, most likely, will trigger an assertion
     violation if the garbage collector scans a machine word we have not
     explicitly initialised  to something valid.  Whenever  we reserve a
     portion of memory  page, with aligned size, for a  Scheme object we
     must initialise all its words to something valid.

     When  we  convert  a  requested  size to  an  aligned  size  with
     "IK_ALIGN()": either zero  or one machine word  is allocated beyond
     the  requested   size.   When  such  additional   machine  word  is
     allocated: we  have to initialise  it to something  valid.  Usually
     the safe value  to which we should initialise memory  is the fixnum
     zero: a machine word with all the bits set to 0. */
  memset(mem, -1, mapsize);
#ifdef VICARE_DEBUGGING
  ik_debug_message("%s: 0x%016lx .. 0x%016lx\n", __func__, (long)mem, ((long)(mem))+mapsize-1);
#endif
  return (ikptr_t)mem;
}
void
ik_munmap (ikptr_t mem, ikuword_t size)
/* All memory relese is performed by this function. */
{
  ikuword_t	npages  = IK_MINIMUM_PAGES_NUMBER_FOR_SIZE(size);
  ikuword_t	mapsize = npages * IK_PAGESIZE;
  assert(size == mapsize);
  /* Assert that the  12 least significant bits in MEM  are set to zero:
     this means MEM is a pointer  to the beginning of an absolute memory
     page. */
  assert(((-IK_PAGESIZE) & (int)mem) == (int)mem);
  total_allocated_pages -= npages;
#if ((defined __CYGWIN__) || (defined __FAKE_CYGWIN__))
  win_munmap((char*)mem, mapsize);
#else
  {
    int	err = munmap((char*)mem, mapsize);
    if (err)
      ik_abort("ik_munmap failed: %s", strerror(errno));
  }
#endif
#ifdef VICARE_DEBUGGING
  ik_debug_message("%s: 0x%016lx .. 0x%016lx\n", __func__, (long)mem, ((long)(mem))+mapsize-1);
#endif
}


/** --------------------------------------------------------------------
 ** Memory mapping and tagging for garbage collection.
 ** ----------------------------------------------------------------- */

static void set_page_range_type       (ikptr_t base, ikuword_t size, uint32_t type, ikpcb_t* pcb);
static void extend_page_vectors_maybe (ikptr_t base, ikuword_t size, ikpcb_t* pcb);

ikptr_t
ik_mmap_typed (ikuword_t size, uint32_t type, ikpcb_t* pcb)
/* Allocate new  memory pages  or recycle  an old  memory page  from the
   PCB's cache and return a pointer to it.

   The  allocated  memory  is  NOT initialised  to  safe  values:  its
   contents have to be considered invalid and initialised to safe values
   before  being scanned  by the  garbage collector.   If the  allocated
   memory is  used for the  Scheme stack  or the generational  pages: we
   must initialise every word to a  safe value.  If the allocated memory
   is used for  the Scheme heap: we can leave  some words uninitialised,
   because the heap is not a garbage collector root. */
{
  ikptr_t		base;
  if (size == IK_PAGESIZE) {
    /* If available, recycle  a page from the cache.   Remember that the
       memory in the cached pages is  NOT reset in any way: its contents
       is what it is. */
    ikpage_t *	pages = pcb->cached_pages;
    if (pages) {
      /* Extract the first page from the linked list of cached pages. */
      base		  = pages->base;
      pcb->cached_pages	  = pages->next;
      /* Prepend  the extracted  page  to the  linked  list of  uncached
	 pages. */
      pages->next	  = pcb->uncached_pages;
      pcb->uncached_pages = pages;
    } else {
      /* No cached page available: allocate a new page. */
      base = ik_mmap(size);
    }
  } else {
    base = ik_mmap(size);
  }
  extend_page_vectors_maybe(base, size, pcb);
  set_page_range_type(base, size, type, pcb);
  return base;
}
ikptr_t
ik_mmap_ptr (ikuword_t size, int gen, ikpcb_t* pcb)
{
  return ik_mmap_typed(size, POINTERS_MT|gen, pcb);
}
ikptr_t
ik_mmap_data (ikuword_t size, int gen, ikpcb_t* pcb)
{
  return ik_mmap_typed(size, DATA_MT|gen, pcb);
}
ikptr_t
ik_mmap_code (ikuword_t aligned_size, int gen, ikpcb_t* pcb)
/* Allocate contiguous  memory mapped pages  into a single  memory block
   which will hold one or more code objects, return a raw pointer to the
   allocated block.  The first page is  marked in the segments vector as
   "for code"; subsequent pages, if any, are marked as "for data".

   Question: Why when allocating a code  object the first page is tagged
   as code  and the subsequent pages  as data?  Answer: Because  we know
   that the  all the  slots in  a code  object containing  references to
   other  Scheme  objects  are  in  the  first  page,  so  when  garbage
   collecting we need to scan only the first page.

   This  function is  used, for  example,  to allocate  memory for  code
   objects loaded from  the boot image.  If the code  object is "small",
   it fits into a single page; in this case multiple code objects can be
   stored in  the same  page (with  each code  object size  aligned with
   IK_ALIGN). */
{
  ikptr_t p = ik_mmap_typed(aligned_size, CODE_MT|gen, pcb);
  if (aligned_size > IK_PAGESIZE)
    set_page_range_type(p+IK_PAGESIZE, aligned_size-IK_PAGESIZE, DATA_MT|gen, pcb);
  return p;
}
ikptr_t
ik_mmap_mainheap (ikuword_t size, ikpcb_t* pcb)
/* Allocate a memory segment tagged as part of the Scheme heap. */
{
  return ik_mmap_typed(size, MAINHEAP_MT, pcb);
}
static void
set_page_range_type (ikptr_t base, ikuword_t size, uint32_t type, ikpcb_t* pcb)
/* Set to TYPE all the entries in "pcb->segment_vector" corresponding to
   the memory block starting at BASE and SIZE bytes wide. */
{
  /* The PCB  fields "memory_base"  and "memory_end" delimit  the memory
     used by Scheme code; obviously an allocated segment must be in this
     range. */
  assert(base        >= pcb->memory_base);
  assert((base+size) <= pcb->memory_end);
  assert(size == IK_ALIGN_TO_NEXT_PAGE(size));
  uint32_t * p = pcb->segment_vector + IK_PAGE_INDEX(base);
  uint32_t * q = p                   + IK_PAGE_INDEX_RANGE(size);
  for (; p < q; ++p)
    *p = type;
}
static void
extend_page_vectors_maybe (ikptr_t base_ptr, ikuword_t size, ikpcb_t* pcb)
/* For garbage  collection purposes we  keep track of every  Vicare page
 * used by Scheme code in PCB's  dirty vector and segments vector.  When
 * a new memory block is allocated we must check if such vectors need to
 * be updated to reference it.
 *
 *   All the memory  used by Scheme code  must be in the  range from the
 * PCB members"memory_base" and "memory_end":
 *
 Scheme used memory
 *         |.......................|
 *    |--------------------------------------------| system memory
 *         ^                        ^
 *      memory_base             memory_end
 *
 * the dirty vector and segments vector  contain a slot for each page in
 * such range.
 *
 * This function only  updates the tracked range of memory,  it does NOT
 * tag the new memory in any way.
 */
{
  assert(size == IK_ALIGN_TO_NEXT_PAGE(size));
  ikptr_t end_ptr = base_ptr + size;
  if (base_ptr < pcb->memory_base) {
    ikuword_t new_lo_seg   = IK_SEGMENT_INDEX(base_ptr);
    ikuword_t old_lo_seg   = IK_SEGMENT_INDEX(pcb->memory_base);
    ikuword_t hi_seg       = IK_SEGMENT_INDEX(pcb->memory_end); /* unchanged */
    ikuword_t new_vec_size = (hi_seg - new_lo_seg) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    ikuword_t old_vec_size = (hi_seg - old_lo_seg) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    ikuword_t size_delta   = new_vec_size - old_vec_size;
    { /* Allocate a new  dirty vector.  The old slots go  to the tail of
	 the new  vector; the  head of  the new vector  is set  to zero,
	 which   means  the   corresponding   pages   are  marked   with
	 IK_PURE_WORD. */
      ikptr_t	new_dvec_base = ik_mmap(new_vec_size);
      bzero((char*)new_dvec_base, size_delta);
      memcpy((char*)(new_dvec_base + size_delta), (char*)pcb->dirty_vector_base, old_vec_size);
      ik_munmap((ikptr_t)pcb->dirty_vector_base, old_vec_size);
      pcb->dirty_vector_base = (uint32_t *)new_dvec_base;
      pcb->dirty_vector      = new_dvec_base - new_lo_seg * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    }
    { /* Allocate a new  segments vector.  The old slots go  to the tail
	 of the new vector;  the head of the new vector  is set to zero,
	 which   means  the   corresponding   pages   are  marked   with
	 "HOLE_MT". */
      ikptr_t	new_svec_base = ik_mmap(new_vec_size);
      bzero((char*)new_svec_base, size_delta);
      memcpy((char*)(new_svec_base + new_vec_size - old_vec_size), (char*)(pcb->segment_vector_base), old_vec_size);
      ik_munmap((ikptr_t)pcb->segment_vector_base, old_vec_size);
      pcb->segment_vector_base = (uint32_t *)new_svec_base;
      pcb->segment_vector      = (uint32_t *)(new_svec_base - new_lo_seg * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT);
    }
    pcb->memory_base = new_lo_seg * IK_SEGMENT_SIZE;
  } else if (end_ptr >= pcb->memory_end) {
    ikuword_t lo_seg       = IK_SEGMENT_INDEX(pcb->memory_base); /* unchanged */
    ikuword_t old_hi_seg   = IK_SEGMENT_INDEX(pcb->memory_end);
    ikuword_t new_hi_seg   = IK_SEGMENT_INDEX(end_ptr + IK_SEGMENT_SIZE - 1);
    ikuword_t new_vec_size = (new_hi_seg - lo_seg) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    ikuword_t old_vec_size = (old_hi_seg - lo_seg) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    ikuword_t size_delta   = new_vec_size - old_vec_size;
    { /* Allocate a new  dirty vector.  The old slots go  to the head of
	 the new  vector; the  tail of  the new vector  is set  to zero,
	 which   means  the   corresponding   pages   are  marked   with
	 IK_PURE_WORD. */
      ikptr_t new_dvec_base = ik_mmap(new_vec_size);
      memcpy((char*)new_dvec_base, (char*)pcb->dirty_vector_base, old_vec_size);
      bzero((char*)(new_dvec_base + old_vec_size), size_delta);
      ik_munmap((ikptr_t)pcb->dirty_vector_base, old_vec_size);
      pcb->dirty_vector_base = (uint32_t *)new_dvec_base;
      pcb->dirty_vector      = new_dvec_base - lo_seg * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    }
    { /* Allocate a new  segments vector.  The old slots go  to the head
	 of the new vector;  the tail of the new vector  is set to zero,
	 which   means  the   corresponding   pages   are  marked   with
	 "HOLE_MT". */
      ikptr_t new_svec_base = ik_mmap(new_vec_size);
      memcpy((char*)new_svec_base, (char*)pcb->segment_vector_base, old_vec_size);
      bzero((char*)(new_svec_base + old_vec_size), size_delta);
      ik_munmap((ikptr_t)pcb->segment_vector_base, old_vec_size);
      pcb->segment_vector_base = (uint32_t *) new_svec_base;
      pcb->segment_vector      = (uint32_t *) (new_svec_base - lo_seg * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT);
    }
    pcb->memory_end = new_hi_seg * IK_SEGMENT_SIZE;
  }
}


ikpcb_t*
ik_make_pcb (void)
{
  ikpcb_t * pcb = ik_malloc(sizeof(ikpcb_t));
  bzero(pcb, sizeof(ikpcb_t));

  /* The  Scheme heap  grows from  low memory  addresses to  high memory
   * addresses:
   *
   *     heap_base      growth         redline
   *         v         ------->           v
   *  lo mem |----------------------------+--------| hi mem
   *                       Scheme heap
   *         |.....................................| heap size
   *
   * when a Scheme  object is allocated on the heap  and its end crosses
   * the "red  line": either a garbage  collection runs, or the  heap is
   * enlarged.     See     the    functions     "ik_safe_alloc()"    and
   * "ik_unsafe_alloc()".
   */
  {
    IK_RUNTIME_MESSAGE("initialising Scheme heap's nursery hot block, size: %lu bytes, %lu pages",
		       (ik_ulong)ik_customisable_heap_nursery_size,
		       (ik_ulong)ik_customisable_heap_nursery_size/IK_PAGESIZE);
    pcb->heap_nursery_hot_block_base          = ik_mmap(ik_customisable_heap_nursery_size);
    pcb->heap_nursery_hot_block_size          = ik_customisable_heap_nursery_size;
    pcb->allocation_pointer = pcb->heap_nursery_hot_block_base;
    pcb->allocation_redline = pcb->heap_nursery_hot_block_base + ik_customisable_heap_nursery_size - IK_DOUBLE_PAGESIZE;
    /* Notice that below we will register the heap block in the segments
       vector. */
  }

  /* The Scheme  stack grows  from high memory  addresses to  low memory
   * addresses:
   *
   *    stack_base   redline    growth
   *         v          v      <-------
   *  lo mem |----------+--------------------------| hi mem
   *                       Scheme stack
   *         |.....................................| stack size
   *
   * when Scheme code execution uses  the stack crossing the "red line":
   * at the first subsequent function  call, the current Scheme stack is
   * stored away  in a Scheme continuation  and a new memory  segment is
   * allocated  and  installed as  Scheme  stack;  see for  example  the
   * "ik_stack_overflow()"  function.  When  the  function returns:  the
   * stored continuation  is reinstated  and execution continues  on the
   * old stack.
   *
   * The first stack frame starts from the end of the stack:
   *
   *    stack_base                   frame_pointer = frame_base
   *         v                                     v
   *  lo mem |-------------------------------------| hi mem
   *                       Scheme stack
   *
   * then, while nested  functions are called, new frames  are pushed on
   * the stack:
   *
   *    stack_base    frame_pointer            frame_base
   *         v             v                       v
   *  lo mem |-------------+-----------------------| hi mem
   *                  |....|xxxx|xxxxxx|xxx|xxxx|xx|
   *                           Scheme stack
   *   * Notice how "pcb->frame_base" references a  word that is one-off the
   * end of the stack segment; so the first word in the stack is:
   *
   *    pcb->frame_base - wordsize
   *
   * Also,  when   C  code   is  running,   "pcb->frame_pointer"  always
   * references the highest  memory address in the  lowest function call
   * frame; the machine word referenced by "pcb->frame_pointer" contains
   * the return address of the last  function call.  When Scheme code is
   * entered: "pcb->frame_pointer" is stored  in the %esp register; when
   * Scheme   code  is   exited   the  %esp   register   is  stored   in
   * "pcb->frame_pointer".
   *
   * See the function "ik_exec_code()" for details about entering Scheme
   * code execution.
   */
  {
    IK_RUNTIME_MESSAGE("initialising Scheme stack, size: %lu bytes, %lu pages",
		       (ik_ulong)ik_customisable_stack_size,
		       (ik_ulong)ik_customisable_stack_size/IK_PAGESIZE);
    pcb->stack_base	= ik_mmap(ik_customisable_stack_size);
    pcb->stack_size	= ik_customisable_stack_size;
    pcb->frame_pointer	= pcb->stack_base + pcb->stack_size;
    pcb->frame_base	= pcb->frame_pointer;
    if (IK_PROTECT_FROM_STACK_OVERFLOW) {
      /* Forbid reading  and writing in  the low-address memory  page of
       * the  stack  segment;  this  should  trigger  a  SIGSEGV  if  an
       * undetected  Scheme  stack  overflow happens.   Not  a  solution
       * against stack  overflows, but at  least it should  avoid memory
       * corruption.
       *
       *    stack_base                             frame_base
       *         v                                     v
       *  lo mem |-------------------------------------| hi mem
       *
       *         |.....|...............................|
       *       1st page         usable region
       *
       * This  configuration  must  be  repeated whenever  a  new  stack
       * segment is allocated because of detected stack overflow.
       */
      mprotect((void*)(ikuword_t)(pcb->stack_base), IK_PAGESIZE, PROT_NONE);
      pcb->frame_redline= pcb->stack_base + IK_DOUBLE_PAGESIZE + IK_PAGESIZE;
    } else {
      pcb->frame_redline= pcb->stack_base + IK_DOUBLE_PAGESIZE;
    }
    /* Notice  that  below we  will  register  the  stack block  in  the
       segments vector. */
  }

  /* Allocate and  initialise the page  cache; see the  documentation of
   * the PCB  struct for details.  We  link all the structs  in a linked
   * list, from the last slot to the first.
   *
   *          next            next
   *      -----------     -----------
   *     |           |   |           |
   *     v           |   v           |
   *   |---+---|---+---|---+---|---+---|---+---| allocated array
   *         |   ^           |   ^           |
   *         v   |           |   |           |
   *       NULL   -----------     -----------
   *                 next            next
   *   |.......|.......|.......|.......|.......|
   *    ikpage_t0 ikpage_t1 ikpage_t2 ikpage_t3 ikpage_t4
   */
  {
    ikpage_t *	cur  = (ikpage_t*)ik_mmap(IK_PAGE_CACHE_SIZE_IN_BYTES);
    ikpage_t *	past = cur + IK_PAGE_CACHE_NUM_OF_SLOTS;
    ikpage_t *	prev = NULL;
    pcb->cached_pages_base = (ikptr_t)cur;
    pcb->cached_pages_size = IK_PAGE_CACHE_SIZE_IN_BYTES;
    for (; cur < past; ++cur) {
      cur->next = prev;
      prev = cur;
    }
    pcb->cached_pages   = NULL;
    pcb->uncached_pages = prev;
  }

  /* Allocate and initialise the dirty vector and the segment vector.
   *
   * We forsee two possible scenarios:
   *
   *       Scheme heap              Scheme stack
   *    |--------------+----------+-------------| interesting memory
   *  begin              (unused?)             end
   *
   *      Scheme stack              Scheme heap
   *    |--------------+----------+-------------| interesting memory
   *  begin              (unused?)             end
   *
   * We compute two addresses: "lo_mem"  which is guaranteed to be below
   * "begin"; "hi_mem" which is guaranteed to be above "end".
   *
   * The dirty vector
   * ----------------
   *
   * The "dirty  vector" is an  array of "unsigned" integers  (which are
   * meant to  be 32-bit words), one  for each memory page  allocated by
   * Vicare; given  a memory address used  by Vicare, it is  possible to
   * compute the  index of the  corresponding slot in the  dirty vector.
   * Each slot can be one of two states:
   *
   *    0 -	The page is pure.
   *   -1 -	The page is dirty: some Scheme object in it has been
   *            mutated after the last garbage collection.
   *
   * such state is used by the garbage collector to decide which page to
   * scan, see  the function "scan_dirty_pages()".  For  example: when a
   * machine word location is  modified by "set-car!", the corresponding
   * slot in the dirty vector is set to -1.
   *
   * Indexes in  the dirty vector  are *not* zero-based.  The  fields in
   * the PCB are:
   *
   *   dirty_vector_base -
   *      Pointer to  the first byte  of memory allocated for  the dirty
   *      vector.
   *
   *   dirty_vector -
   *      Pointer to  a memory  address that  can be  used to  index the
   *      slots  in the  dirty vector,  with indexes  computed from  the
   *      actual memory addresses used by Vicare.
   *
   * it's like this:
   *
   *         base_offset            slots
   *    |...................|....................|
   *    |-------------------|--|--|--|--|--|--|--|
   *     ^                   ^
   *     dirty_vector        dirty_vector_base
   *
   * the first  slot is *not* "dirty_vector[0]",  rather some expression
   * like "dirty_vector[734]", where  734 is the value  computed here in
   * "lo_seg_idx".
   *
   * The segment vector
   * ------------------
   *
   * The "segment vector" is an  array of "unsigned" integers (which are
   * meant to  be 32-bit words), one  for each memory page  allocated by
   * Vicare; given a memory address (tagged or untagged) used by Vicare,
   * it is  possible to compute the  index of the corresponding  slot in
   * the  segment vector.   Each integer  represents the  type of  usage
   * Vicare makes  of the  page, the  garbage collection  generation the
   * page is in, and other meta informations; some of the types (defined
   * in the internal header file) are:
   *
   *   0            -	Unused memory.
   *   MAINHEAP_MT  -	Scheme heap memory.
   *   MAINSTACK_MT -	Scheme stack memory.
   *
   * Indexes in the segment vector  are *not* zero-based.  The fields in
   * the PCB are:
   *
   *   segment_vector_base -
   *      Pointer to the first byte  of memory allocated for the segment
   *      vector.
   *
   *   segment_vector -
   *      Pointer to  a memory  address that  can be  used to  index the
   *      slots in  the segment vector,  with indexes computed  from the
   *      actual memory addresses used by Vicare.
   *
   * it's like this:
   *
   *         base_offset            slots
   *    |...................|....................|
   *    |-------------------|--|--|--|--|--|--|--|
   *     ^                   ^
   *     segment_vector      segment_vector_base
   *
   * the first slot is *not* "segment_vector[0]", rather some expression
   * like "segment_vector[734]", where 734 is the value computed here in
   * "lo_seg_idx".
   */
  {
    ikptr_t	lo_mem, hi_mem;
    ikuword_t	lo_seg_idx, hi_seg_idx, vec_size, base_offset;
    if (pcb->heap_nursery_hot_block_base < pcb->stack_base) {
      lo_mem = pcb->heap_nursery_hot_block_base - IK_PAGESIZE;
      hi_mem = pcb->stack_base + pcb->stack_size + IK_PAGESIZE;
    } else {
      lo_mem = pcb->stack_base - IK_PAGESIZE;
      hi_mem = pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size + IK_PAGESIZE;
    }
    /* The segment index "lo_seg_idx" is  the index of the first segment
     * (lowest address) of used  memory.  The segment index "hi_seg_idx"
     * is the segment index of the  segment right after the last segment
     * (highest address) of used memory.
     *
     *          segment     segment     segment
     *    ---|-----------|-----------|-----------|--- used_memory
     *        ^                                   ^
     *        lo_seg_idx                          hi_seg_idx
     */
    lo_seg_idx  = IK_SEGMENT_INDEX(lo_mem);
    hi_seg_idx  = IK_SEGMENT_INDEX(hi_mem + IK_SEGMENT_SIZE - 1);
    base_offset = lo_seg_idx * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    vec_size    = (hi_seg_idx - lo_seg_idx) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    {
      ikptr_t	dvec = ik_mmap(vec_size);
      bzero((char*)dvec, vec_size);
      pcb->dirty_vector_base   = (uint32_t *)dvec;
      pcb->dirty_vector        = dvec - base_offset;
    }
    {
      ikptr_t	svec = ik_mmap(vec_size);
      bzero((char*)svec, vec_size);
      pcb->segment_vector_base = (uint32_t *)svec;
      pcb->segment_vector      = (uint32_t *)(svec - base_offset);
    }
    /* In the  whole system  memory we want  pointers to  delimiting the
       interesting memory:

       |---------------------------------------------| system_memory
       ^                        ^
       memory_base              memory_end
    */
    pcb->memory_base = (ikptr_t)(lo_seg_idx * IK_SEGMENT_SIZE);
    pcb->memory_end  = (ikptr_t)(hi_seg_idx * IK_SEGMENT_SIZE);

    /* Register  the heap  block and  the  stack block  in the  segments
       vector.   We  do this  here,  after  having  set the  PCB  fields
       "memory_base" and "memory_end". */
    set_page_range_type(pcb->heap_nursery_hot_block_base,  pcb->heap_nursery_hot_block_size,  MAINHEAP_MT,  pcb);
    set_page_range_type(pcb->stack_base, pcb->stack_size, MAINSTACK_MT, pcb);

#if 0
    fprintf(stderr, "\n*** Vicare debug:\n");
    fprintf(stderr, "*  pcb->heap_nursery_hot_block_base  = #x%lX\n", pcb->heap_nursery_hot_block_base);
    fprintf(stderr, "*  pcb->heap_nursery_hot_block_size  = %lu\n", pcb->heap_nursery_hot_block_size);
    fprintf(stderr, "*  pcb->stack_base = #x%lX\n", pcb->stack_base);
    fprintf(stderr, "*  pcb->stack_size = %lu\n", pcb->stack_size);
    fprintf(stderr, "*  lo_mem = #x%lX, hi_mem = #x%lX\n", lo_mem, hi_mem);
    fprintf(stderr, "*  lo_seg_idx = %lu, hi_seg_idx = %lu\n", lo_seg_idx, hi_seg_idx);
    fprintf(stderr, "*  vec_size = %lu bytes, %lu 32-bit words\n",
	    vec_size, vec_size/sizeof(uint32_t));
    fprintf(stderr, "*  memory_base = #x%lX\n", pcb->memory_base);
    fprintf(stderr, "*  memory_end  = #x%lX\n", pcb->memory_end);
    fprintf(stderr, "*  first dirty   slot: dirty_vector[%lu]\n",
	    ((ikuword_t)pcb->dirty_vector_base   - (ikuword_t)pcb->dirty_vector)/IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT);
    fprintf(stderr, "*  first segment slot: segment_vector[%lu]\n",
	    ((ikuword_t)pcb->segment_vector_base - (ikuword_t)pcb->segment_vector)/IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT);
    fprintf(stderr, "\n");
#endif
  }

  /* Initialize base structure type descriptor  (STD).  This is the type
     descriptor of all the struct type descriptors; it describes itself.
     See   the  Texinfo   documentation  node   "objects  structs"   for
     details. */
  {
    ikptr_t s_base_rtd = ik_unsafe_alloc(pcb, IK_ALIGN(rtd_size)) | rtd_tag;
    IK_REF(s_base_rtd, off_rtd_rtd)        = s_base_rtd;
    IK_REF(s_base_rtd, off_rtd_length)     = (ikptr_t) (rtd_size-wordsize);
    IK_REF(s_base_rtd, off_rtd_name)       = 0; /* = the fixnum 0 */
    IK_REF(s_base_rtd, off_rtd_fields)     = 0; /* = the fixnum 0 */
    IK_REF(s_base_rtd, off_rtd_printer)    = 0; /* = the fixnum 0 */
    IK_REF(s_base_rtd, off_rtd_symbol)     = 0; /* = the fixnum 0 */
    IK_REF(s_base_rtd, off_rtd_destructor) = IK_FALSE;
    pcb->base_rtd = s_base_rtd;
  }

  /* Initialise miscellaneous fields. */
  {
    pcb->collect_key         = IK_FALSE_OBJECT;
    pcb->not_to_be_collected = NULL;
  }
  return pcb;
}


void
ik_delete_pcb (ikpcb_t* pcb)
{
  { /* Release the page cache. */
    ikpage_t *	p = pcb->cached_pages;
    for (; p; p = p->next) {
      ik_munmap(p->base, IK_PAGESIZE);
    }
    pcb->cached_pages   = NULL;
    pcb->uncached_pages = NULL;
    ik_munmap(pcb->cached_pages_base, pcb->cached_pages_size);
  }
  {
    int i;
    for(i=0; i<IK_GC_GENERATION_COUNT; i++) {
      ik_ptr_page_t* p = pcb->protected_list[i];
      while (p) {
        ik_ptr_page_t* next = p->next;
        ik_munmap((ikptr_t)p, IK_PAGESIZE);
	p = next;
      }
    }
  }
  ikptr_t	base = pcb->memory_base;
  ikptr_t	end  = pcb->memory_end;
  { /* Release all the used pages. */
    uint32_t *	segment_vec  = pcb->segment_vector;
    ikuword_t	page_idx     = IK_PAGE_INDEX(base);
    ikuword_t	page_idx_end = IK_PAGE_INDEX(end);
    for (; page_idx < page_idx_end; ++page_idx) {
      if (HOLE_MT != segment_vec[page_idx]) {
	ik_munmap((ikptr_t)(page_idx << IK_PAGESHIFT), IK_PAGESIZE);
      }
    }
  }
  { /* Release the dirty vector and the segments vector. */
    ikuword_t	lo_seg   = IK_SEGMENT_INDEX(base);
    ikuword_t	hi_seg   = IK_SEGMENT_INDEX(end);
    ikuword_t	vec_size = (hi_seg - lo_seg) * IK_PAGE_VECTOR_SLOTS_PER_LOGIC_SEGMENT;
    ik_munmap((ikptr_t)pcb->dirty_vector_base,   vec_size);
    ik_munmap((ikptr_t)pcb->segment_vector_base, vec_size);
  }
  ik_free(pcb, sizeof(ikpcb_t));
}


/** --------------------------------------------------------------------
 ** Basic memory allocation for Scheme objects.
 ** ----------------------------------------------------------------- */

ikptr_t
ik_safe_alloc (ikpcb_t * pcb, ikuword_t aligned_size)
/* Reserve a memory  block on the Scheme heap and  return a reference to
   it as an *untagged* pointer.   PCB must reference the process control
   block, ALIGNED_SIZE  must be the  requested number of  bytes filtered
   through "IK_ALIGN()".

   If not  enough memory  is available on  the current  heap's nursery
   segment, a garbage collection is  triggered; then allocation is tried
   again: if it  still fails the process is terminated  with exit status
   EXIT_FAILURE.

   The reserved memory is NOT initialised to safe values: its contents
   have  to be  considered  invalid.  However,  notice  that the  heap's
   nursery is NOT a garbage collection root; so if we leave some machine
   word uninitialised  on the heap,  outside of Scheme  objects: nothing
   bad happens, because the garbage collector never sees them. */
{
  assert(aligned_size == IK_ALIGN(aligned_size));
  ikptr_t		alloc_ptr;
  ikptr_t		end_ptr;
  ikptr_t		new_alloc_ptr;
  alloc_ptr	= pcb->allocation_pointer;
  new_alloc_ptr	= alloc_ptr + aligned_size;
  end_ptr	= pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size;
  if (new_alloc_ptr < end_ptr) {
    /* There is room in the current heap's nursery hot block: update the
       PCB and return the offset. */
    pcb->allocation_pointer = new_alloc_ptr;
  } else if (ik_garbage_collection_is_forbidden) {
    /* Running garbage  collection is  currently suspended.   Let's make
       some room as "ik_unsafe_alloc()" does, then reserve the space. */
    ik_make_room_in_heap_nursery(pcb, aligned_size);
    alloc_ptr		= pcb->allocation_pointer;
    new_alloc_ptr	= alloc_ptr + aligned_size;
    end_ptr		= pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size;
    if (new_alloc_ptr < end_ptr) {
      pcb->allocation_pointer = new_alloc_ptr;
      return alloc_ptr;
    } else {
      ik_abort("unable to reserve enough room for %lu bytes", aligned_size);
    }
  } else {
    /* No room in the current heap's nursery hot block: run GC. */
    IK_RUNTIME_MESSAGE("%s: calling GC, requested size: %lu bytes, free space: %lu bytes",
		       __func__, (ik_ulong)aligned_size, (ik_ulong)(end_ptr - alloc_ptr));
    ik_automatic_collect_from_C(aligned_size, pcb);
    {
      alloc_ptr		= pcb->allocation_pointer;
      end_ptr		= pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size;
      new_alloc_ptr	= alloc_ptr + aligned_size;
      if (new_alloc_ptr < end_ptr)
	pcb->allocation_pointer = new_alloc_ptr;
      else
	ik_abort("collector did not leave enough room for %lu bytes", aligned_size);
    }
  }
  return alloc_ptr;
}
ikptr_t
ik_unsafe_alloc (ikpcb_t * pcb, ikuword_t aligned_size)
/* Reserve a memory  block on the Scheme heap and  return a reference to
   it as an *untagged* pointer.   PCB must reference the process control
   block, ALIGNED_SIZE  must be the  requested number of  bytes filtered
   through "IK_ALIGN()".  This function is  meant to be used to allocate
   "small" memory blocks.

   If not  enough memory  is available on  the current  heap's nursery
   segment: a new  heap segment is allocated; if  such allocation fails:
   the process is terminated with exit status EXIT_FAILURE.

   The reserved memory is NOT initialised to safe values: its contents
   have  to be  considered  invalid.  However,  notice  that the  heap's
   nursery is NOT a garbage collection root; so if we leave some machine
   word uninitialised  on the heap,  outside of Scheme  objects: nothing
   bad happens, because the garbage collector never sees them. */
{
  assert(aligned_size == IK_ALIGN(aligned_size));
  ikptr_t alloc_ptr       = pcb->allocation_pointer;
  ikptr_t end_ptr         = pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size;
  ikptr_t new_alloc_ptr   = alloc_ptr + aligned_size;
  if (new_alloc_ptr < end_ptr) {
    /* There is  room in the  current heap  nursery: update the  PCB and
       return the offset. */
    pcb->allocation_pointer = new_alloc_ptr;
    return alloc_ptr;
  } else {
    /* No  room  in  the  current  heap nursery:  enlarge  the  heap  by
       allocating new memory. */
    ik_make_room_in_heap_nursery(pcb, aligned_size);
    alloc_ptr			= pcb->allocation_pointer;
    pcb->allocation_pointer	= alloc_ptr + aligned_size;
    return alloc_ptr;
  }
}
void
ik_make_room_in_heap_nursery (ikpcb_t * pcb, ikuword_t aligned_size)
/* To be called when there is no  room in the current heap nursery's hot
   block to reserve ALIGNED_SIZE bytes for a Scheme object.  Enlarge the
   heap's nursery by  allocating a new memory segment to  become the new
   hot block; store the old hot block in the PCB. */
{
  assert(aligned_size == IK_ALIGN(aligned_size));
#ifndef NDEBUG
  {
    ikptr_t alloc_ptr       = pcb->allocation_pointer;
    ikptr_t end_ptr         = pcb->heap_nursery_hot_block_base + pcb->heap_nursery_hot_block_size;
    ikptr_t new_alloc_ptr   = alloc_ptr + aligned_size;
    assert((new_alloc_ptr >= end_ptr) || (new_alloc_ptr >= pcb->allocation_redline));
  }
#endif
  if (pcb->allocation_pointer) {
    /* This is not the first heap's nursery block allocation, so prepend
       a  new "ikmemblock_t"  node to  the  linked list  of old  nursery
       blocks  and  initialise  it  with  a  reference  to  the  current
       nursery's hot block. */
    ikmemblock_t *	node = (ikmemblock_t *)ik_malloc(sizeof(ikmemblock_t));
    node->base = pcb->heap_nursery_hot_block_base;
    node->size = pcb->heap_nursery_hot_block_size;
    node->next = pcb->full_heap_nursery_segments;
    pcb->full_heap_nursery_segments = node;
    IK_RUNTIME_MESSAGE("%s: stored full heap nursery hot block, size: %lu bytes, %lu pages",
		       __func__,
		       (ik_ulong)node->size, (ik_ulong)node->size/IK_PAGESIZE);
  }
  /* Accounting.  We keep count of all the bytes allocated for the heap,
   * so that:
   *
   *   total_allocated_bytes = \
   *     IK_MOST_BYTES_IN_MINOR * pcb->allocation_count_major + pcb->allocation_count_minor
   */
  {
    ikuword_t bytes = ((ikuword_t)pcb->allocation_pointer) - ((ikuword_t)pcb->heap_nursery_hot_block_base);
    ikuword_t minor = bytes + pcb->allocation_count_minor;
    while (minor >= IK_MOST_BYTES_IN_MINOR) {
      minor -= IK_MOST_BYTES_IN_MINOR;
      pcb->allocation_count_major++;
    }
    pcb->allocation_count_minor = minor;
  }
  /* Allocate a  new heap's nursery  segment and register it  as current
   * nursery's hot block.   While computing the segment  size: make sure
   * that there is always  some room at the end of  the new heap segment
   * after allocating the requested memory for the new object.
   *
   * Initialise it as follows:
   *
   *     heap_base                allocation_redline
   *         v                            v
   *  lo mem |----------------------------+--------| hi mem
   *                       Scheme heap
   *         |.....................................|
   *              heap_nursery_hot_block_size
   */
  {
    ikptr_t	heap_ptr;
    ikuword_t	new_size;
    if (aligned_size > (ik_customisable_heap_nursery_size - IK_DOUBLE_PAGESIZE)) {
      new_size = IK_ALIGN_TO_NEXT_PAGE(aligned_size + IK_DOUBLE_PAGESIZE);
    } else {
      new_size = ik_customisable_heap_nursery_size;
    }
    heap_ptr				= ik_mmap_mainheap(new_size, pcb);
    pcb->heap_nursery_hot_block_base	= heap_ptr;
    pcb->heap_nursery_hot_block_size	= new_size;
    pcb->allocation_redline		= heap_ptr + new_size - IK_DOUBLE_CHUNK_SIZE;
    pcb->allocation_pointer		= heap_ptr;
    IK_RUNTIME_MESSAGE("%s: allocated new heap nursery hot block, size: %lu bytes, %lu pages",
		       __func__, (ik_ulong)new_size, (ik_ulong)new_size/IK_PAGESIZE);
  }
}
void
ik_signal_dirt_in_page_of_pointer (ikpcb_t * pcb, ikptr_t s_pointer)
{
  IK_SIGNAL_DIRT_IN_PAGE_OF_POINTER(pcb, s_pointer);
}


/** --------------------------------------------------------------------
 ** Run-time configuration.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_scheme_heap_nursery_size_ref (ikpcb_t * pcb)
{
  return ika_integer_from_ulong(pcb, ik_customisable_heap_nursery_size);
}
ikptr_t
ikrt_scheme_heap_nursery_size_set (ikptr_t s_num_of_bytes, ikpcb_t * pcb)
{
  ik_customisable_heap_nursery_size = IK_ALIGN_TO_NEXT_PAGE(ik_integer_to_ulong(s_num_of_bytes));
  if (0) {
    fprintf(stderr, "%s: set customisable heap nursery size to %lu\n",
	    __func__, ik_customisable_heap_nursery_size);
  }
  return IK_VOID;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_scheme_stack_size_ref (ikpcb_t * pcb)
{
  return ika_integer_from_ulong(pcb, ik_customisable_stack_size);
}
ikptr_t
ikrt_scheme_stack_size_set (ikptr_t s_num_of_bytes, ikpcb_t * pcb)
{
  ik_customisable_stack_size = IK_ALIGN_TO_NEXT_PAGE(ik_integer_to_ulong(s_num_of_bytes));
  return IK_VOID;
}

/* ------------------------------------------------------------------ */

ikptr_t
ikrt_automatic_garbage_collection_status (ikpcb_t * pcb)
{
  return IK_BOOLEAN_FROM_INT(!ik_garbage_collection_is_forbidden);
}
ikptr_t
ikrt_enable_disable_automatic_garbage_collection (ikptr_t s_enable, ikpcb_t * pcb)
{
  ik_garbage_collection_is_forbidden = !IK_BOOLEAN_TO_INT(s_enable);
  return IK_VOID;
}


/** --------------------------------------------------------------------
 ** Internals inspection messages.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_enable_runtime_messages (ikpcb_t pcb)
{
  ik_enabled_runtime_messages = 1;
  return IK_VOID;
}
ikptr_t
ikrt_disable_runtime_messages (ikpcb_t pcb)
{
  ik_enabled_runtime_messages = 0;
  return IK_VOID;
}
void
ik_runtime_message (const char * message, ...)
{
  va_list        ap;
  va_start(ap, message);
  fprintf(stderr, "vicare: runtime: ");
  vfprintf(stderr, message, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}


void
ik_debug_message (const char * error_message, ...)
{
  va_list        ap;
  va_start(ap, error_message);
  fprintf(stderr, "vicare: debug: ");
  vfprintf(stderr, error_message, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}
void
ik_debug_message_no_newline (const char * error_message, ...)
{
  va_list        ap;
  va_start(ap, error_message);
  fprintf(stderr, "vicare: debug: ");
  vfprintf(stderr, error_message, ap);
  va_end(ap);
}
void
ik_debug_message_start (const char * error_message, ...)
{
  va_list        ap;
  va_start(ap, error_message);
  fprintf(stderr, "\nvicare: debug: ");
  vfprintf(stderr, error_message, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}
int
ik_abort (const char * error_message, ...)
{
  va_list        ap;
  va_start(ap, error_message);
  fprintf(stderr, "vicare: error: ");
  vfprintf(stderr, error_message, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(EXIT_FAILURE);
  return EXIT_FAILURE;
}
void
ik_error (ikptr_t args)
{
  fprintf(stderr, "vicare: error: ");
  ik_fprint(stderr, args);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}


void
ik_stack_overflow (ikpcb_t* pcb)
/* Let's recall  how the  Scheme stack  is managed; at  first we  have a
 * single stack segment:
 *
 *    stack_base   redline        growth     frame_base
 *         v          v          <------        v
 *  lo mem |----------+----------------------|-| hi mem
 *                                            v
 *                                     ik_underflow_handler
 *
 * where the highest machine word is  set to the address of the assembly
 * label "ik_underflow_handler",  defined in the  file "ikarus-enter.S",
 * to  which the  execution  flow  returns after  the  last Scheme  code
 * execution completes.
 *
 * When the use  of the stack passes the redline:  this very function is
 * called;  the current  stack segment  is frozen  into a  continuation
 * object, registered in  the PCB as "next process  continuation"; a new
 * stack segment  is allocated and  initialised in  the same way  of the
 * old:
 *
 *    stack_base   redline        growth    frame_base
 *         v          v          <------        v
 *  lo mem |----------+----------------------|-| hi mem
 *                                            v
 *                                     ik_underflow_handler
 *
 * When  use of  the  new stack  segment is  finished:  the Scheme  code
 * execution returns to the  "ik_underflow_handler" label, which will do
 * what is  needed to retrieve the  frozen stack frames and  resume the
 * continuation.
 *
 * Notice that  "ik_stack_overflow()" is  always called by  the assembly
 * routine "ik_foreign_call"  with code that  does not touch  the Scheme
 * stack (because "ik_stack_overflow()" has  no Scheme arguments).  Upon
 * entering this function, assuming there are 2 frames, the situation on
 * the old Scheme stack is as follows:
 *
 *         high memory
 *   |                      | <-- pcb->frame_base
 *   |----------------------|
 *   | ik_underflow_handler |
 *   |----------------------|                         --
 *   |    local value 1     |                         .
 *   |----------------------|                         .
 *   |    local value 1     |                         . framesize 1
 *   |----------------------|                         .
 *   |   return address 1   |                         .
 *   |----------------------|                         --
 *   |    local value 0     | <-- pcb->frame_redline  .
 *   |----------------------|                         .
 *   |    local value 0     |                         . framesize 0
 *   |----------------------|                         .
 *   |   return address 0   | <-- pcb->frame_pointer  .
 *   |----------------------|                         --
 *             ...
 *   |----------------------|
 *   |                      | <-- pcb->stack_base
 *   |----------------------|
 *   |                      |
 *         low memory
 *
 * where  the frame  0  is  the one  that  crossed  the redline  causing
 * "ik_stack_overflow()" to be called.   Right after initialisation, the
 * situation of the new Scheme stack is as follows:
 *
 *         high memory
 *   |                      | <-- pcb->frame_base
 *   |----------------------|
 *   | ik_underflow_handler | <-- pcb->frame_pointer
 *   |----------------------|
 *             ...
 *   |----------------------|
 *   |                      | <-- pcb->frame_redline
 *   |----------------------|
 *             ...
 *   |----------------------|
 *   |                      | <-- pcb->stack_base
 *   |----------------------|
 *   |                      |
 *         low memory
 *
 * So  after   returning  from  this  function:   the  assembly  routine
 * "ik_foreign_call" will return to  the label "ik_underflow_handler and
 * the underflow handler will do its job.
 */
#define STACK_DEBUG	0
{
  if (0 || STACK_DEBUG) {
    ik_debug_message("%s: enter pcb=0x%016lx", __func__, (long)pcb);
  }
  assert(pcb->frame_pointer <= pcb->frame_base);
  assert(pcb->frame_pointer <= pcb->frame_redline);
  assert(IK_UNDERFLOW_HANDLER == IK_REF(pcb->frame_base, -wordsize));
  /* Freeze the  Scheme stack segment  into a continuation  and register
     the continuation object in the  PCB as "next process continuation".
     Mark the old Scheme stack segment as "data".*/
  {
    ikcont_t *	kont   = (ikcont_t*)ik_unsafe_alloc(pcb, IK_ALIGN(continuation_size));
    ikptr_t	s_kont = ((ikptr_t)kont) | continuation_primary_tag;
    kont->tag  = continuation_tag;
    kont->top  = pcb->frame_pointer;
    kont->size = pcb->frame_base - pcb->frame_pointer - wordsize;
    kont->next = pcb->next_k;
    pcb->next_k = s_kont;
    set_page_range_type(pcb->stack_base, pcb->stack_size, DATA_MT, pcb);
    assert(0 != kont->size);
    if (IK_PROTECT_FROM_STACK_OVERFLOW) {
      /* Release the protection on the  first low-address memory page in
	 the stack  segment, which avoids  memory corruption in  case of
	 undetected Scheme stack overflow. */
      mprotect((void*)(pcb->stack_base), IK_PAGESIZE, PROT_READ|PROT_WRITE);
    }
  }
  /* Allocate a  new memory segment to  be used as Scheme  stack and set
     the PCB accordingly. */
  {
    IK_RUNTIME_MESSAGE("allocating a new Scheme stack, size: %lu bytes, %lu pages",
		       (ik_ulong)ik_customisable_stack_size,
		       (ik_ulong)ik_customisable_stack_size/IK_PAGESIZE);
    pcb->stack_base	= ik_mmap_typed(ik_customisable_stack_size, MAINSTACK_MT, pcb);
    pcb->stack_size	= ik_customisable_stack_size;
    pcb->frame_base	= pcb->stack_base + ik_customisable_stack_size;
    pcb->frame_pointer	= pcb->frame_base - wordsize;
    IK_REF(pcb->frame_pointer, 0) = IK_UNDERFLOW_HANDLER;
    if (IK_PROTECT_FROM_STACK_OVERFLOW) {
      /* Forbid reading  and writing in  the low-address memory  page of
       * the  stack  segment;  this  should  trigger  a  SIGSEGV  if  an
       * undetected  Scheme  stack  overflow happens.   Not  a  solution
       * against stack  overflows, but at  least it should  avoid memory
       * corruption.
       *
       *    stack_base                             frame_base
       *         v                                     v
       *  lo mem |-------------------------------------| hi mem
       *
       *         |.....|...............................|
       *       1st page         usable region
       *
       * This configuration must be performed also when first allocating
       * the stack segment.
       */
      mprotect((void*)(pcb->stack_base), IK_PAGESIZE, PROT_NONE);
      pcb->frame_redline= pcb->stack_base + IK_DOUBLE_CHUNK_SIZE + IK_PAGESIZE;
    } else {
      pcb->frame_redline= pcb->stack_base + IK_DOUBLE_CHUNK_SIZE;
    }
  }
  if (0 || STACK_DEBUG) {
    ik_debug_message("%s: leave pcb=0x%016lx", __func__, (long)pcb);
  }
}


ikptr_t
ik_uuid (ikptr_t s_bv)
{
  static const char *	uuid_chars = "!$%&/0123456789<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static int		uuid_len;
  static int		fd = -1;
  if (-1 == fd) {
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
      return ik_errno_to_code();
    }
    uuid_len = strlen(uuid_chars);
  }
  {
    uint8_t *	data     = IK_BYTEVECTOR_DATA_UINT8P(s_bv);
    iksword_t	data_len = IK_BYTEVECTOR_LENGTH(s_bv);
    ssize_t	r        = read(fd, data, data_len);
    if (r < 0) {
      return ik_errno_to_code();
    }
    for (uint8_t *p=data, *p_end=data+data_len; p < p_end; ++p) {
      *p = uuid_chars[*p % uuid_len];
    }
  }
  return s_bv;
}


/** --------------------------------------------------------------------
 ** Helper functions for debugging purposes.
 ** ----------------------------------------------------------------- */

static char*
mtname (unsigned n)
{
  if (n == MAINHEAP_TYPE)  { return "HEAP_T"; }
  if (n == MAINSTACK_TYPE) { return "STAK_T"; }
  if (n == POINTERS_TYPE)  { return "PTER_T"; }
  if (n == DATA_TYPE)      { return "DATA_T"; }
  if (n == CODE_TYPE)      { return "CODE_T"; }
  if (n == HOLE_TYPE)      { return "      "; }
  return "WHAT_T";
}
ikptr_t
ik_dump_metatable (ikpcb_t* pcb)
{
  unsigned* s = pcb->segment_vector_base;
  ikptr_t p = pcb->memory_base;
  ikptr_t hi = pcb->memory_end;
  while (p < hi) {
    unsigned t = *s & TYPE_MASK;
    ikptr_t start = p;
    p += IK_PAGESIZE;
    s++;
    while ((p < hi) && ((*s & TYPE_MASK) == t)) {
      p += IK_PAGESIZE;
      s++;
    }
    fprintf(stderr, "0x%016lx + %5ld pages = %s\n",
	    (long) start,
	    ((long)p-(long)start)/IK_PAGESIZE,
	    mtname(t));
  }
  return IK_VOID_OBJECT;
}
ikptr_t
ik_dump_dirty_vector (ikpcb_t* pcb)
{
  unsigned* s  = pcb->dirty_vector_base;
  ikptr_t     p  = pcb->memory_base;
  ikptr_t     hi = pcb->memory_end;
  while (p < hi) {
    unsigned t     = *s;
    ikptr_t    start = p;
    p += IK_PAGESIZE;
    s++;
    while ((p < hi) && (*s == t)) {
      p += IK_PAGESIZE;
      s++;
    }
    fprintf(stderr, "0x%016lx + %5ld pages = 0x%08x\n",
	    (long) start,
	    ((long)p-(long)start)/IK_PAGESIZE,
	    t);
  }
  return IK_VOID_OBJECT;
}


/** --------------------------------------------------------------------
 ** Code objects constructor and auxiliary functions.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_make_code (ikptr_t s_code_size, ikptr_t s_freevars, ikptr_t s_relocation_vector, ikpcb_t* pcb)
/* Build a new code object and return a reference to it.  S_CODE_SIZE is
   a  non-negative   fixnum  representing   the  requested   code  size.
   S_FREEVARS is a  non-negative fixnum representing the  number of free
   variables in the  code.  S_RELOCATION_VECTOR is an  empty vector used
   to  initialise  the relocation  vector  field;  this argument  exists
   because it is  easier and more efficient to allocate  an empty vector
   from Scheme  code than here from  C code (the empty  vector in Scheme
   code is a constant: only one is allocated). */
{
  assert(IK_IS_FIXNUM(s_code_size));
  assert(IK_IS_FIXNUM(s_freevars));
  assert(ik_is_vector(s_relocation_vector));
  assert(0 == IK_VECTOR_LENGTH(s_relocation_vector));
  iksword_t	code_size = IK_UNFIX(s_code_size);
  /* We allocate a number of bytes equal to the size of the least number
   * of pages required  to hold CODE_SIZE plus the  meta data.  Example:
   * if the size is less than IK_PAGESIZE:
   *
   *            IK_PAGESIZE
   *   |-------------------------| memreq
   *   |---------|---------| code object
   *    meta data code_size
   *
   * Example: if CODE_SIZE is greater than IK_PAGESIZE:
   *
   *            IK_PAGESIZE              IK_PAGESIZE
   *   |-------------------------|-------------------------| memreq
   *   |---------|-------------------------| code object
   *    meta data         code_size
   *
   * All the allocated pages have  execution protection set by "mmap()".
   * In  the  segments  vector:  the  first  page  is  marked  as  code;
   * subsequent pages are marked as data.
   */
  ikuword_t	memreq	= IK_ALIGN_TO_NEXT_PAGE(disp_code_data + code_size);
  /* P_CODE  references  the first  byte  in  the pages  allocated  with
     "mmap()" with execution protection. */
  ikptr_t	p_code	= ik_mmap_code(memreq, 0, pcb);
  bzero((char*)p_code, memreq);
  IK_REF(p_code, disp_code_tag)		  = code_tag;
  IK_REF(p_code, disp_code_code_size)	  = s_code_size;
  IK_REF(p_code, disp_code_reloc_vector)  = s_relocation_vector;
  IK_REF(p_code, disp_code_freevars)	  = s_freevars;
  IK_REF(p_code, disp_code_annotation)	  = IK_FALSE;
  IK_REF(p_code, disp_code_unused)	  = IK_FIX(0);
  return p_code | code_primary_tag;
}
ikptr_t
ikrt_set_code_reloc_vector (ikptr_t s_code, ikptr_t s_vec, ikpcb_t* pcb)
{
  IK_REF(s_code, off_code_reloc_vector) = s_vec;
  IK_SIGNAL_DIRT_IN_PAGE_OF_POINTER(pcb, IK_PTR(s_code, off_code_reloc_vector));
  ik_relocate_code(s_code - code_primary_tag);
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_set_code_annotation (ikptr_t s_code, ikptr_t s_annot, ikpcb_t* pcb)
{
  IK_REF(s_code, off_code_annotation) = s_annot;
  IK_SIGNAL_DIRT_IN_PAGE_OF_POINTER(pcb, IK_PTR(s_code, off_code_annotation));
  return IK_VOID;
}


/** --------------------------------------------------------------------
 ** Guardians handling.
 ** ----------------------------------------------------------------- */

/* The tagged  pointers referencing  guardians are  stored in  the array
   "protected_list" of the structure  "ik_ptr_page_t"; such structures are
   nodes   in   a  linked   list   referenced   by  the   PCB's   member
   "protected_list".
*/

ikptr_t
ikrt_register_guardian_pair (ikptr_t p0, ikpcb_t* pcb)
/* Register a guardian  pair in the protected list of  PCB.  If there is
   no more room in the current  protected list node: allocate a new node
   and prepend it to the linked list.  Return the void object. */
{
  /* FIRST is  a pointer  to the first  node in  a linked list.   If the
     linked list is empty or the first node is full: allocate a new node
     and prepend it to the list. */
  ik_ptr_page_t *	first = pcb->protected_list[IK_GUARDIANS_GENERATION_NUMBER];
  if ((NULL == first) || (IK_PTR_PAGE_NUMBER_OF_GUARDIANS_SLOTS == first->count)) {
    assert(sizeof(ik_ptr_page_t) == IK_PAGESIZE);
    ik_ptr_page_t *	new_node;
    new_node        = (ik_ptr_page_t*)ik_mmap(IK_PAGESIZE);
    new_node->count = 0;
    new_node->next  = first;
    first           = new_node;
    pcb->protected_list[IK_GUARDIANS_GENERATION_NUMBER] = new_node;
  }
  first->ptr[first->count++] = p0; /* store the guardian pair */
  return IK_VOID_OBJECT;
}
ikptr_t
ikrt_register_guardian (ikptr_t tc, ikptr_t obj, ikpcb_t* pcb)
{
  ikptr_t p0   = IKU_PAIR_ALLOC(pcb);
  IK_CAR(p0) = tc;
  IK_CDR(p0) = obj;
  return ikrt_register_guardian_pair(p0, pcb);
}


/** --------------------------------------------------------------------
 ** Garbage collections statistics.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_stats_now (ikptr_t t, ikpcb_t* pcb)
{
  struct rusage r;
  struct timeval s;
  gettimeofday(&s, 0);
  getrusage(RUSAGE_SELF, &r);
  /* Do  not change  the  order  of the  fields!!!   It  must match  the
     implementation     of     the     record    type     "stats"     in
     "scheme/ikarus.timer.sls". */
  IK_FIELD(t,  0) = IK_FIX(r.ru_utime.tv_sec);
  IK_FIELD(t,  1) = IK_FIX(r.ru_utime.tv_usec);
  IK_FIELD(t,  2) = IK_FIX(r.ru_stime.tv_sec);
  IK_FIELD(t,  3) = IK_FIX(r.ru_stime.tv_usec);
  IK_FIELD(t,  4) = IK_FIX(s.tv_sec);
  IK_FIELD(t,  5) = IK_FIX(s.tv_usec);
  IK_FIELD(t,  6) = IK_FIX(pcb->collection_id);
  IK_FIELD(t,  7) = IK_FIX(pcb->collect_utime.tv_sec);
  IK_FIELD(t,  8) = IK_FIX(pcb->collect_utime.tv_usec);
  IK_FIELD(t,  9) = IK_FIX(pcb->collect_stime.tv_sec);
  IK_FIELD(t, 10) = IK_FIX(pcb->collect_stime.tv_usec);
  IK_FIELD(t, 11) = IK_FIX(pcb->collect_rtime.tv_sec);
  IK_FIELD(t, 12) = IK_FIX(pcb->collect_rtime.tv_usec);
  { /* minor bytes */
    ikuword_t bytes_in_heap	= ((ikuword_t)pcb->allocation_pointer) - ((ikuword_t)pcb->heap_nursery_hot_block_base);
    ikuword_t bytes		= bytes_in_heap + pcb->allocation_count_minor;
    IK_FIELD(t, 13)		= IK_FIX(bytes);
  }
  /* major bytes */
  IK_FIELD(t, 14) = IK_FIX(pcb->allocation_count_major);
  return IK_VOID_OBJECT;
}


/** --------------------------------------------------------------------
 ** Process termination.
 ** ----------------------------------------------------------------- */

ikptr_t
ikrt_exit (ikptr_t status, ikpcb_t* pcb)
/* This function is the core of implementation of the EXIT proceure from
   "(rnrs programs (6))". */
{
  ik_delete_pcb(pcb);
  if (total_allocated_pages)
    ik_debug_message("allocated pages: %d", total_allocated_pages);
  assert(0 == total_allocated_pages);
  exit(IK_IS_FIXNUM(status)? IK_UNFIX(status) : EXIT_FAILURE);
}

/* end of file */
