#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    char hex[HASH_HEX_SIZE + 1], path[512];
    uint32_t mode; uint64_t mtime, size;
    while (fscanf(f, "%o %64s %lu %lu %511s\n", &mode, hex, (unsigned long*)&mtime, (unsigned long*)&size, path) == 5) {
        IndexEntry *e = &index->entries[index->count++];
        e->mode = mode; hex_to_hash(hex, &e->hash);
        e->mtime_sec = mtime; e->size = size;
        strncpy(e->path, path, sizeof(e->path)-1);
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    FILE *f = fopen(INDEX_FILE, "w");
    if (!f) return -1;
    char hex[HASH_HEX_SIZE + 1];
    for (int i = 0; i < index->count; i++) {
        hash_to_hex(&index->entries[i].hash, hex);
        fprintf(f, "%o %s %lu %lu %s\n", index->entries[i].mode, hex, 
                (unsigned long)index->entries[i].mtime_sec, 
                (unsigned long)index->entries[i].size, index->entries[i].path);
    }
    fclose(f);
    return 0;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    void *buf = malloc(fsize); fread(buf, 1, fsize, f); fclose(f);
    ObjectID id;
    object_write(OBJ_BLOB, buf, fsize, &id);
    free(buf);
    
    IndexEntry *e = NULL;
    for(int i=0; i<index->count; i++) {
        if(strcmp(index->entries[i].path, path) == 0) { e = &index->entries[i]; break; }
    }
    if(!e) e = &index->entries[index->count++];
    
    strncpy(e->path, path, sizeof(e->path)-1);
    e->hash = id; e->mode = 0100644; e->size = st.st_size; e->mtime_sec = st.st_mtime;
    return index_save(index);
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    for (int i = 0; i < index->count; i++) printf("  staged: %s\n", index->entries[i].path);
    return 0;
}
