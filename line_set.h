/* inode_line_set.h - functions to manipulate a set of files with their hash sums */
#ifndef LINE_SET_H
#define LINE_SET_H

#include "calc_sums.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Entire hash-file line with its inode number (for fast search).
 */
typedef struct inode_line_set_item
{
	ino_t inode;
    time_t mtime;
	char* line;
	short path_offset;
	short path_len;
} line_set_item;

/* array to store filenames from a parsed hash file */
struct vector_t;
typedef struct vector_t inode_line_set;

#define line_set_new() rsh_vector_new((void(*)(void*))line_set_item_free) /* allocate new file set */
#define line_set_free(set) rsh_vector_free(set) /* free memory */
#define line_set_get(set, index) ((line_set_item*)((set)->array[index])) /* get i-th element */
#define line_set_add(set, item) rsh_vector_add_ptr(set, item) /* add a inode_line_set_item to inode_line_set */

void line_set_item_free(line_set_item *item);
void line_set_add_line(inode_line_set *set, const char *line, const short path_offset, const short path_len, const ino_t inode, const time_t mtime);
void line_set_sort(inode_line_set *set);
void inode_line_set_sort_by_path(inode_line_set *set);
line_set_item *line_set_item_new(const char *line, const ino_t inode, const time_t mtime, const short path_offset, const short path_len);
int line_set_exist(inode_line_set *set, const ino_t inode);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LINE_SET_H */
