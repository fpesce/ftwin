#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "j_heap.h"
#include "j_basic_list.h"
#include "j_file.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>


typedef struct file_specs_tag
{
    unsigned char *name;
    off_t size;
}             *file_specs_t;

void free_file_specs(file_specs_t fspec)
{
    free(fspec->name);
    free(fspec);
}

/*
 * The heap will be sorted from biggest to small files
 */
int cmp_file_specs(file_specs_t fspec1, file_specs_t fspec2)
{
    return fspec1->size - fspec2->size;
}

// S_IFLNK & stat.st_mode 
int file_select_nolink(const struct dirent *direntry)
{
    if (direntry->d_name[0] == '.') {
	switch (direntry->d_name[1]) {
	case '\0':
	    return 0;
	case '.':
	    return direntry->d_name[2];
	default:
	    return 1;
	}
    }
    return 1;
}

int get_file_specs(j_heap_t heap, const char *filename)
{
    struct dirent **namelist;
    char *name;
    struct stat filestat;
    size_t size_name, size_dir;
    int n;


    if (0 > (n = scandir(filename, &namelist, file_select_nolink, alphasort))) {
	fprintf(stderr, "%s: get_file_specs failed.\n", __FUNCTION__);
	return -1;
    }

    size_dir = strlen(filename);
    while (n--) {
	size_name = strlen(namelist[n]->d_name) + size_dir + 2;	//2 = . + /
	if (NULL == (name = malloc(size_name * sizeof(char)))) {
	    fprintf(stderr, "%s: malloc failed.\n", __FUNCTION__);
	    return -1;
	}
	strcpy(name, filename);
	name[size_dir] = '/';
	name[size_dir + 1] = '\0';
	strcat(name, namelist[n]->d_name);
	free(namelist[n]);
	if (-1 == lstat(name, &filestat)) {
	    fprintf(stderr, "%s: lstat failed on %s.\n", __FUNCTION__, name);
	    perror("");
	    free(name);
	    // freenamelist
	    return -1;
	}
	/* I don't follow link */
	if (!((S_IFMT & filestat.st_mode) == S_IFLNK)) {
	    if ((S_IFMT & filestat.st_mode) == S_IFDIR) {
		if (0 > get_file_specs(heap, name)) {
		    fprintf(stderr, "%s: recursive call to get_file_specs failed.\n", __FUNCTION__);
		    free(name);
		    // freenamelist
		    return -1;
		}
		free(name);
	    } else {
		/* Must be a non-null file */
		if (0 != filestat.st_size) {
		    file_specs_t fspec;

		    if (NULL == (fspec = (file_specs_t) malloc(sizeof(struct file_specs_tag)))) {
			fprintf(stderr, "%s: malloc failed.\n", __FUNCTION__);
			free(name);
			// freenamelist
			return -1;
		    }
		    fspec->name = name;
		    fspec->size = filestat.st_size;
		    if (0 > j_heap_insert(heap, fspec)) {
			fprintf(stderr, "%s: j_heap_insert failed.\n", __FUNCTION__);
			return -1;
		    }
		} else {
		    fprintf(stdout, "%s: size == 0\n", name);
		}
	    }
	} else {		/* It's a symlink */
	    free(name);
	}
    }
    free(namelist);

    return 1;
}

int create_file_heap(j_heap_t *heap, const char *filename)
{
    if (NULL == filename) {
	fprintf(stderr, "%s: filename is NULL.\n", __FUNCTION__);
	return -1;
    }

    if (NULL == (*heap = j_heap_init((void *) cmp_file_specs, (void *) free_file_specs))) {
	fprintf(stderr, "%s: j_heap_init failed.\n", __FUNCTION__);
	return -1;
    }

    if (0 > get_file_specs(*heap, filename)) {
	fprintf(stderr, "%s: get_file_specs failed.\n", __FUNCTION__);
	j_heap_destroy(*heap);
	return -1;
    }

    return 0;
}

