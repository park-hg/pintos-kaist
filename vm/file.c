/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <string.h>

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */

bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	struct file_info *f_info = (struct file_info *) page->uninit.aux;

	file_page->file = f_info->file;
	file_page->ofs = f_info->ofs;
	file_page->read_bytes = f_info->read_bytes;
	file_page->zero_bytes = f_info->zero_bytes;
    lock_acquire(&filesys_lock);

	if (file_read_at (f_info->file, kva, f_info->read_bytes, f_info->ofs) != f_info->read_bytes) {
        lock_release(&filesys_lock);
		vm_dealloc_page (page);

		return false;
	}
    lock_release(&filesys_lock);
    if (f_info->zero_bytes > 0)
	    memset (kva + f_info->read_bytes, 0, f_info->zero_bytes);
	
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
    lock_acquire(&filesys_lock);

	if (file_read_at (file_page->file, kva, file_page->read_bytes, file_page->ofs) != file_page->read_bytes) {
        lock_release(&filesys_lock);
		vm_dealloc_page (page);

		return false;
	}
    lock_release(&filesys_lock);

    if (file_page->zero_bytes > 0)
	    memset (kva + file_page->read_bytes, 0, file_page->zero_bytes);
	
	return true;

}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

    if (pml4_is_dirty(page->thread->pml4, page)) {
        lock_acquire(&filesys_lock);
        if (file_write_at (file_page->file, page->va, file_page->read_bytes, file_page->ofs) != file_page->read_bytes) {
            lock_release(&filesys_lock);
            return false;
        }
            
        lock_release(&filesys_lock);
        pml4_set_dirty(page->thread->pml4, page, false);
    }
    pml4_clear_page (page->thread->pml4, page->va);

    return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}
/* Do the mmap */
/* MMAP syscall for memory mapped file */
/* Every file-backed page should be mmapped */
/* To claim file-backed page, the mmap should be called */
void *
do_mmap (void *addr, size_t length, int writable,
        struct file *file, off_t offset) {
    /* Maps length bytes the file open as fd 
     * starting from offset byte into the process's virtual address space at addr. */

    struct thread *current = thread_current ();
	size_t f_length = file_length(file);
    void *curr = addr;

    while (f_length > 0)
    {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        struct file_info *f_info = (struct file_info *) calloc (1, sizeof (struct file_info));

        size_t page_length = f_length < PGSIZE ? f_length : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_length;

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        f_info->file = file;
        f_info->ofs = offset;
        f_info->read_bytes = page_length;
        f_info->zero_bytes = page_zero_bytes;

        /* Set mmap flag true */
        if (!vm_alloc_page_with_initializer(VM_FILE, curr,
                                            writable, NULL, f_info))
            return NULL;

        /* Advance. */
        f_length -= page_length;
        curr += PGSIZE;
        offset += page_length;
    }
    return addr;

}

/* Do the munmap */
void
do_munmap (void *addr) {

    struct supplemental_page_table *spt = &thread_current ()->spt;
    void *curr = addr;
    struct file *f = spt_find_page(spt, curr)->file.file;

    /* Travel all mmapped pages consecutively */
    for (curr = addr; (spt_find_page(spt, curr) != NULL); curr += PGSIZE) {
        struct page *p = spt_find_page(spt, curr);
        /* End of mmapped pages */
        if (!p->is_mmapped || p->file.file != f)
            return;
        
        if (pml4_is_dirty(thread_current ()->pml4, curr)) {
            // lock_acquire(&filesys_lock);
            if (file_write_at (p->file.file, curr, p->file.read_bytes, p->file.ofs) != p->file.read_bytes) {
                // lock_release(&filesys_lock);
                return;
            }
            // lock_release(&filesys_lock);

            pml4_set_dirty(thread_current ()->pml4, curr, false);
        }

        /* Turn off the flag */
        p->is_mmapped = false;
    }
}
