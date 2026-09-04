#include "fs.h"
#include "io.h"
#include "string.h"
#include <stddef.h>

/* In-memory filesystem */
static inode_t inodes[MAX_FILES + MAX_DIRS];
static int inodes_count = 0;

/* Initialize filesystem with root directory */
void fs_init(void) {
    inodes_count = 0;
    /* Create root directory */
    strcpy(inodes[0].name, "/");
    inodes[0].type = TYPE_DIR;
    inodes[0].size = 0;
    inodes[0].data[0] = '\0';
    inodes[0].parent = 0;
    inodes_count = 1;
}

/* Find a file/dir by path */
inode_t* find_file(const char* path) {
    int i;
    if (path == NULL || path[0] == '\0') return NULL;
    
    /* Handle root */
    if (strcmp(path, "/") == 0) return &inodes[0];
    
    /* Strip leading slash */
    const char* p = path;
    if (p[0] == '/') p++;
    
    /* Search by basename */
    char basename[MAX_FILENAME];
    int len = strlen(p);
    if (len >= MAX_FILENAME) return NULL;
    strcpy(basename, p);
    
    /* Remove trailing slash */
    if (len > 0 && basename[len-1] == '/') basename[len-1] = '\0';
    
    for (i = 0; i < inodes_count; i++) {
        if (strcmp(inodes[i].name, basename) == 0) {
            return &inodes[i];
        }
    }
    return NULL;
}

int file_exists(const char* path) {
    return find_file(path) != NULL;
}

int create_file(const char* path) {
    if (inodes_count >= MAX_FILES + MAX_DIRS) return -1;
    if (file_exists(path)) return -1;
    
    char basename[MAX_FILENAME];
    int len = strlen(path);
    if (len >= MAX_FILENAME) return -1;
    strcpy(basename, path);
    if (len > 0 && basename[len-1] == '/') basename[len-1] = '\0';
    
    inode_t* file = &inodes[inodes_count];
    strcpy(file->name, basename);
    file->type = TYPE_FILE;
    file->size = 0;
    file->data[0] = '\0';
    file->parent = 0;
    inodes_count++;
    return 0;
}

int delete_file(const char* path) {
    inode_t* file = find_file(path);
    if (file == NULL) return -1;
    if (file->type == TYPE_DIR) return -1;
    
    /* Find the index of this inode */
    int idx = (int)(file - inodes);
    
    /* Shift all inodes down to fill the gap */
    int i;
    for (i = idx; i < inodes_count - 1; i++) {
        inodes[i] = inodes[i+1];
    }
    inodes_count--;
    return 0;
}

int mkdir(const char* path) {
    if (inodes_count >= MAX_FILES + MAX_DIRS) return -1;
    if (file_exists(path)) return -1;
    
    char basename[MAX_FILENAME];
    int len = strlen(path);
    if (len >= MAX_FILENAME) return -1;
    strcpy(basename, path);
    if (len > 0 && basename[len-1] == '/') basename[len-1] = '\0';
    
    inode_t* dir = &inodes[inodes_count];
    strcpy(dir->name, basename);
    dir->type = TYPE_DIR;
    dir->size = 0;
    dir->data[0] = '\0';
    dir->parent = 0;
    inodes_count++;
    return 0;
}

int rmdir(const char* path) {
    inode_t* dir = find_file(path);
    if (dir == NULL) return -1;
    if (dir->type != TYPE_DIR) return -1;
    
    int idx = (int)(dir - inodes);
    int i;
    for (i = idx; i < inodes_count - 1; i++) {
        inodes[i] = inodes[i+1];
    }
    inodes_count--;
    return 0;
}

int read_file(const char* path, char* buffer, size_t max_len) {
    inode_t* file = find_file(path);
    if (file == NULL || file->type != TYPE_FILE) return -1;
    
    size_t to_copy = (size_t)file->size;
    if (to_copy >= max_len) to_copy = max_len - 1;
    memcpy(buffer, file->data, to_copy);
    buffer[to_copy] = '\0';
    return (int)to_copy;
}

int write_file(const char* path, const char* data, size_t len) {
    inode_t* file = find_file(path);
    if (file == NULL) {
        if (create_file(path) != 0) return -1;
        file = find_file(path);
    }
    if (file->type != TYPE_FILE) return -1;
    if (len >= MAX_FILE_SIZE) len = MAX_FILE_SIZE - 1;
    memcpy(file->data, data, len);
    file->data[len] = '\0';
    file->size = (int)len;
    return 0;
}

void ls(const char* path) {
    (void)path;
    int i;
    for (i = 0; i < inodes_count; i++) {
        if (inodes[i].type == TYPE_DIR) {
            terminal_setcolor(0x0B); /* Light cyan */
        } else {
            terminal_setcolor(0x0F); /* White */
        }
        terminal_writestring(inodes[i].name);
        if (inodes[i].type == TYPE_DIR) terminal_putchar('/');
        terminal_putchar('\n');
    }
    terminal_setcolor(0x07);
}

void get_parent_path(const char* path, char* parent) {
    int len = strlen(path);
    int i;
    if (len <= 1) { parent[0] = '/'; parent[1] = '\0'; return; }
    for (i = len - 1; i >= 0; i--) {
        if (path[i] == '/' && i < len - 1) {
            if (i == 0) { parent[0] = '/'; parent[1] = '\0'; }
            else { strncpy(parent, path, i); parent[i] = '\0'; }
            return;
        }
    }
    parent[0] = '/'; parent[1] = '\0';
}

void get_basename(const char* path, char* base) {
    int len = strlen(path);
    int i;
    if (len == 0) { base[0] = '\0'; return; }
    for (i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            strcpy(base, path + i + 1);
            return;
        }
    }
    strcpy(base, path);
}
