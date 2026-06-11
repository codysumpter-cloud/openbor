/*
	Copyright 2006 Luigi Auriemma
	Copyright 2009-2011 Bryan Cain

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

	http://www.gnu.org/licenses/gpl.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>


#ifdef WIN32
	#include <direct.h>
	#include <windows.h>
	#include "stristr.h"
	#include "scandir.h"

	#define MKDIR(x)    mkdir(x)
	#define PATHSLASH   '\\'
#else
	#include <unistd.h>

	#define stristr		strcasestr
	#define MKDIR(x)    mkdir(x, 0755)
	#define PATHSLASH   '/'
#endif

#define FNAMEMAX        1024
#define PAK_UINT_MAX 	UINT32_MAX

typedef uint32_t pak_u32;

/*
* 2026-06-10  Damon V. Caskey
* 
* Not used, for future large pak file
* support.
*/
typedef uint64_t pak_offset_t;
typedef uint64_t pak_size_t;

void file_write(const void *buffer, size_t size, FILE *file_object);
int pack_file(FILE *pak_file_object, char *file_path);
void extract_file(FILE *pak_file_object, char *file_path, pak_u32 data_offset, pak_u32 data_size);
pak_u32 read_le_uint(FILE *pak_file_object, int bits);
void write_le_uint(FILE *pak_file_object, pak_u32 num, int bits);
int pack_directory(FILE *pak_file_object, char *directory_path);
void write_err(void);
void std_err(void);
void winpause(void);
pak_u32 pak_tell32(FILE *pak_file_object);
int is_unsafe_path(const char *name);

typedef struct {
	pak_u32   record_size;
	pak_u32   data_offset;
	pak_u32   data_size;
	char    *name;
} pak_entry_t;

struct pak_entry_node_t {
	pak_entry_t     		entry;
	struct pak_entry_node_t	*next;
};

struct  pak_entry_node_t   *pak_entries = NULL;

/*
* Caskey, Damon V.
* 2026-06-10
*
* Writes a fixed-size block to a file.
* Exits through write_err() if the write fails.
*/
void file_write(const void *buffer, size_t size, FILE *file_object) {
	if(fwrite(buffer, size, 1, file_object) != 1) {
		write_err();
	}
}

/*
* Caskey, Damon V.
* 2026-06-10
*
* Returns the current file position as a 32-bit 
* archive offset. The PAK format stores offsets 
* in 32-bit little-endian fields.
*/
pak_u32 pak_tell32(FILE *pak_file_object) {
	long pos;

	pos = ftell(pak_file_object);

	if(pos < 0) {
		std_err();
	}

	if((unsigned long)pos > PAK_UINT_MAX) {
		std_err();
	}

	return (pak_u32)pos;
}

/*
* Caskey, Damon V.
* 2026-06-10
*
* Rejects archive names that would escape the 
* extraction directory. PAK entries should always 
* be relative paths.
*/
int is_unsafe_path(const char *name) {

	if(name[0] == '/' || name[0] == '\\') {
		return 1;
	}

	if(strstr(name, "../") || strstr(name, "..\\")) {
		return 1;
	}

#ifdef WIN32
	if(strchr(name, ':')) {
		return 1;
	}
#endif

	return 0;
}

