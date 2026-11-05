#pragma once
#include "lib.h"

#define VFS_MAX_FILES 16
#define VFS_NAME_MAX  32

void vfs_init(void);
void vfs_list_files(char* output_buffer, size_t cap);
int vfs_read_file(const char* filename, char* output_buffer, size_t cap);
int vfs_write_file(const char* filename, const char* content);
int vfs_append_file(const char* filename, const char* content);
int vfs_delete_file(const char* filename);
int vfs_file_count(void);
int vfs_get_file_info(int index, const char** name_out, unsigned int* size_out);
