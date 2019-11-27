/* hash_update.c - functions to update a crc file */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
# include <dirent.h>
#endif

#include "calc_sums.h"
#include "common_func.h"
#include "file.h"
#include "file_set.h"
#include "file_mask.h"
#include "hash_print.h"
#include "hash_update.h"
#include "output.h"
#include "parse_cmdline.h"
#include "rhash_main.h"
#include "win_utils.h"
#include "line_set.h"
#include "find_file.h"
#include <sys/stat.h>

/* first define some internal functions, implemented later in this file */
static int add_new_crc_entries(file_t* file, file_set *crc_entries, inode_line_set* removed_entries);
static int file_set_load_from_crc_file(file_set *set, inode_line_set* removed_entries, file_t* file);
static int fix_sfv_header(file_t* file);

typedef struct update_call_back_ctx
{
	file_set *crc_entries;
	file_set* files_to_add;
	inode_line_set *removed_entries;
} update_call_back_ctx;

/**
 * Update given crc file, by adding to it hashes of files from the same
 * directory, but which the crc file doesn't contain yet.
 *
 * @param file the file containing hash sums
 * @return 0 on success, -1 on fail
 */
int update_hash_file(file_t* file)
{
	file_set* crc_entries;
	inode_line_set* removed_entries;
	timedelta_t timer;
	int res;

	if (opt.flags & OPT_VERBOSE) {
		log_msg(_("Updating: %s\n"), file->path);
	}

	crc_entries = file_set_new();
	removed_entries = line_set_new();
	res = file_set_load_from_crc_file(crc_entries, removed_entries, file);

	if (opt.flags & OPT_SPEED) rsh_timer_start(&timer);
	rhash_data.total_size = 0;
	rhash_data.processed  = 0;

	if (res == 0) {
		/* add the crc file itself to the set of excluded from re-calculation files */
		file_set_add_name(crc_entries, get_basename(file->path));
		file_set_sort(crc_entries);
		line_set_sort(removed_entries);

		/* update crc file with sums of files not present in the crc_entries */
		res = add_new_crc_entries(file, crc_entries, removed_entries);
	}
	file_set_free(crc_entries);
	line_set_free(removed_entries);

	if (opt.flags & OPT_SPEED && rhash_data.processed > 0) {
		double time = rsh_timer_stop(&timer);
		print_time_stats(time, rhash_data.total_size, 1);
	}

	return res;
}

/**
 * Load a set of files from given crc file.
 *
 * @param set the file set to store loaded files
 * @param file the file containing hash sums to load
 * @return 0 on success, -1 on fail with error code in errno
 */
static int file_set_load_from_crc_file(file_set *set, inode_line_set* removed_entries, file_t* file)
{
	FILE *in;
	FILE* out;
	int line_num;
	char buf[2048];
	char orig_line[2048];
	hash_check hc;
	file_t new_file;
	int err = 0;

	if ( !(in = file_fopen(file, FOpenRead | FOpenBin) )) {
		/* if file not exist, it will be created */
		return (errno == ENOENT ? 0 : -1);
	}

	/* open a temporary file for writing */
	file_path_append(&new_file, file, ".new");
	if ( !(out = file_fopen(&new_file, FOpenWrite) )) {
		log_file_t_error(&new_file);
		file_cleanup(&new_file);
		fclose(in);
		return -1;
	}

	if (opt.fmt == FMT_SFV)
		print_sfv_banner(out);

	for (line_num = 0; fgets(buf, 2048, in); line_num++) {
		char append = 1;
		char* line = buf;
		strcpy(orig_line, line);

		/* skip unicode BOM */
		if (line_num == 0 && buf[0] == (char)0xEF && buf[1] == (char)0xBB && buf[2] == (char)0xBF) line += 3;

		if (*line == 0) continue; /* skip empty lines */

		if (is_binary_string(line)) {
			log_error(_("skipping binary file %s\n"), file->path);
			err = 1;
			break;
		}

		if (IS_COMMENT(*line) || *line == '\r' || *line == '\n')
			continue;

		/* parse a hash file line */
		if (hash_check_parse_line(line, &hc, !feof(in))) {
			/* store file info to the file set */
			if (hc.file_path) {
				if ((opt.flags & OPT_REMOVE_MISSING) || (opt.flags & OPT_DETECT_CHANGES)) {
					struct stat stats;
					int res = stat(hc.file_path, &stats);
					if (res != 0) { // file is missing
						append = 0;
						if ((opt.flags & OPT_DETECT_CHANGES) && hc.inode && hc.mtime) {
							char *path_offset = strstr(orig_line, hc.file_path);
							line_set_add_line(removed_entries, orig_line, path_offset - orig_line, strlen(hc.file_path),
											  hc.inode, hc.mtime);
						}
					} else if ((opt.flags & OPT_DETECT_CHANGES) && (hc.inode != stats.st_ino || hc.mtime != stats.st_mtim.tv_sec)) {
						append = 0;
					}
				}

				if (append) {
					file_set_add_name(set, hc.file_path);
					if (opt.fmt == FMT_SFV) {
						file_t tmp_file;
						file_init(&tmp_file, hc.file_path, FILE_OPT_DONT_FREE_PATH);
						if (file_stat(&tmp_file, 0) < 0) {
							err = 1;
							file_cleanup(&tmp_file);
							break;
						}
						print_sfv_header_line(out, &tmp_file, 0);
						file_cleanup(&tmp_file);
					}
				}
			} else if (opt.flags & OPT_DETECT_CHANGES) {
				append = 0;
			}
		} else if (opt.flags & OPT_DETECT_CHANGES) {
			append = 0;
		}

		if (append && fputs(orig_line, out) < 0)
			break;
	}

	if (ferror(in)) {
		log_file_t_error(file);
		err = 1;
	}
	if (ferror(out)) {
		log_file_t_error(&new_file);
		err = 1;
	}
	fclose(in);
	fclose(out);

	/* overwrite the hash file with a new one */
	if (!err && file_rename(&new_file, file) < 0) {
		log_error(_("can't move %s to %s: %s\n"),
				  new_file.path, file->path, strerror(errno));
	}
	file_cleanup(&new_file);
	return (err ? -1 : 0);
}