int main(int argc, char *argv[]) {

	struct pak_entry_node_t *entry_node;
	struct pak_entry_node_t *entry_node_free;
	
	pak_entry_t    entry;
	FILE    *pak_file_object;
	pak_u32   table_offset;
	pak_u32   table_end_offset;
	pak_u32 table_start_offset;
	int     i;
	size_t	name_length;
	int		list    = 0;
	int		build   = 0;
	pak_u32	packver = 0;
	int		file_count   = 0;
	int 	fgetc_result = 0;

	char	pack[4];
	char	curdir[512];
	char	*input_dir    = NULL;
	char	*pattern      = NULL;
	char	*pak_filename;

	setbuf(stdout, NULL);

#ifdef WIN32
	atexit(winpause);
#endif

	fputs("\n"
		"BOR PAK extractor/builder v0.1\n"
		"originally by Luigi Auriemma\n"
		"e-mail: aluigi@autistici.org\n"
		"web:    aluigi.org\n"
		"\n"
		"Updated to v0.2 by SX\n"
		"e-mail: sumolx@gmail.com\n"
		"web:    https://www.chronocrash.com\n"
		"\n"
		"Updated to v0.3 by Plombo\n"
		"web:    https://www.chronocrash.com\n"
		"\n", stdout);

	if(argc < 2) {
		printf("\n"
			"Usage: %s [options] <file.PAK>\n"
			"\n"
			"-d DIR   files folder, default is the current\n"
			"-b       build a PAK from the files folder, default is extraction\n"
			"-l       list files without extracting\n"
			"-p PAT   extract only the files which contain PAT in their name\n"
			"\n", argv[0]);
		exit(1);
	}

	argc--;
	for(i = 1; i < argc; i++) {
		
		if(argv[i][0] != '-') {
			 break;
		}
		
		switch(argv[i][1]) {
			case 'd': 
			
				if(i + 1 >= argc) {
					printf("\nError: missing argument for -d option\n\n");
					exit(1);
				}
			
				input_dir = argv[++i];    break;
			
			case 'b': 
			
				build = 1;            break;
			case 'l': 
			
				list  = 1;            break;
			case 'p': 
			
				if(i + 1 >= argc) {
					printf("\nError: missing argument for -p option\n\n");
					exit(1);
				}

				pattern  = argv[++i];    break;
			default: {
				printf("\nError: wrong command-line argument (%s)\n\n", argv[i]);
				exit(1);
				} break;
		}
	}
	pak_filename = argv[argc];

	if(build) {
		if(!input_dir) {
			printf("\n"
				"Error: please specify the files directory with the -d option and don't specify\n"
				"       the current\n\n");
			exit(1);
		}

		printf("- create file: %s\n", pak_filename);
		printf("- directory: %s\n\n", input_dir);
		pak_file_object = fopen(pak_filename, "rb");
		if(pak_file_object) {
			fclose(pak_file_object);
			printf("- a file with the same name already exists, overwrite? (y/N)\n  ");
			
			fgetc_result = fgetc(stdin);
			if(fgetc_result == EOF || tolower((unsigned char)fgetc_result) != 'y') {
				exit(1);
			}
		}
		pak_file_object = fopen(pak_filename, "wb");

		if(!pak_file_object){
			std_err();
		}

		file_write("PACK", 4, pak_file_object);
		write_le_uint(pak_file_object, packver, 32);

		printf(
			"    offset       size   filename\n"
			"--------------------------------\n");

		if(pack_directory(pak_file_object, input_dir) < 0) {
			printf("\nError: an error occurred during the directory scanning\n");
			goto quit;
		}

		// fflush(pak_file_object);
		table_offset = pak_tell32(pak_file_object);
		printf("- files info offset: %08x\n", (unsigned int)table_offset);

		entry_node = pak_entries;
		while(entry_node) {
			write_le_uint(pak_file_object, entry_node->entry.record_size,  32);
			write_le_uint(pak_file_object, entry_node->entry.data_offset,  32);
			write_le_uint(pak_file_object, entry_node->entry.data_size, 32);
			file_write(entry_node->entry.name, entry_node->entry.record_size - 12, pak_file_object);

			entry_node_free = entry_node;
			entry_node     = entry_node->next;
			free(entry_node_free->entry.name);
			free(entry_node_free);
			file_count++;
		}
		write_le_uint(pak_file_object, table_offset,  32);

	} else {

		printf("- open file: %s\n", pak_filename);
		pak_file_object = fopen(pak_filename, "rb");

		if(!pak_file_object){
			std_err();
		}

		if(input_dir) {
			printf("- change directory: %s\n", input_dir);
			
			if(chdir(input_dir) < 0) {
				std_err();
			}
		}

		if(!fread(pack, 4, 1, pak_file_object)){
			goto quit;
		}

		if(memcmp(pack, "PACK", 4)){
			goto quit;
		}

		packver = read_le_uint(pak_file_object, 32);

		if(fseek(pak_file_object, -4, SEEK_END) < 0) {
			std_err();
		}

		/*
		* The final 4 bytes store the offset of the file table.
		* The table itself ends immediately before that footer.
		*/
		table_end_offset = pak_tell32(pak_file_object);
		table_offset = read_le_uint(pak_file_object, 32);

		if(table_offset < 8 || table_offset > table_end_offset) {
			printf("\nError: malformed PAK file table offset\n");
			goto quit;
		}

		table_start_offset = table_offset;

		if(fseek(pak_file_object, table_offset, SEEK_SET) < 0) { 
			std_err();
		}

		while(table_offset < table_end_offset) {
			entry.record_size = read_le_uint(pak_file_object, 32);
			entry.data_offset = read_le_uint(pak_file_object, 32);
			entry.data_size   = read_le_uint(pak_file_object, 32);

			if(entry.record_size <= 12) {
				printf("\nError: malformed PAK file table entry\n");
				goto quit;
			}

			if(entry.record_size > table_end_offset - table_offset) {
				printf("\nError: malformed PAK file table\n");
				goto quit;
			}

			name_length = entry.record_size - 12;

			entry.name = malloc(name_length + 1);
			
			if(!entry.name) { 
				std_err();
			}
			

			if(fread(entry.name, name_length, 1, pak_file_object) != 1) {
				free(entry.name);
				std_err();
			}

			/*
			*  Archive names are stored as raw bytes. Add the terminator
			*  before using the name with normal C string functions.
			*/
			entry.name[name_length] = '\0';

			if(!pattern || (pattern && stristr(entry.name, pattern))) {
				printf("  %s\n", entry.name);
				if(!list) {

					/*
					* Verify safe extraction before writing any files. 
					* This prevents malicious or malformed archive entries 
					* from escaping the extraction directory.
					*/
					if(is_unsafe_path(entry.name)) {
						printf("  skipping %s: path traversal detected\n", entry.name);
					} else {

						if(entry.data_offset > table_start_offset ||
							entry.data_size > table_start_offset - entry.data_offset) {
							printf("\nError: malformed PAK file data range: %s\n", entry.name);
							free(entry.name);
							goto quit;
						}

						extract_file(pak_file_object, entry.name, entry.data_offset, entry.data_size);
					}
				}
				file_count++;
			}

			free(entry.name);
			table_offset += entry.record_size;
			if(fseek(pak_file_object, table_offset, SEEK_SET) < 0){ 
				std_err();
			}
		}
	}

quit:
	fclose(pak_file_object);
	printf("- finished: %d files\n", file_count);
	printf("- current directory: %s\n", getcwd(curdir, sizeof(curdir)));
	
	return 0;
}

