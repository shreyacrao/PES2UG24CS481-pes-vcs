#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

int commit_create(const char *message, ObjectID *commit_id_out) {
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;
    
    char tree_hex[65]; hash_to_hex(&tree_id, tree_hex);
    char buf[4096];
    
    char parent_hex[65] = "";
    FILE *pf = fopen(".pes/refs/heads/main", "r");
    if (pf) {
        fscanf(pf, "%64s", parent_hex);
        fclose(pf);
    }

    int n;
    if (strlen(parent_hex) > 0) {
        n = sprintf(buf, "tree %s\nparent %s\nauthor Shreya Rao <PES2UG24CS481> %ld\n\n%s\n", 
                    tree_hex, parent_hex, (long)time(NULL), message);
    } else {
        n = sprintf(buf, "tree %s\nauthor Shreya Rao <PES2UG24CS481> %ld\n\n%s\n", 
                    tree_hex, (long)time(NULL), message);
    }
    
    object_write(OBJ_COMMIT, buf, n, commit_id_out);
    
    mkdir(".pes/refs", 0755);
    mkdir(".pes/refs/heads", 0755);
    FILE *f = fopen(".pes/refs/heads/main", "w");
    char commit_hex[65]; hash_to_hex(commit_id_out, commit_hex);
    fprintf(f, "%s\n", commit_hex);
    fclose(f);
    
    printf("Committed: %.12s... %s\n", commit_hex, message);
    return 0;
}

int commit_walk(commit_walk_fn callback, void *ctx) {
    FILE *f = fopen(".pes/refs/heads/main", "r");
    if (!f) return -1;
    char hex[65];
    if (fscanf(f, "%64s", hex) != 1) { fclose(f); return -1; }
    fclose(f);

    while (strlen(hex) > 0) {
        ObjectID id;
        hex_to_hash(hex, &id);
        
        char path[512];
        snprintf(path, sizeof(path), ".pes/objects/%.2s/%s", hex, hex + 2);
        FILE *obj = fopen(path, "r");
        if (!obj) break;

        char line[1024];
        Commit c; memset(&c, 0, sizeof(c));
        char next_hex[65] = "";

        while (fgets(line, sizeof(line), obj)) {
            if (strncmp(line, "author ", 7) == 0) {
                // Find the timestamp at the end of the author line
                char *last_space = strrchr(line, ' ');
                if (last_space) {
                    c.timestamp = strtoul(last_space + 1, NULL, 10);
                    *last_space = '\0';
                }
                strncpy(c.author, line + 7, sizeof(c.author)-1);
            } else if (strncmp(line, "parent ", 7) == 0) {
                sscanf(line + 7, "%s", next_hex);
            } else if (line[0] == '\n') {
                if (fgets(c.message, sizeof(c.message), obj)) {
                    c.message[strcspn(c.message, "\n")] = 0;
                }
                break;
            }
        }
        fclose(obj);

        callback(&id, &c, ctx);
        strcpy(hex, next_hex);
    }
    return 0;
}
