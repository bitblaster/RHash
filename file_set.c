/* file_set.c - functions to manipulate a set of files */
#include <assert.h>
#include <ctype.h>  /* isspace */
#include <stddef.h> /* ptrdiff_t */
#include <stdlib.h> /* qsort */
#include <string.h>
#include <sys/stat.h>

#include "file_set.h"
#include "common_func.h"
#include "hash_print.h"
#include "output.h"
#include "parse_cmdline.h"
#include "rhash_main.h"
#include "librhash/rhash.h"

/**
 * Generate a hash for a string.
 *
 * @param string the string to hash
 * @return a string hash
 */
static uint64_t file_set_make_hash(const char* string)
{
	unsigned hash;
    int res = -1;

	if (opt.flags & OPT_DETECT_CHANGES) {
	    file_t tmp_file;
	    file_init(&tmp_file, string, FILE_OPT_DONT_FREE_PATH);
        if(file_stat(&tmp_file, 0) == 0)
            res = tmp_file.stats->st_ino;
        else
            res = 0;

        file_cleanup(&tmp_file);

		return res;
	}

	if (opt.flags & OPT_IGNORE_CASE) {
		char* tmp_string = str_tolower(string);
		res = rhash_msg(RHASH_CRC32, tmp_string, strlen(tmp_string), (unsigned char *) &hash);
		free(tmp_string);
	}
	else {
		res = rhash_msg(RHASH_CRC32, string, strlen(string), (unsigned char *) &hash);
	}

	if(res < 0) {
		return 0;
	}
	return hash;
}

/**
 * Set file path of the given item.
 *
 * @param item pointer to the item to change
 * @param filepath the file path to set
 */
static int file_set_item_set_filepath(file_set_item* item, const char* filepath)
{
	free(item->filepath);
	item->filepath = rsh_strdup(filepath);
	if (!item->filepath) return 0;

	item->name_hash_or_inode = file_set_make_hash(item->filepath);
	return 1;
}

/**
 * Allocate a file_set_item structure and initialize it with a filepath.
 *
 * @param filepath a filepath to initialize the file_set_item
 * @return allocated file_set_item structure
 */
file_set_item* file_set_item_new(const char* filepath)
{
	file_set_item *item = (file_set_item*)rsh_malloc(sizeof(file_set_item));
	memset(item, 0, sizeof(file_set_item));

	if (filepath) {
		if (!file_set_item_set_filepath(item, filepath)) {
			free(item);
			return NULL;
		}
	}
	return item;
}

/**
 * Free memory allocated by file_set_item.
 *
 * @param item the item to delete
 */
void file_set_item_free(file_set_item *item)
{
	free(item->filepath);
	free(item);
}

/**
 * Call-back function to compare two file items by search_filepath, using hashes
 *
 * @param pp_rec1 the first item to compare
 * @param pp_rec2 the second item to compare
 * @return 0 if items are equal, -1 if pp_rec1 &lt; pp_rec2, 1 otherwise
 */
static int crc_pp_rec_compare(const void *pp_rec1, const void *pp_rec2)
{
	const file_set_item *rec1 = *(file_set_item *const *)pp_rec1;
	const file_set_item *rec2 = *(file_set_item *const *)pp_rec2;
	if (rec1->name_hash_or_inode != rec2->name_hash_or_inode)
		return (rec1->name_hash_or_inode < rec2->name_hash_or_inode ? -1 : 1);

	if (opt.flags & OPT_IGNORE_CASE)
		return strcmpci(rec1->filepath, rec2->filepath);
	else
		return strcmp(rec1->filepath, rec2->filepath);
}

/**
 * Compare two file items by filepath.
 *
 * @param rec1 pointer to the first file_set_item structure
 * @param rec2 pointer to the second file_set_item structure
 * @return 0 if files have the same filepath, and -1 or 1 (strcmp result) if not
 */
static int path_compare(const void *rec1, const void *rec2)
{
	return strcmp((*(file_set_item *const *)rec1)->filepath,
		(*(file_set_item *const *)rec2)->filepath);
}

/**
 * Sort given file_set using hashes of search_filepath for fast binary search.
 *
 * @param set the file_set to sort
 */
void file_set_sort(file_set *set)
{
	if (set->array) qsort(set->array, set->size, sizeof(file_set_item*), crc_pp_rec_compare);
}

/**
 * Sort files in the specified file_set by file path.
 *
 * @param set the file-set to sort
 */
void file_set_sort_by_path(file_set *set)
{
	qsort(set->array, set->size, sizeof(file_set_item*), path_compare);
}

/**
 * Create and add a file_set_item with given filepath to given file_set
 *
 * @param set the file_set to add the item to
 * @param filepath the item file path
 */
void file_set_add_name(file_set *set, const char* filepath)
{
	file_set_item* item = file_set_item_new(filepath);
	if (item) file_set_add(set, item);
}

/**
 * Find a file path in the file_set.
 *
 * @param set the file_set to search
 * @param filepath the file path to search for
 * @return 1 if filepath is found, 0 otherwise
 */
int file_set_exist(file_set *set, const char* filepath)
{
	int a, b, c;
	int cmp, res = 0;
	uint64_t hash;

	if (!set->size) return 0; /* not found */
	assert(set->array != NULL);

	/* generate hash to speedup the search */
	hash = file_set_make_hash(filepath);

	/* fast binary search */
	for (a = -1, b = (int)set->size; (a + 1) < b;) {
		file_set_item *item;

		c = (a + b) / 2;
		assert(0 <= c && c < (int)set->size);

		item = (file_set_item*)set->array[c];
		if (hash != item->name_hash_or_inode) {
			cmp = (hash < item->name_hash_or_inode ? -1 : 1);
		} else {
			if (opt.flags & OPT_IGNORE_CASE)
				cmp = strcmpci(filepath, item->filepath);
			else
				cmp = strcmp(filepath, item->filepath);
			if (cmp == 0) {
				res = 1; /* file path has been found */
				break;
			}
		}
		if (cmp < 0) b = c;
		else a = c;
	}
	return res;
}
