#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"

/* Update one title through SQLite locking; never replace the live app database. */
int main(void) {
    sqlite3 *db = NULL, *backup = NULL;
    sqlite3_stmt *tables = NULL;
    FILE *log = fopen("/data/xmr-miner-metadata.log", "w");
    if (!log) return 1;
    int rc = sqlite3_open_v2("/system_data/priv/mms/app.db", &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc) goto done;
    sqlite3_busy_timeout(db, 5000);
    /* Keep a consistent rollback snapshot on the console before the transaction. */
    rc = sqlite3_open("/data/xmr-miner-metadata-backup.db", &backup);
    if (rc) goto done;
    sqlite3_backup *copy = sqlite3_backup_init(backup, "main", db, "main");
    if (!copy) { rc = SQLITE_ERROR; goto done; }
    rc = sqlite3_backup_step(copy, -1);
    int finish = sqlite3_backup_finish(copy);
    if (rc != SQLITE_DONE || finish) { rc = SQLITE_ERROR; goto done; }
    sqlite3_close(backup); backup = NULL;
    /* appinfo.db is authoritative; launch regenerates app.db from these rows. */
    rc = sqlite3_exec(db, "ATTACH DATABASE '/system_data/priv/mms/appinfo.db' AS appinfo", NULL, NULL, NULL);
    if (rc) goto done;
    rc = sqlite3_open("/data/xmr-miner-appinfo-backup.db", &backup);
    if (rc) goto done;
    copy = sqlite3_backup_init(backup, "main", db, "appinfo");
    if (!copy) { rc = SQLITE_ERROR; goto done; }
    rc = sqlite3_backup_step(copy, -1);
    finish = sqlite3_backup_finish(copy);
    if (rc != SQLITE_DONE || finish) { rc = SQLITE_ERROR; goto done; }
    rc = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    if (rc) goto done;
    rc = sqlite3_exec(db,
        "UPDATE appinfo.tbl_appinfo SET val='XMR Miner' WHERE titleId='PPSA99531' "
        "AND key IN ('TITLE','TITLE_01') AND metaDataId IN "
        "(SELECT metaDataId FROM appinfo.tbl_appinfo WHERE titleId='PPSA99531' "
        "AND key='CONTENT_ID' AND val='UP9000-PPSA99531_00-PS5CPUGPUMINER01')",
        NULL, NULL, NULL);
    if (rc || sqlite3_changes(db) != 2) { rc = SQLITE_ERROR; goto rollback; }
    /* Installed titles retain cached capabilities; enable Power Saver on upgrade. */
    rc = sqlite3_exec(db,
        "UPDATE appinfo.tbl_appinfo SET val=(CAST(val AS INTEGER) | 1) "
        "WHERE titleId='PPSA99531' AND key='ATTRIBUTE4' AND metaDataId IN "
        "(SELECT metaDataId FROM appinfo.tbl_appinfo WHERE titleId='PPSA99531' "
        "AND key='CONTENT_ID' AND val='UP9000-PPSA99531_00-PS5CPUGPUMINER01')",
        NULL, NULL, NULL);
    if (rc || sqlite3_changes(db) != 1) { rc = SQLITE_ERROR; goto rollback; }
    char *stamp_sql = sqlite3_mprintf(
        "UPDATE appinfo.tbl_appinfo SET val=%lld WHERE titleId='PPSA99531' "
        "AND key='_path_icon0_info_time_stamp'", (sqlite3_int64)time(NULL));
    rc = sqlite3_exec(db, stamp_sql, NULL, NULL, NULL);
    sqlite3_free(stamp_sql);
    if (rc) goto rollback;
    /* Guard the exact title/content identity before changing any display fields. */
    char *sql = sqlite3_mprintf(
        "UPDATE tbl_contentinfo SET titleName='XMR Miner',"
        "AppInfoJson=json_set(AppInfoJson,'$.TITLE','XMR Miner','$.TITLE_01','XMR Miner',"
        "'$.ATTRIBUTE4',(CAST(COALESCE(json_extract(AppInfoJson,'$.ATTRIBUTE4'),0) AS INTEGER) | 1),"
        "'$._path_icon0_info_time_stamp',%lld),"
        "icon0Info='/user/app/PPSA99531/sce_sys/icon0.png?ts=%lld' "
        "WHERE titleId='PPSA99531' AND contentId='UP9000-PPSA99531_00-PS5CPUGPUMINER01'",
        (sqlite3_int64)time(NULL), (sqlite3_int64)time(NULL));
    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_free(sql);
    if (rc || sqlite3_changes(db) != 1) { rc = SQLITE_ERROR; goto rollback; }
    /* Every local user's home tile holds its own copy of the title name. */
    rc = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name GLOB 'tbl_iconinfo_[0-9]*'", -1, &tables, NULL);
    if (rc) goto rollback;
    while ((rc = sqlite3_step(tables)) == SQLITE_ROW) {
        const char *table = (const char *)sqlite3_column_text(tables, 0);
        sql = sqlite3_mprintf("UPDATE \"%w\" SET titleName='XMR Miner' WHERE titleId='PPSA99531' AND contentId='UP9000-PPSA99531_00-PS5CPUGPUMINER01'", table);
        int update = sqlite3_exec(db, sql, NULL, NULL, NULL);
        sqlite3_free(sql);
        if (update) { rc = update; break; }
    }
    sqlite3_finalize(tables); tables = NULL;
    if (rc != SQLITE_DONE) goto rollback;
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    goto done;
rollback:
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
done:
    fprintf(log, "metadata refresh status=%d error=%s\n", rc, db ? sqlite3_errmsg(db) : "open failed");
    if (tables) sqlite3_finalize(tables);
    if (backup) sqlite3_close(backup);
    if (db) sqlite3_close(db);
    fclose(log);
    return rc != SQLITE_OK;
}
