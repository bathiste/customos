#ifndef FS_H
#define FS_H

#include <stddef.h>

#define MAX_FILENAME  64
#define MAX_PATH      256
#define MAX_FILES     64
#define MAX_DIRS      32
#define MAX_FILE_SIZE 8192

/* File types */
#define TYPE_FILE  1
#define TYPE_DIR   2

/* Inode structure */
typedef struct {
    char name[MAX_FILENAME];
    int type;
    int size;
    char data[MAX_FILE_SIZE];
    int parent;  /* Parent directory index */
} inode_t;

/* Initialize filesystem */
void fs_init(void);

/* Directory operations */
int mkdir(const char* path);
int rmdir(const char* path);

/* File operations */
int create_file(const char* path);
int delete_file(const char* path);
int file_exists(const char* path);
inode_t* find_file(const char* path);
int read_file(const char* path, char* buffer, size_t max_len);
int write_file(const char* path, const char* data, size_t len);

/* List directory */
void ls(const char* path);

/* Path utilities */
void get_parent_path(const char* path, char* parent);
void get_basename(const char* path, char* base);

#endif