int pack_file(FILE *pak_file_object, char *file_path) {

	struct  pak_entry_node_t   *entry_node;
	FILE    *fdi;
	size_t  bytes_read;
	size_t  name_length;
	pak_u32 data_offset;
	size_t	data_size;

	unsigned char 	buff[8192];
	char	*path;

	// printf("  %s\r", file_path);
	fdi = fopen(file_path, "rb");
	if(!fdi){ 
		std_err();
	}

	/*
	* Skip leading "./" if present.
	*/
	if(file_path[0] == '.' && (file_path[1] == '/' || file_path[1] == '\\')) {
		file_path += 2;
	}

	for(path = file_path; *path; path++) {           // WIN mode
		if(*path == '/') {
			*path = '\\';
		} /*else {
			*path = toupper(*path);
		} */
	}

	// fflush(pak_file_object);
	data_offset = pak_tell32(pak_file_object);

	data_size = 0;

	while((bytes_read = fread(buff, 1, sizeof(buff), fdi)) != 0) {
		if(data_size > PAK_UINT_MAX - bytes_read) {
			printf("\nError: file too large for current PAK format: %s\n", file_path);
			exit(1);
		}

		file_write(buff, bytes_read, pak_file_object);
		data_size += bytes_read;
	}

	if(ferror(fdi)) {
		std_err();
	}

	fclose(fdi);

	for(entry_node = pak_entries; entry_node && entry_node->next; entry_node = entry_node->next);

	if(entry_node) {
		entry_node->next = malloc(sizeof(struct pak_entry_node_t));
		
		if(!entry_node->next) { 
			std_err();
		}
		entry_node       = entry_node->next;

	} else {
		pak_entries      = malloc(sizeof(struct pak_entry_node_t));
		
		if(!pak_entries) { 
			std_err();
		}
		entry_node       = pak_entries;
	}
	entry_node->next = NULL;

	name_length = strlen(file_path) + 1;

	if(name_length > PAK_UINT_MAX - 12) {
		printf("\nError: file name too long for current PAK format: %s\n", file_path);
		exit(1);
	}

	entry_node->entry.record_size = (pak_u32)(12 + name_length);
	entry_node->entry.data_offset  = data_offset;
	entry_node->entry.data_size = (pak_u32)data_size;
	entry_node->entry.name = malloc(name_length);
	
	if(!entry_node->entry.name) { 
		std_err();
	}

	memcpy(entry_node->entry.name, file_path, name_length);

	printf("  %08x %10zu   %s\n", (unsigned int)data_offset, data_size, file_path);
	return(0);
}

