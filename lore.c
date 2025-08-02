#include <ctype.h>
#include <stddef.h>
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
            assert(da->items != NULL && "ERROR: Your db might be fucked\n");           \
        }                                                                     \
        da->items[da->count++] = (item);                                      \
    } while (0)

bool initialize_file_creation(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO File_Creation DEFAULT VALUES;", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return false;
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_finalize(stmt);

    return true;
}

bool update_file_creation_message(sqlite3 *db) 
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    unsigned int non_null_value = 3;

    // Check if table is empty
    int ret = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM File_Creation;", -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    int row_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (row_count == 0) {
        // Update first row since it doesnt exist
        if (!initialize_file_creation(db)) return_defer(false);
    } else {
        // Table not empty, no update needed
        return_defer(false);
    }

    // Reselect first row
    ret = sqlite3_prepare_v2(db, "SELECT id, active, datetime(created_at, 'localtime') FROM File_Creation LIMIT 1;", -1, &stmt, NULL);
    if (ret != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    int id = sqlite3_column_int(stmt, 0);
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
        return_defer(false);
    } 

    sqlite3_finalize(stmt);
    stmt = NULL;

    // Prepare update statement
    ret = sqlite3_prepare_v2(db, "UPDATE File_Creation SET active = ? where id = ?;", -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_bind_int(stmt, 1, non_null_value) != SQLITE_OK || 
            sqlite3_bind_int(stmt, 2, id) != SQLITE_OK) {
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

bool create_schema(sqlite3 *db)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS Notifications (\n"
        "    id INTEGER PRIMARY KEY ASC,\n"
        "    title TEXT NOT NULL,\n"
        "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
        "    dismissed_at DATETIME DEFAULT NULL\n"
        ")\n";
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sql =
        "CREATE TABLE IF NOT EXISTS Reminders (\n"
        "    id INTEGER PRIMARY KEY ASC,\n"
        "    title TEXT NOT NULL,\n"
        "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
        "    scheduled_at DATE NOT NULL,\n"
        "    period TEXT DEFAULT NULL,\n"
        "    finished_at DATETIME DEFAULT NULL\n"
        ")\n";
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sql =
        "CREATE TABLE IF NOT EXISTS File_Creation (\n"
        "    id INTEGER PRIMARY KEY ASC,\n"
        "    active INTEGER DEFAULT NULL,\n"
        "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
        ")\n";
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return false;
    }

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

    int ret = sqlite3_prepare_v2(db, "SELECT id, title, datetime(created_at, 'localtime') FROM Notifications WHERE dismissed_at IS NULL;", -1, &stmt, NULL);
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

    for (int i = 0; (size_t)i < notifs.count; i++) {
        printf("%d: %s (%s)\n", i, notifs.items[i].title, notifs.items[i].created_at);
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

bool dismiss_notification_by_id(sqlite3 *db, int id)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    int ret = sqlite3_prepare_v2(db, "UPDATE Notifications SET dismissed_at = CURRENT_TIMESTAMP WHERE id = ?", -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }
    
    if (sqlite3_bind_int(stmt, 1, id) != SQLITE_OK) {
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

bool dismiss_notification_by_index(sqlite3 *db, int index)
{   
    bool result = true;

    Notifications notifs = {0};
    if (!load_active_notifications(db, &notifs)) return_defer(false);
    if (!(0 <= index && (size_t)index < notifs.count)) {
        fprintf(stderr, "ERROR: %d is not a valid index of an active notification.\n", index);
        return_defer(false);
    }

    if (!dismiss_notification_by_id(db, notifs.items[index].id)) return_defer(false);

defer:
    free(notifs.items);
    return result;
}

bool show_active_reminders(sqlite3 *db) {
    assert(0 && "NOT IMPLEMENTED: show active reminders");
}

bool create_new_reminder(sqlite3 *db, const char *title, const char *scheduled_at, const char *period)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "INSERT INTO Reminders (title, scheduled_at, period) VALUES (?, ?, ?)", -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_bind_text(stmt, 1, title, strlen(title), NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_bind_text(stmt, 2, scheduled_at, strlen(scheduled_at), NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLITE3 ERROR: %s\n", sqlite3_errmsg(db));
        return_defer(false);
    }

    if (sqlite3_bind_text(stmt, 3, period, period ? strlen(period) : 0, NULL) != SQLITE_OK) {
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

// TODO: consider valid date range (ie. month 1-12, day 1-31)
bool valid_date_format_checker(const char *str) 
{
    char date_format[] = "YYYY-MM-DD";
    char date[sizeof(date_format)] = {0};
    size_t year_count = 0, month_count = 0, day_count = 0, pad_count = 0;

    for (size_t i = 0; i < strlen(str); i++) {
        switch (pad_count) {
            case 0: // within the year part of the date format
                if (isdigit(str[i])) {
                    // fill date string with partial date format (YYYY)
                    if (year_count >= 4) break;
                    date[year_count++] = str[i];
                } else if (isascii((char)str[i]) && (char)str[i] == '-') {
                    date[year_count] = '-';
                    pad_count++;
                } else{
                    return false;
                }
                break;
            case 1: // within month part of the date format
                if (isdigit(str[i])) {
                    // fill date string with partial date format (MM)
                    if (month_count >= 2) break;
                    date[year_count+pad_count+month_count++] = str[i];
                } else if (isascii((char)str[i]) && (char)str[i] == '-') {
                    date[year_count+pad_count+month_count] = '-';
                    pad_count++;
                } else{
                    return false;
                }
                break;
            case 2: // within the day part of the date format
                if (isdigit(str[i])) {
                    if (day_count >= 2) break;
                    // fill date string with partial date format (DD)
                    date[year_count+pad_count+month_count+day_count++] = str[i];
                } else if (isascii((char)str[i]) && (char)str[i] == ' ') {
                    date[year_count+pad_count+month_count+day_count] = '\0';
                    break;
                } else{
                    return false;
                }
                break;
            default:
                // Not sure the default case will ever be reached but here for sanity
                printf("ERROR: valid date format of the form '%s' is in incorrect\n", date_format);
                exit(1);
        }
    }

    bool result = strcmp(str, date) == 0;
    return result;
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

#ifdef LOCAL
    const char *path = getenv("PWD");
    if (path == NULL) {
        fprintf(stderr, "ERROR: No $PWD environment variable is set up. We need it to find the location of the ./"LORE_FILENAME" database.\n");
    }
#else
    const char *path = getenv("HOME");
    if (path == NULL) {
        fprintf(stderr, "ERROR: No $HOME environment variable is set up. We need it to find the location of the ./"LORE_FILENAME" database.\n");
    }
#endif

    char lore_path[256];
    int ra = sprintf(lore_path, "%s/"LORE_FILENAME"", path); 

    if (ra < 0) {
        fprintf(stderr, "ERROR: sprintf failed to generate file name: %s\n", LORE_FILENAME);
        return_defer(1);
    }

    int ret = sqlite3_open(lore_path, &db);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", lore_path, sqlite3_errstr(ret));
        return_defer(1);
    }         

    if (!create_schema(db)) return_defer(1);
    if (update_file_creation_message(db)) { // one time execution for newly created databases
        fprintf(stdout, "Created database file here: \"%s\"\n", lore_path);
    }

    // Fire of notifications everytime `lore` is called
    if (strcmp(cmd, "checkout") == 0) {
        if (!show_active_notifications(db)) return_defer(1);
        // TODO: arguably can display reminders as well that are not specifically give periods (ie. period is NULL)
        return_defer(0);
    }

    if (strcmp(cmd, "notify") == 0) {
        if (argc <= 0) {
            fprintf(stderr, "Usage: %s notify <title>\n", program_name);
            fprintf(stderr, "ERROR: expeced title\n");
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

    if (strcmp(cmd, "dismiss") == 0) {
        if (argc <= 0) {
            fprintf(stderr, "Usage: %s dismiss <id>\n", program_name);
            fprintf(stderr, "ERROR: expeced id\n");
            return_defer(1);
        }

        int index = atoi(shift(argv, argc));
        if (!dismiss_notification_by_index(db, index)) return_defer(1);
        if (!show_active_notifications(db)) return_defer(1);
        return_defer(0);
    }

    if (strcmp(cmd, "remind") == 0) {
        if (argc <= 0) {
            // TODO: implement show reminders function
            if (!show_active_reminders(db)) return_defer(1);
            return_defer(0);
        }
        char *tmp = NULL;

        for (bool pad = false; argc > 0; pad = true) {
            if (valid_date_format_checker(*argv)) {
                tmp = *argv;
                shift(argv, argc);
                break;
            } else {
                if (pad) sb_append_cstr(&sb, " ");
                sb_append_cstr(&sb, shift(argv, argc));
            }
        }
        sb_append_null(&sb);
        const char *title = sb.items;
        const char *scheduled_at = tmp;

        if (scheduled_at != NULL) {
            if (argc > 0) {
                // Optional [period] is present for reminders to periodically fire off
                assert(0 && "NOT IMPLEMENTED: periodically perform this reminder");
                // TODO: implement periodic reminder scheduling
            }
        } else {
            fprintf(stderr, "Usage: %s remind [<title> <date> [period]]\n", program_name);
            fprintf(stderr, "ERROR: expected date: YYYY-MM-DD\n");
            return_defer(1);
        }

        // if period is null, reminders kick off just like a notification will with the exception for only the
        // entirety of the date in which it was create/scheduled at.
        if (!create_new_reminder(db, title, scheduled_at, NULL)) return_defer(1);
        return_defer(0);
    }

    fprintf(stderr, "ERROR: unknown command %s\n", cmd);

defer:
    if (db) sqlite3_close(db);
    free(sb.items);
    return result;
}
