/* line_set.c - functions to manipulate a set of files */
#include <assert.h>
#include <ctype.h>  /* isspace */
#include <stddef.h> /* ptrdiff_t */
#include <stdlib.h> /* qsort */
#include <string.h>

#include "line_set.h"
#include "common_func.h"
#include "hash_print.h"
#include "output.h"
#include "parse_cmdline.h"
#include "rhash_main.h"
#include "librhash/rhash.h"

/**
 * Set line and inode of the given item.
 *
 * @param item pointer to the item to change
 * @param line the line to set
 * @param inode the inode to set
 */
static int line_set_item_set_line(line_set_item* item, const char* line)
{
	free(item->line);
	item->line = rsh_strdup(line);
	if (!item->line) return 0;

	return 1;
}

/**
 * Allocate a inode_line_set_item structure and initialize it with a line and an inode.
 *
 * @param line a line to initialize the inode_line_set_item
 * @param inode an inode to initialize the inode_line_set_item
 * @param path_offset offset of the file path inside the line string
 * @param path_len length of the file path inside the line string
 * @return allocated inode_line_set_item structure
 */
line_set_item *line_set_item_new(const char *line, const ino_t inode, const time_t mtime, const short path_offset, const short path_len)
{
	line_set_item *item = (line_set_item*)rsh_malloc(sizeof(line_set_item));
	memset(item, 0, sizeof(line_set_item));

	if (line) {
		if (!line_set_item_set_line(item, line)) {
			free(item);
			return NULL;
		}
		item->inode = inode;
		item->mtime = mtime;
        item->path_offset = path_offset;
        item->path_len = path_len;
	}
	return item;
}

/**
 * Free memory allocated by inode_line_set_item.
 *
 * @param item the item to delete
 */
void line_set_item_free(line_set_item *item)
{
	free(item->line);
	free(item);
}

/**
 * Call-back function to compare two items by inode
 *
 * @param pp_rec1 the first item to compare
 * @param pp_rec2 the second item to compare
 * @return 0 if items are equal, -1 if pp_rec1 &lt; pp_rec2, 1 otherwise
 */
static int inode_pp_rec_compare(const void *pp_rec1, const void *pp_rec2)
{
	const line_set_item *rec1 = *(line_set_item *const *)pp_rec1;
	const line_set_item *rec2 = *(line_set_item *const *)pp_rec2;
	if (rec1->inode != rec2->inode)
	    return (rec1->inode < rec2->inode ? -1 : 1);
	return 0;
}

/**
 * Sort given inode_line_set using hashes of search_filepath for fast binary search.
 *
 * @param set the inode_line_set to sort
 */
void line_set_sort(inode_line_set *set)
{
	if (set->array) qsort(set->array, set->size, sizeof(line_set_item*), inode_pp_rec_compare);
}

/**
 * Create and add a inode_line_set_item with given filepath to given inode_line_set
 *
 * @param set the inode_line_set to add the item to
 * @param line a line to initialize the inode_line_set_item
 * @param inode an inode to initialize the inode_line_set_item
 * @param path_offset offset of the file path inside the line string
 * @param path_len length of the file path inside the line string
 */
void line_set_add_line(inode_line_set *set, const char *line, const short path_offset, const short path_len, const ino_t inode, const time_t mtime)
{
	line_set_item* item = line_set_item_new(line, inode, mtime, path_offset, path_len);
	if (item) line_set_add(set, item);
}

/**
 * Find a file path in the inode_line_set.
 *
 * @param set the inode_line_set to search
 * @param filepath the file path to search for
 * @return file index if filepath is found, -1 otherwise
 */
int line_set_exist(inode_line_set *set, const ino_t inode)
{
	int a, b, c;
	int cmp, res = -1;

	if (!set->size) return -1; /* not found */
	assert(set->array != NULL);

	/* fast binary search */
	for (a = -1, b = (int)set->size; (a + 1) < b;) {
		line_set_item *item;

		c = (a + b) / 2;
		assert(0 <= c && c < (int)set->size);

		item = (line_set_item*)set->array[c];
		if (inode != item->inode) {
			cmp = (inode < item->inode ? -1 : 1);
		} else {
            res = c; /* file path has been found */
            break;
		}
		if (cmp < 0) b = c;
		else a = c;
	}

	return res;
}
