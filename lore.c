#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "sqlite3.h"

#define SB_INIT_CAP 256
#define DB_INIT_CAP 528

#define shift(src, src_sz) (assert(src_sz > 0), (src_sz)--, *(src)++)

typedef struct {
    int id;
    char *title;
    char *created_at;
} Notification;

typedef struct {
    Notification *items;
    size_t count;
    size_t capacity;
} Notifcations;

void create_notification(Notifcations *db, const char *title)
{
    // add "title" along with time stamp for current notification creation
    // [0] "foo bar" YYYY-MM-DD - time
    if (db->count >= db->capacity) {
        db->capacity = db->capacity == 0 ? DB_INIT_CAP : DB_INIT_CAP*2;
        db->items = realloc(db->items, db->capacity*sizeof(*db->items));
        assert(db->items != NULL && "ERROR: database has been compromised, better luck nex...");
    }
    db->items->id = db->count;
    db->items->title = strdup(title);
    db->items->created_at = "Some time ago";
    db->count++;
}

void print_db(Notifcations *db)
{
    if (db == NULL) {
        fprintf(stderr, "ERROR: error reading database\n");
    }
        
    printf("db size: %zu\n", db->count);
        
    for (size_t i = 0; i < db->count; i++) {
        printf("[%zu]: %s (%s)\n", (size_t)db->items->id, db->items->title, db->items->created_at);
    }
}

typedef struct  {
    char *items;
    size_t count;
    size_t capacity;
} String_Builder;

void sb_append_cstr(String_Builder *sb, const char *str)
{
    size_t n = strlen(str);

    if (sb->count + n >= sb->capacity) {
        if (sb->capacity == 0) {
            sb->capacity = SB_INIT_CAP;
        }
        while (sb->count + n >= sb->capacity) {
            sb->capacity = sb->capacity*2; 
        }
        sb->items = realloc(sb->items, sb->capacity*sizeof(sb->items));
        assert(sb->items != NULL && "ERROR: dynamic allocation error...");
    }
    memcpy(sb->items+sb->count, str, n*sizeof(sb->items));
    sb->count += n;
}

void sb_append_null(String_Builder *sb)
{
    if (sb->count >= sb->capacity) {
        sb->capacity = sb->capacity == 0 ? 256 : sb->capacity*2; 
        sb->items = realloc(sb->items, sb->capacity*sizeof(sb->items));
        assert(sb->items != NULL);
    }
    sb->items[sb->count++] = '\0';
}

int main(int argc, char **argv)
{
    Notifcations db = {0};
    
    const char *program_name = shift(argv, argc);
    String_Builder sb = {0};

    if (argc <= 0) {
        // If the program alone is run at least try to dump the 
        // database since it will be created upon compilation
        printf("%s\n", program_name);
        print_db(&db);
        return 1;
    }

    const char *cmd = shift(argv, argc);
    
    if (strcmp(cmd, "notify") == 0) {
        if (argc <= 0) {
            fprintf(stderr, "Usage: %s notify <title>\n", program_name);
            fprintf(stderr, "ERROR: expected <title>\n");
            return 1;
        }

        for (bool pad = false; argc > 0; pad = true) {
            if (pad) sb_append_cstr(&sb, " ");
            sb_append_cstr(&sb, shift(argv, argc));
        }
        sb_append_null(&sb);

        char *title = sb.items;
        printf("TITLE: %s\n", title);
                
        create_notification(&db, title);
        print_db(&db);
        return 0;
    }

    fprintf(stderr, "ERROR: unknown command %s", cmd);
    free(db.items);

    return 0;
}