/**
 * Add hash sums of files from given file-set to a specified hash-file.
 * A specified directory path will be prepended to the path of added files,
 * if it is not a current directory.
 *
 * @param file the hash file to add the hash sums to
 * @param dir_path the directory path to prepend
 * @param files_to_add the set of files to hash and add
 * @return 0 on success, -1 on error
 */
static int add_sums_to_file(file_t* file, char* dir_path, file_set *files_to_add, inode_line_set* removed_entries)
{
	FILE* fd;
	unsigned i;
	int ch;
	char new_line[2048];

	/* SFV banner will be printed only in SFV mode and only for empty crc files */
	int print_banner = (opt.fmt == FMT_SFV);

	file->size = 0;
	if (file_stat(file, 0) == 0) {
		if (print_banner && file->size > 0) print_banner = 0;
	}

	/* open the hash file for writing */
	if ( !(fd = file_fopen(file, FOpenRead | FOpenWrite) )) {
		log_file_t_error(file);
		return -1;
	}
	rhash_data.upd_fd = fd;

	if (file->size > 0) {
		/* read the last character of the file to check if it is EOL */
		if (fseek(fd, -1, SEEK_END) != 0) {
			log_file_t_error(file);
			return -1;
		}
		ch = fgetc(fd);

		/* somehow writing doesn't work without seeking */
		if (fseek(fd, 0, SEEK_END) != 0) {
			log_file_t_error(file);
			return -1;
		}

		/* write EOL if it wasn't present */
		if (ch != '\n' && ch != '\r') {
			/* fputc('\n', fd); */
			rsh_fprintf(fd, "\n");
		}
	}

	/* append hash sums to the updated crc file */
	for (i = 0; i < files_to_add->size; i++, rhash_data.processed++) {
		file_t tmp_file;
		char *print_path = file_set_get(files_to_add, i)->filepath;
		int removed_index = -1;
		memset(&tmp_file, 0, sizeof(tmp_file));

		if (dir_path[0] != '.' || dir_path[1] != 0) {
			/* prepend the file path by directory path */
			file_init(&tmp_file, make_path(dir_path, print_path), 0);
		} else {
			file_init(&tmp_file, print_path, FILE_OPT_DONT_FREE_PATH);
		}

		if (opt.fmt == FMT_SFV) {
			if (print_banner) {
				print_sfv_banner(fd);
				print_banner = 0;
			}
		}
		file_stat(&tmp_file, 0);

		removed_index = line_set_exist(removed_entries, tmp_file.stats->st_ino);
		if (removed_index >= 0) {
			// the file has been moved, so reuse the same hashes of the input file for that inode
			line_set_item *removed_item = line_set_get(removed_entries, removed_index);
			if (removed_item->mtime == tmp_file.stats->st_mtim.tv_sec) { // check that the inode has not been reused
				// replace the original name with the new,  keeping the rest of the original line
				strncpy(new_line, removed_item->line, removed_item->path_offset);
				strcpy(new_line + removed_item->path_offset, tmp_file.path);
				strcat(new_line, removed_item->line + removed_item->path_offset + removed_item->path_len);
				fputs(new_line, fd);
			}
			else {
				/* print hash sums to the crc file */
				calculate_and_print_sums(fd, &tmp_file, print_path);
			}
		}
		else {
			/* print hash sums to the crc file */
			calculate_and_print_sums(fd, &tmp_file, print_path);
		}

		file_cleanup(&tmp_file);

		if (rhash_data.interrupted) {
			fclose(fd);
			return 0;
		}
	}
	fclose(fd);
	log_msg(_("Updated: %s\n"), file->path);
	return 0;
}

