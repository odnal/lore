#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "sqlite3.h"

#define return_defer(value) do { result = (value); goto defer; } while(0)

#define LORE_FILENAME ".lore"

#define SB_INIT_CAP 256
#define DB_INIT_CAP 528

#define shift(src, src_sz) (assert(src_sz > 0), (src_sz)--, *(src)++)

#define da_append(da, item)                                                   \
    do {                                                                      \
        if (da->count >= da->capacity) {                                      \
            da->capacity = da->capacity == 0 ? DB_INIT_CAP : da->capacity*2;  \
            da->items = realloc(da->items, da->capacity*sizeof(*da->items));   \
            assert(da->items != NULL && "ERROR: realloc failed\n");           \
        }                                                                     \
        da->items[da->count++] = (item);                                      \
    } while (0)

bool create_schema(sqlite3 *db)
{
    const char *sql =
    "CREATE TABLE IF NOT EXISTS Notifications (\n"
    "    id INTEGER PRIMARY KEY ASC,\n"
    "    title TEXT NOT NULL,\n"
    "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    dismissed_at DATETIME DEFAULT NULL\n"
    ")\n";

    char *errmsg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", errmsg);
        sqlite3_free(errmsg);
        return false;
    }

    // TODO: create schema for reminders

    return true;
}

typedef struct {
    int id;
    const char *title;
    const char *created_at;
} Notification;

typedef struct {
    Notification *items;
    size_t count;
    size_t capacity;
} Notifications;

bool load_active_notifications(sqlite3 *db, Notifications *notifs)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    int ret = sqlite3_prepare_v2(db, "SELECT id, title, datetime(created_at, 'localtime') FROM Notifications;", -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    ret = sqlite3_step(stmt);
    for (int index = 0; ret == SQLITE_ROW; index++) {
        int id = sqlite3_column_int(stmt, 0);
        const char *title = strdup((const char *)sqlite3_column_text(stmt, 1));
        const char *created_at = strdup((const char *)sqlite3_column_text(stmt, 2));
        da_append(notifs, ((Notification) {
            .id = id,
            .title = title,
            .created_at = created_at,
        }));

        ret = sqlite3_step(stmt);
    }

    if (ret != SQLITE_DONE) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool show_active_notifications(sqlite3 *db)
{
    bool result = true;
    Notifications notifs = {0};

    if (!load_active_notifications(db, &notifs)) return_defer(false);

    for (size_t i = 0; i < notifs.count; i++) {
        printf("%d: %s (%s)\n", notifs.items[i].id, notifs.items[i].title, notifs.items[i].created_at);
    }

defer:
    free(notifs.items);
    return result;
}

bool create_notification_with_title(sqlite3 *db, const char *title)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO Notifications (title) VALUES (?)", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_bind_text(stmt, 1, title, strlen(title), NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
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
    int result = 0;
    // Create sqlite database
    sqlite3 *db = NULL;
    String_Builder sb = {0};

    const char *program_name = shift(argv, argc);

    const char *cmd = "checkout";
    if (argc > 0) cmd = shift(argv, argc);

    // TODO: change to HOME for final version
    const char *home_path = getenv("PWD");
    if (home_path == NULL) {
        fprintf(stderr, "ERROR: No $PWD environment variable is set up. We need it to find the location of the ./"LORE_FILENAME" database.\n");
    }

    char lore_path[256];
    int ra = sprintf(lore_path, "%s/"LORE_FILENAME"", home_path); 

    if (ra < 0) {
        fprintf(stderr, "ERROR: sprintf failed to generate file name: %s\n", LORE_FILENAME);
        return_defer(1);
    }

    int ret = sqlite3_open(lore_path, &db);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", lore_path, sqlite3_errstr(ret));
        return_defer(1);
    } else {
        fprintf(stdout, "Created file: %s\n", lore_path);
    }

    if (!create_schema(db)) return_defer(1);

    // Fire of notifications everytime `lore` is called
    if (strcmp(cmd, "checkout") == 0) {
        if (!show_active_notifications(db)) return_defer(1);
        return_defer(0);
    }

    if (strcmp(cmd, "notify") == 0) {
        if (argc <= 0) {
            fprintf(stderr, "Usage: %s notify <title>\n", program_name);
            fprintf(stderr, "ERROR: expeced <title>\n");
            return_defer(1);
        }

        for (bool pad = false; argc > 0; pad = true) {
            if (pad) sb_append_cstr(&sb, " ");
            sb_append_cstr(&sb, shift(argv, argc));
        }
        sb_append_null(&sb);

        char *title = sb.items;
                
        if (!create_notification_with_title(db, title)) return_defer(1);
        if (!show_active_notifications(db)) return_defer(1);
        return_defer(0);
    }

    fprintf(stderr, "ERROR: unknown command %s\n", cmd);
defer:
    if (db) sqlite3_close(db);
    free(sb.items);
    return result;
}
