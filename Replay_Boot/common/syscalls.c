/***********************************************************************/
/*                                                                     */
/*  SYSCALLS.C:  System Calls for the newlib                                      */
/*  most of this is from newlib-lpc and a Keil-demo                    */
/*                                                                     */
/*  These are "reentrant functions" as needed by                       */
/*  the WinARM-newlib-config, see newlib-manual.                       */
/*  Collected and modified by Martin Thomas                            */
/*                                                                     */
/***********************************************************************/

/* adapted for the SAM7 at91_lib DBGU - mthomas 4/2006 */

#include <stdlib.h>
#include <sys/stat.h>
#include "board.h"

int isatty(int file); /* avoid warning */

int isatty(int file)
{
	return 1;
}


#if 0
static void _exit (int n) {
label:  goto label; /* endless loop */
}
#endif


/* "malloc clue function" from newlib-lpc/Keil-Demo/"generic" */

/**** Locally used variables. ****/
// mt: "cleaner": extern char* end;
extern char end[];              /*  end is set in the linker command    */
				/* file and is the end of statically    */
				/* allocated data (thus start of heap). */

static char *heap_ptr;          /* Points to current end of the heap.   */

/************************** _sbrk_r *************************************
 * Support function. Adjusts end of heap to provide more memory to
 * memory allocator. Simple and dumb with no sanity checks.

 *  struct _reent *r -- re-entrancy structure, used by newlib to
 *                      support multiple threads of operation.
 *  ptrdiff_t nbytes -- number of bytes to add.
 *                      Returns pointer to start of new heap area.
 *
 *  Note:  This implementation is not thread safe (despite taking a
 *         _reent structure as a parameter).
 *         Since _s_r is not used in the current implementation,
 *         the following messages must be suppressed.
 */
void * _sbrk_r(
	struct _reent *_s_r,
	ptrdiff_t nbytes)
{
	char  *base;                /*  errno should be set to  ENOMEM on error  */

	if (!heap_ptr) {    /*  Initialize if first time through.  */
		heap_ptr = end;
	}
	base = heap_ptr;    /*  Point to end of heap.  */
	heap_ptr += nbytes; /*  Increase heap.  */

	return base;                /*  Return pointer to start of new heap area.  */
}

// only needed when using mallinfo()
int _close(int __fildes ) { return -1; }
int	_fstat( int __fd, struct stat *__sbuf ) { return -1; }
int	_isatty(int __fildes ) { return 1; }
_off_t _lseek(int __fildes, _off_t __offset, int __whence ) { return (_off_t)-1;}
_ssize_t _write(int __fd, const void *__buf, size_t __nbyte ) { return -1; }
_ssize_t _read(int __fd, void *__buf, size_t __nbyte ) { return -1; }