/**
 * Check if the file must be skipped. Returns 1 if the file path
 * is the same as the output or the log file path.
 *
 * @param file the file to check
 * @param mask the mask of accepted files
 * @return 1 if the file should be skipped, 0 otherwise
 */
static int must_skip_file(file_t* file)
{
	const rsh_tchar* path = FILE_TPATH(file);

	/* check if the file path is the same as the output or the log file path */
	return (opt.output && are_paths_equal(path, opt.output)) ||
		   (opt.log && are_paths_equal(path, opt.log));
}
/**
 * Callback function to process new files while recursively traversing a directory.
 * It adds new files to the file_set of the caller.
 *
 * @param file the file to process
 * @param call_back_data context of the call, containing the entries already present in the
 *        current hash file and the set to be updated with the new files
 */
static int update_file_callback(file_t* file, call_back_ctx call_back_data) {
	update_call_back_ctx* ctx = call_back_data.pval;

	if (FILE_ISDATA(file) || !file_mask_match(opt.files_accept, file->path) ||
		(opt.files_exclude && file_mask_match(opt.files_exclude, file->path)) ||
		must_skip_file(file)) {
		return 0;
	}

	if(!file_set_exist(ctx->crc_entries, file->path))
		file_set_add_name(ctx->files_to_add, file->path);

	return 0;
}

/**
 * Calculate and add to the given hash-file the hash-sums for all files
 * from the same directory as the hash-file, but absent from given
 * crc_entries file-set.
 *
 * <p/>If SFV format was specified by a command line switch, the after adding
 * hash sums SFV header of the file is fixed by moving all lines starting
 * with a semicolon before other lines. So an SFV-formatted hash-file
 * will remain correct.
 *
 * @param file the hash-file to add sums into
 * @param crc_entries file-set of files to omit from adding
 * @param removed_entries inode-line-set of files not found on the filesystem
 * @return 0 on success, -1 on error
 */
static int add_new_crc_entries(file_t* file, file_set *crc_entries, inode_line_set* removed_entries)
{
	char* dir_path;
	int res = 0;
	struct update_call_back_ctx ctx;
	struct file_search_data search_data;
	file_t dir;

	ctx.files_to_add = file_set_new();
	ctx.crc_entries = crc_entries;
	ctx.removed_entries = removed_entries;

	search_data.max_depth = opt.search_data->max_depth ? opt.search_data->max_depth : 1;
	search_data.options = opt.search_data->options;
	search_data.call_back = update_file_callback;
	search_data.call_back_data.pval = &ctx;

	dir_path = get_dirname(file->path);
	file_init(&dir, dir_path, FILE_IFDIR | FILE_OPT_DONT_FREE_PATH);

	dir_scan(&dir, &search_data);

	if (ctx.files_to_add->size > 0) {
		/* sort files by path */
		file_set_sort_by_path(ctx.files_to_add);

		/* calculate and write crc sums to the file */
		res = add_sums_to_file(file, dir_path, ctx.files_to_add, removed_entries);
	}

	if (res == 0 && opt.fmt == FMT_SFV && !rhash_data.interrupted) {
		/* move SFV header from the end of updated file to its head */
		res = fix_sfv_header(file);
	}

	file_set_free(ctx.files_to_add);
	free(dir_path);
	return res;
}

/**
 * Move all SFV header lines (i.e. all lines starting with a semicolon)
 * from the end of updated file to its head.
 *
 * @param file the hash file requiring fixing of its SFV header
 */
static int fix_sfv_header(file_t* file)
{
	FILE* in;
	FILE* out;
	char line[2048];
	file_t new_file;
	int err = 0;

	if ( !(in = file_fopen(file, FOpenRead))) {
		log_file_t_error(file);
		return -1;
	}

	/* open a temporary file for writing */
	file_path_append(&new_file, file, ".new");
	if ( !(out = file_fopen(&new_file, FOpenWrite) )) {
		log_file_t_error(&new_file);
		file_cleanup(&new_file);
		fclose(in);
		return -1;
	}

	/* The first, output all commented lines to the file header */
	while (fgets(line, 2048, in)) {
		if (*line == ';') {
			if (fputs(line, out) < 0) break;
		}
	}
	if (!ferror(out) && !ferror(in)) {
		fseek(in, 0, SEEK_SET);
		/* The second, output non-commented lines */
		while (fgets(line, 2048, in)) {
			if (*line != ';') {
				if (fputs(line, out) < 0) break;
			}
		}
	}
	if (ferror(in)) {
		log_file_t_error(file);
		err = 1;
	}
	if (ferror(out)) {
		log_file_t_error(&new_file);
		err = 1;
	}

	fclose(in);
	fclose(out);

	/* overwrite the hash file with a new one */
	if (!err && file_rename(&new_file, file) < 0) {
		log_error(_("can't move %s to %s: %s\n"),
				  new_file.path, file->path, strerror(errno));
	}
	file_cleanup(&new_file);
	return (err ? -1 : 0);
}
