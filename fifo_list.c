/*
 * FCRON - periodic command scheduler 
 *
 *  Copyright 2000-2025 Thibault Godouet <fcron@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *  The GNU General Public License can also be found in the file
 *  `LICENSE' that comes with the fcron source distribution.
 */


/*
 * 'First in first out' list of generic items
 */

#include "global.h"
#include "fcron.h"
#include "fifo_list.h"

/* private functions: */
int fifo_list_resize_array(fifo_list_t * l);
#define Sizeof_fifo_list(list) ((list)->entry_size * (list)->array_size)
fifo_list_entry_t *fifo_list_last(fifo_list_t * l);

fifo_list_t *
fifo_list_init(size_t entry_size, int init_size, int grow_size)
/* Create a new fifo list
 * Returns the newly created unordered list
 * Enough memory to hold init_size entries will initially be allocated,
 * and it will grow by grow_size entries when more space is needed.
 * Dies on error. */
{
    fifo_list_t *l = NULL;

    /* sanity check */
    if (entry_size < 1 || init_size < 1 || grow_size < 1)
        die("Invalid arguments for fifo_list_init(): entry_size=%d, init_size=%d, " "grow_size=%d", entry_size, init_size, grow_size);

    /* Allocate the list structure: */
    l = alloc_safe(sizeof(struct fifo_list_t), "new fifo_list_t");

    /* Initialize the structure and allocate the array: */
    l->max_entries = l->num_entries = 0;
    l->array_size = init_size;
    l->entry_size = entry_size;
    l->grow_size = grow_size;
    l->first_entry = l->cur_entry = NULL;
    l->entries_array =
        alloc_safe(init_size * entry_size, "new fifo_list_t array");

    return l;
}

fifo_list_entry_t *
fifo_list_last(fifo_list_t *l)
/* Returns the pointer of the last entry in the list, or NULL if l is empty */
{
    fifo_list_entry *e = NULL;

    if (l->num_entries <= 0)
        return NULL;

    e = (fifo_list_entry_t *)
        ((char *)l->entries_array + l->entry_size * (l->num_entries - 1));
    if (e >=
        (fifo_list_entry_t *) ((char *)l->entries_array + Sizeof_fifo_list(l)))
        e -= Sizeof_fifo_list(l);
}

int
fifo_list_resize_array(fifo_list_t *l)
/* Resize l's entries_array up to l->max_entries
 * Returns OK on success, ERR if the array is already at maximum size */
{
    fifo_list_entry_t *e = NULL;
    int offset = 0;
    int old_size = l->array_size;

    /* sanity check */
    if (l == NULL)
        die("Invalid argument for fifo_list_resize_array(): list=%d", l);
    if (l->max_entries > 0 && l->array_size >= l->max_entries) {
        debug
            ("Resizing fifo_list_t failed because it is already at max size (size: %d)",
             l->array_size);
        return ERR;
    }

    if (l->cur_entry != NULL)
        /* Compute cur_entry's offset so as we can set cur_entry to the right place
         * after we have allocated a new chunk of memory for the entries_array */
        offset = (char *)l->cur_entry - (char *)l->entries_array;

    l->array_size = (l->array_size + l->grow_size);
    if (l->max_entries > 0 && l->array_size > l->max_entries)
        l->array_size = l->max_entries;

    debug("Resizing fifo_list_t (old size: %d, new size: %d)...", old_size,
          l->array_size);

    e = alloc_safe(l->array_size * l->entry_size, "larger fifo_list_t array");
    memcpy(e, l->entries_array, (l->entry_size * old_size));
    Free_safe(l->entries_array);
    l->entries_array = e;

    if (l->cur_entry != NULL)
        l->cur_entry =
            (fifo_list_entry_t *) ((char *)l->entries_array + offset);

    return OK;
}


fifo_list_entry_t *
fifo_list_add(fifo_list_t *l, fifo_list_entry_t *e)
/* Add one entry to the list
 * Returns a pointer to the added element, or NULL if list is already at max size */
{
    fifo_list_entry_t *new = NULL;

    /* sanity check */
    if (l == NULL || e == NULL)
        die("Invalid arguments for fifo_list_add(): list=%d, entry=%d", l, e);

    /* Check there is some space left, or resize the array */
    if (l->num_entries >= l->array_size) {
        /* no more space: attempt to grow (the following function dies on error: */
        if (fifo_list_resize_array(l) != OK)
            return NULL;
    }

    l->num_entries++;
    new = fifo_list_last(l);
    memcpy(new, e, l->entry_size);

    return new;
}

fifo_list_entry_t *
fifo_list_first(fifo_list_t *l)
/* Return the first entry of the list (then fifo_list_next() can be used) */
{
    /* sanity check */
    if (l == NULL)
        die("Invalid argument for fifo_list_first(): list=%d", l);
    if (l->cur_entry != NULL)
        die("fifo_list_first() called but there is already an iteration");

    if (l->num_entries > 0) {
        l->cur_entry = l->entries_array;
    }

    return l->cur_entry;
}

fifo_list_entry_t *
fifo_list_next(fifo_list_t *l)
/* Return the entry after e */
{
    /* // WHAT IF I CALL _ADD() (+RESIZE?) OR _REMOVE() BETWEEN TWO _NEXT CALLS? */

    /* sanity checks */
    if (l == NULL)
        die("Invalid arguments for fifo_list_next(): list=%d", l);
    if (l->cur_entry == NULL)
        die("fifo_list_next() called outside an iteration: l->cur_entry=%d",
            l->cur_entry);

    if (l->cur_removed > 0) {
        l->cur_removed = 0;
        /* the current entry has just been removed and replaced by another one:
         * we can return the same pointer again.
         * However if the removed entry was the last one then we reached the end
         * of the list */
        if (l->cur_entry > fifo_list_last(l))
            l->cur_entry = NULL;
    }
    else {
        /* cur_entry *not* removed (standard behavior) */

        if (l->cur_entry < fifo_list_last(l))
            l->cur_entry =
                (fifo_list_entry_t *) ((char *)l->cur_entry + l->entry_size);
        else
            /* reached the end of the list */
            l->cur_entry = NULL;
    }

    return l->cur_entry;
}

void
fifo_list_end_iteration(fifo_list_t *list)
    /* Stop an iteration before _next() reached the end of the list by itself */
{
    list->cur_entry = NULL;
    list->cur_removed = 0;
}


void
fifo_list_remove_first(fifo_list_t *l)
{
    /* // MANAGE L->NEXT_ENTRY (+ SPECIAL CASE FIRST/LAST ENTRY) */
    fifo_list_entry_t *last = NULL;

    /* sanity checks */
    if (l == NULL)
        die("Invalid arguments for fifo_list_remove(): list=%d", l);
    if (l->cur_entry == NULL)
        die("fifo_list_remove_cur() called outside of an iteration");

    last = fifo_list_last(l);
    if (l->cur_entry < last) {
        /* Override e with the last entry */
        memcpy(l->cur_entry, last, l->entry_size);
    }
    /* erase the last entry and update the number of entries */
    memset(last, 0, l->entry_size);
    l->num_entries--;
    l->cur_removed = 1;

}

fifo_list_t *
fifo_list_destroy(fifo_list_t *list)
    /* free() the memory allocated for list and returns NULL */
{
    if (list == NULL)
        die("Invalid argument for fifo_list_destroy(): list=%d", list);

    Free_safe(list->entries_array);
    Free_safe(list);
    return NULL;
}