void extract_file(FILE *pak_file_object, char *file_path, pak_u32 data_offset, pak_u32 data_size) {
	FILE    *fdo;
	size_t  chunk_size;
	size_t  bytes_read;
	size_t 	bytes_remaining;
	unsigned char  buff[8192];
	char	*path;

	if(fseek(pak_file_object, data_offset, SEEK_SET) < 0){ 
		std_err();
	}

	for(path = file_path; *path; path++) {
		if((*path == '\\') || (*path == '/')) {
			*path = 0;
			MKDIR(file_path);
			*path = PATHSLASH;
		} else {
			*path = (char)tolower((unsigned char)*path);
		}
	}

	fdo = fopen(file_path, "wb");
	if(!fdo){ 
		std_err();
	}

	bytes_remaining = data_size;

	while(bytes_remaining) {
		chunk_size = sizeof(buff);

		if(bytes_remaining < chunk_size) {
			chunk_size = bytes_remaining;
		}

		bytes_read = fread(buff, 1, chunk_size, pak_file_object);

		if(bytes_read == 0) {
			std_err();
		}

		file_write(buff, bytes_read, fdo);
		bytes_remaining -= bytes_read;
	}

	fclose(fdo);
}



pak_u32 read_le_uint(FILE *pak_file_object, int bits) {
	pak_u32   num;
	int     i;
	int 	bytes;
	unsigned char  tmp[sizeof(pak_u32)];
	
	bytes = bits >> 3;
	
	if(fread(tmp, bytes, 1, pak_file_object) != 1) {
		std_err();
	}

	for(num = i = 0; i < bytes; i++) {
		num |= ((pak_u32)tmp[i] << (i << 3));
	}

	return(num);
}



void write_le_uint(FILE *pak_file_object, pak_u32 num, int bits) {
	int     i;
	int bytes;
	unsigned char  tmp[sizeof(pak_u32)];
	
	bytes = bits >> 3;
	for(i = 0; i < bytes; i++) {
		tmp[i] = (num >> (i << 3)) & 0xff;
	}
	file_write(tmp, bytes, pak_file_object);
}

int pack_directory(FILE *pak_file_object, char *directory_path) {
	char  tcDir[FNAMEMAX];

	struct  stat    xstat;
	struct  dirent  **namelist;
	int             n,
					i;
	int written;

	n = scandir(directory_path, &namelist, NULL, alphasort);
	if(n < 0) {
		
		if(stat(directory_path, &xstat) < 0) {
			printf("**** %s", directory_path);
			std_err();
		}

		if(pack_file(pak_file_object, directory_path) < 0) {
			return(-1);
		}

	} else {
		for(i = 0; i < n; i++) {    // Changed by Plombo
			
			/*
			*  scandir() allocates every returned entry, including "." and "..".
			*  Free skipped entries before continuing.
			*/
			if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, "..")) {
				free(namelist[i]);
				continue;
			}

			/*
			*  Construct the full path for the current entry.
			*  Check for path length overflow.
			*/
			

			written = snprintf(tcDir, sizeof(tcDir), "%s/%s", directory_path, namelist[i]->d_name);
			if(written < 0 || (size_t)written >= sizeof(tcDir)) {
				printf("\nError: path too long: %s/%s\n", directory_path, namelist[i]->d_name);
				goto quit;
			}

			if(stat(tcDir, &xstat) < 0) {
				printf("**** %s", tcDir);
				std_err();
			}
			if(S_ISDIR(xstat.st_mode)) {
				if(pack_directory(pak_file_object, tcDir) < 0) {
					goto quit;
				}
			} else {
				if(pack_file(pak_file_object, tcDir) < 0) {
					goto quit;
				}
			}
			free(namelist[i]);
		}
		free(namelist);
	}

	return(0);
quit:
	for(; i < n; i++) {
		free(namelist[i]);
	}
	free(namelist);
	return(-1);
}



void write_err(void) {
	printf("\nError: write error; probably out of disk space\n\n");
	exit(1);
}



void std_err(void) {
	perror("\nError");
	exit(1);
}

#ifdef WIN32
void winpause(void) {
	system("pause");
}
#endif