int compare_file(file_specs_t fspec1, file_specs_t fspec2)
{
    j_file_t file1, file2;
    const char *content1, *content2;
    int n;

    if (0 > j_file_init(fspec1->name, &file1)) {
	fprintf(stderr, "%s: j_file_init file1 failed.\n", __FUNCTION__);
	return -1;
    }

    if (0 > j_file_init(fspec2->name, &file2)) {
	fprintf(stderr, "%s: j_file_init file1 failed.\n", __FUNCTION__);
	if (0 > j_file_free(file1)) {
	    fprintf(stderr, "%s: can't even free file 1.\n", __FUNCTION__);
	}
	return -1;
    }

    content1 = j_file_get_content(file1);
    content2 = j_file_get_content(file2);

    n = memcmp(content1, content2, j_file_get_size(file1));

    if (0 > j_file_free(file1)) {
	fprintf(stderr, "%s: can't free file 1.\n", __FUNCTION__);
	if (0 > j_file_free(file2)) {
	    fprintf(stderr, "%s: can't even free file 2.\n", __FUNCTION__);
	}
	return -1;
    }
    if (0 > j_file_free(file2)) {
	fprintf(stderr, "%s: can't free file 2.\n", __FUNCTION__);
	return -1;
    }

    return n;
}

int verify_double(j_heap_t heap)
{
    file_specs_t fspec;

    while (0 <= j_heap_extract(heap, (void **) &fspec)) {
	/* Now get ALL the file with the same size */
	j_basic_list_t list;
	off_t filesize;
	int i;

	if (0 > j_basic_list_init(&list)) {
	    fprintf(stderr, "%s: j_basic_list_init failed.\n", __FUNCTION__);
	    return -1;
	}

	if (0 > j_basic_list_cons(list, fspec)) {
	    fprintf(stderr, "%s: j_basic_list_cons failed.\n", __FUNCTION__);
	    // clean list
	    return -1;
	}
	filesize = fspec->size;

	/*
	 * we get the first element without extracting it
	 * If it's got the good size we extract it and
	 * compare it to all element in the list and then
	 * insert it into the list.
	 */
	for (i = j_heap_get_first(heap, (void **) &fspec); i >= 0 && fspec->size == filesize;
	     i = j_heap_get_first(heap, (void **) &fspec)) {
	    j_cell_t *cell;

	    if (0 > j_heap_extract(heap, (void **) &fspec)) {
		fprintf(stderr, "%s: j_heap_extract failed even if get_first was successfull.\n", __FUNCTION__);
		// Clean list etc..
		return -1;
	    }
	    /* Now compare to all previous element of same size */
	    for (cell = list->head; NULL != cell; cell = cell->next) {
		if (compare_file((file_specs_t) cell->data, fspec) == 0) {
		    fprintf(stdout, "SAME FILE : %lu bytes--------------------------------------\n%s\n%s.\n", filesize, fspec->name, ((file_specs_t) cell->data)->name);
		}
	    }
	    /* Now insert the file into the list */
	    if (0 > j_basic_list_cons(list, fspec)) {
		fprintf(stderr, "%s: j_basic_list_cons 2 failed.\n", __FUNCTION__);
		return -1;
	    }
	}			/* No more file of this size */

	j_basic_list_delete(list, (void *) free_file_specs);
    }

    return 0;
}

int main(int argc, char **argv)
{
    j_heap_t files;

    if (argc < 2) {
	fprintf(stderr, "Please, give an argument even if it is only . .\n");
	return -1;
    }

    if (0 > create_file_heap(&files, argv[1])) {
	fprintf(stderr, "create_file_hash failed.\n");
	return -1;
    }
    fprintf(stdout, "processed files:\n");
    j_heap_print_stats(files);

    if (0 > verify_double(files)) {
	fprintf(stderr, "create_file_hash failed.\n");
	j_heap_destroy(files);
	return -1;
    }

    j_heap_destroy(files);
    return 0;
}
