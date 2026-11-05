// src/stage1/vfs.c - Dynamic in-memory Virtual File System backed by kmalloc/kfree

#include "vfs.h"
#include "heap.h"
#include "lib.h"

typedef struct {
    int in_use;
    char name[VFS_NAME_MAX];
    char* data;
    unsigned int size;
    unsigned int capacity;
} vfs_node_t;

static vfs_node_t vfs_table[VFS_MAX_FILES];
static int vfs_initialized = 0;

static vfs_node_t* vfs_find(const char* filename) {
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (vfs_table[i].in_use && kstrcmp(vfs_table[i].name, filename) == 0) {
            return &vfs_table[i];
        }
    }
    return 0;
}

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        vfs_table[i].in_use = 0;
        vfs_table[i].name[0] = '\0';
        vfs_table[i].data = 0;
        vfs_table[i].size = 0;
        vfs_table[i].capacity = 0;
    }
    vfs_initialized = 1;

    // Seed initial files on the kernel heap
    vfs_write_file("resume.txt", "Subhashish Reddy - MBA Candidate @ IIM Shillong | Systems & Product");
    vfs_write_file("data.csv", "Q1,Q2,Q3,Q4 -> 120,240,360,510");
    vfs_write_file("config.ini", "[os] version=2.0 arch=x86_64 author=Subhashish");
    vfs_write_file("notes.txt", "Welcome to SubhashishOS! Use the Shell or Notes app to edit me.");
}

int vfs_write_file(const char* filename, const char* content) {
    if (!vfs_initialized) vfs_init();
    if (!filename || !filename[0]) return -1;

    size_t len = kstrlen(content ? content : "");
    unsigned int needed = (unsigned int)len + 1;
    if (needed < 64) needed = 64;

    vfs_node_t* node = vfs_find(filename);
    if (!node) {
        for (int i = 0; i < VFS_MAX_FILES; i++) {
            if (!vfs_table[i].in_use) {
                node = &vfs_table[i];
                node->in_use = 1;
                kstrlcpy(node->name, filename, VFS_NAME_MAX);
                node->data = 0;
                node->size = 0;
                node->capacity = 0;
                break;
            }
        }
        if (!node) return -1; // Table full
    }

    if (!node->data || node->capacity < needed) {
        char* new_buf = (char*)kmalloc(needed);
        if (!new_buf) return -1;
        if (node->data) kfree(node->data);
        node->data = new_buf;
        node->capacity = needed;
    }

    kstrlcpy(node->data, content ? content : "", node->capacity);
    node->size = (unsigned int)len;
    return 0;
}

int vfs_append_file(const char* filename, const char* content) {
    if (!vfs_initialized) vfs_init();
    vfs_node_t* node = vfs_find(filename);
    if (!node) {
        return vfs_write_file(filename, content);
    }
    size_t add_len = kstrlen(content ? content : "");
    unsigned int needed = node->size + (unsigned int)add_len + 2;
    if (needed > node->capacity) {
        unsigned int new_cap = needed + 64;
        char* new_buf = (char*)kmalloc(new_cap);
        if (!new_buf) return -1;
        kstrlcpy(new_buf, node->data, new_cap);
        kfree(node->data);
        node->data = new_buf;
        node->capacity = new_cap;
    }
    if (node->size > 0) {
        kstrlcat(node->data, " ", node->capacity);
    }
    kstrlcat(node->data, content ? content : "", node->capacity);
    node->size = (unsigned int)kstrlen(node->data);
    return 0;
}

int vfs_delete_file(const char* filename) {
    if (!vfs_initialized) vfs_init();
    vfs_node_t* node = vfs_find(filename);
    if (!node) return -1;
    if (node->data) kfree(node->data);
    node->data = 0;
    node->size = 0;
    node->capacity = 0;
    node->name[0] = '\0';
    node->in_use = 0;
    return 0;
}

void vfs_list_files(char* output_buffer, size_t cap) {
    if (!vfs_initialized) vfs_init();
    output_buffer[0] = '\0';
    int any = 0;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (vfs_table[i].in_use) {
            if (any) kstrlcat(output_buffer, "  ", cap);
            kstrlcat(output_buffer, vfs_table[i].name, cap);
            kstrlcat(output_buffer, "(", cap);
            char num[21];
            kutoa(vfs_table[i].size, num);
            kstrlcat(output_buffer, num, cap);
            kstrlcat(output_buffer, "B)", cap);
            any = 1;
        }
    }
    if (!any) {
        kstrlcpy(output_buffer, "(empty directory)", cap);
    }
}

int vfs_read_file(const char* filename, char* output_buffer, size_t cap) {
    if (!vfs_initialized) vfs_init();
    vfs_node_t* node = vfs_find(filename);
    if (!node) {
        kstrlcpy(output_buffer, "Error: File not found.", cap);
        return -1;
    }
    kstrlcpy(output_buffer, node->data ? node->data : "", cap);
    return 0;
}

int vfs_file_count(void) {
    if (!vfs_initialized) vfs_init();
    int count = 0;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (vfs_table[i].in_use) count++;
    }
    return count;
}

int vfs_get_file_info(int index, const char** name_out, unsigned int* size_out) {
    if (!vfs_initialized) vfs_init();
    int seen = 0;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (vfs_table[i].in_use) {
            if (seen == index) {
                if (name_out) *name_out = vfs_table[i].name;
                if (size_out) *size_out = vfs_table[i].size;
                return 0;
            }
            seen++;
        }
    }
    return -1;
}
