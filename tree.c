#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MODE_FILE 0100644
#define MODE_EXEC 0100755
#define MODE_DIR  0040000

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;
    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];
        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;
        char mode_str[16] = {0};
        memcpy(mode_str, ptr, space - ptr);
        entry->mode = strtol(mode_str, NULL, 8);
        ptr = space + 1;
        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;
        strncpy(entry->name, (const char *)ptr, sizeof(entry->name)-1);
        ptr = null_byte + 1;
        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;
        tree_out->count++;
    }
    return 0;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = tree->count * 300; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);
    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1;
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }
    *data_out = buffer;
    *len_out = offset;
    return 0;
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

static int write_tree_recursive(IndexEntry *entries, int count, const char *prefix, ObjectID *id_out) {
    Tree t; t.count = 0;
    int i = 0;
    size_t prefix_len = strlen(prefix);
    while (i < count) {
        const char *rel = entries[i].path + prefix_len;
        const char *slash = strchr(rel, '/');
        if (!slash) {
            TreeEntry *te = &t.entries[t.count++];
            te->mode = entries[i].mode;
            strncpy(te->name, rel, sizeof(te->name)-1);
            te->hash = entries[i].hash;
            i++;
        } else {
            char dir_name[256];
            size_t dlen = slash - rel;
            strncpy(dir_name, rel, dlen); dir_name[dlen] = '\0';
            char new_prefix[512];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s/", prefix, dir_name);
            int j = i;
            while (j < count && strncmp(entries[j].path, new_prefix, strlen(new_prefix)) == 0) j++;
            ObjectID sub_id;
            if (write_tree_recursive(entries + i, j - i, new_prefix, &sub_id) != 0) return -1;
            TreeEntry *te = &t.entries[t.count++];
            te->mode = MODE_DIR;
            strncpy(te->name, dir_name, sizeof(te->name)-1);
            te->hash = sub_id;
            i = j;
        }
    }
    void *data; size_t data_len;
    tree_serialize(&t, &data, &data_len);
    int rc = object_write(OBJ_TREE, data, data_len, id_out);
    free(data);
    return rc;
}

static int cmp_idx(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

int tree_from_index(ObjectID *id_out) {
    Index idx;
    if (index_load(&idx) != 0 || idx.count == 0) {
        Tree t; t.count = 0;
        void *data; size_t len;
        tree_serialize(&t, &data, &len);
        int rc = object_write(OBJ_TREE, data, len, id_out);
        free(data); return rc;
    }
    qsort(idx.entries, idx.count, sizeof(IndexEntry), cmp_idx);
    return write_tree_recursive(idx.entries, idx.count, "", id_out);
}
