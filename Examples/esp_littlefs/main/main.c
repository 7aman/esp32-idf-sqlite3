#include "sqlite3.h"

#include "esp_littlefs.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ---------------------- Configuration ---------------------- */
#define EXAMPLE_FRESH_START (0) /* remove old database */
// #define EXAMPLE_DISABLE_TRACE

/* ----------------------- Helper Macros ---------------------- */

#ifndef EXAMPLE_DISABLE_TRACE
#define example_trace(...) printf(__VA_ARGS__)
#else
#define example_trace()
#endif

#define example_print(...) printf(__VA_ARGS__)
#define example_assert(X)  assert((X))
#define example_delay(ms)  vTaskDelay(pdMS_TO_TICKS(ms))
#define example_get_us()   esp_timer_get_time()
#define example_get_ms()   (esp_timer_get_time() / 1000)

/* ---------------------- Example ---------------------- */

#define MOUNT_POINT        "/storage"
#define PARTITION_LABEL    "storage"
#define DB_PATH            MOUNT_POINT "/test.db"

static esp_vfs_littlefs_conf_t conf = {
    .base_path = MOUNT_POINT,
    .partition_label = PARTITION_LABEL,
    .format_if_mount_failed = true,
    .dont_mount = false,
};

static int is_table_empty(void* data, int argc, char** argv, char** azColName)
{
    bool* empty = data;
    *empty = (atoi(argv[0]) == 0);
    return SQLITE_OK;
}

static int count_boots(void* data, int argc, char** argv, char** azColName)
{
    *(int*)data = atoi(argv[0]);
    return SQLITE_OK;
}

static int count_logs(void* data, int argc, char** argv, char** azColName)
{
    *(uint32_t*)data = atoi(argv[0]);
    return SQLITE_OK;
}

static int db_exec(sqlite3* db, const char* sql, sqlite3_callback cb, void* param)
{
    char* err_msg = NULL;
    example_trace("sqlite> %s\n", sql);
    const int64_t tic = example_get_us();
    int rc = sqlite3_exec(db, sql, cb, param, &err_msg);
    example_trace("example> Time taken: %lld us\r\n", example_get_us() - tic);
    if (rc != SQLITE_OK) {
        example_print("example> [Error] %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    return rc;
}

static int db_open(const char* filename, sqlite3** db)
{
    example_trace("example> open %s\r\n", filename);
    const int64_t tic = example_get_us();
    const int rc = sqlite3_open(filename, db);
    example_trace("example> Time taken: %lld us\r\n", example_get_us() - tic);
    if (rc) {
        example_print("example> [Error] Can't open database: %s\n", sqlite3_errmsg(*db));
        return rc;
    }
    return SQLITE_OK;
}

/* -------------------------------------------------------- */

static void initialize_vfs(void)
{
    example_print("example> Initializing FS\r\n");
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            example_print("example> [Error] Failed to mount or format filesystem\r\n");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            example_print("example> [Error] Failed to find FS partition\r\n");
        } else {
            example_print("example> [Error] Failed to initialize FS (%s)\r\n", esp_err_to_name(ret));
        }
        vTaskDelete(NULL);
        return;
    }
    example_print("example> Initializing FS is done!\r\n");
}

static void get_partition_info(void)
{
    example_print("example> Getting Partition info done!\r\n");
    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info(PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        example_print("example> [Error] Failed to get FS partition information (%s)\r\n", esp_err_to_name(ret));
    } else {
        example_print("example> Partition size: total: %d, used: %d\r\n", total, used);
    }
}

static void update_boot_count(sqlite3* db)
{

    bool isEmpty = false;
    int rc = db_exec(db, "SELECT count(*) FROM info;", is_table_empty, &isEmpty);
    if (rc != SQLITE_OK) {
        goto cleanup;
    }

    int boot_count = 0;
    if (isEmpty) {
        example_print("example> Insert `boot_cnt = 0` into table `info`.\r\n");
        rc = db_exec(db, "INSERT INTO info VALUES (0);", NULL, NULL);
        if (rc != SQLITE_OK) {
            goto cleanup;
        }
    } else {
        example_print("example> Get last `boot_cnt = 0` from table `info`.\r\n");
        rc = db_exec(db, "SELECT boot_cnt FROM info;", count_boots, &boot_count);
        if (rc != SQLITE_OK) {
            goto cleanup;
        }
    }

    boot_count++;
    example_print("----------------------------------------\r\n");
    example_print("example> boot count = %d\r\n", boot_count);
    example_print("----------------------------------------\r\n");

    char sql_statement[100];
    snprintf(sql_statement, sizeof(sql_statement), "UPDATE info SET boot_cnt = %d;", boot_count);
    rc = db_exec(db, sql_statement, NULL, NULL);
    if (rc != SQLITE_OK) {
        goto cleanup;
    }

cleanup:

    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        sqlite3_shutdown();
        esp_vfs_littlefs_unregister(PARTITION_LABEL);
        example_print("example> FS unmounted\r\n");
        vTaskDelete(NULL);
    }
}

static void example_task(void* p)
{

    initialize_vfs();
    get_partition_info();

#if (EXAMPLE_FRESH_START)
    example_print("example> deleting old databases.\r\n");
    unlink(DB_PATH); /* remove existing file */
#endif

    sqlite3_initialize();

    sqlite3* db;
    if (db_open(DB_PATH, &db)) {
        example_print("example> Failed to open %s. exiting task.\r\n", DB_PATH);
        goto cleanup;
    }

    int rc = db_exec(db, "CREATE TABLE IF NOT EXISTS info (boot_cnt INTEGER);", NULL, NULL);
    if (rc != SQLITE_OK) {
        example_print("example> Failed to create table. exiting task.\r\n");
        sqlite3_close(db);
        goto cleanup;
    }

    rc = db_exec(db, "CREATE TABLE IF NOT EXISTS log (time INTEGRE, log TEXT);", NULL, NULL);
    if (rc != SQLITE_OK) {
        example_print("example> Failed to create table. exiting task.\r\n");
        sqlite3_close(db);
        goto cleanup;
    }

    update_boot_count(db);

    uint32_t counter = 0;
    uint32_t last = 0;

    while (1) {
        example_print("------------------------------------------------\r\n");

        char sql_statement[100];
        snprintf(sql_statement, sizeof(sql_statement), "INSERT INTO log VALUES (%lld, 'counter = %d');", example_get_us(), counter++);
        rc = db_exec(db, sql_statement, NULL, NULL);
        if (rc != SQLITE_OK) {
            sqlite3_close(db);
            goto cleanup;
        }

        /* print number of records every minutes */
        const uint32_t now = example_get_ms();
        if (now - last > 1000 * 60) {
            last = now;
            uint32_t logs = 0;
            rc = db_exec(db, "SELECT COUNT(*) FROM log;", count_logs, &logs);
            if (rc != SQLITE_OK) {
                sqlite3_close(db);
                goto cleanup;
            }
            example_print("++++++++++++++++++++++++++++++++++++++++++++++++\r\n");
            example_print("example> Number of recorded logs: %d\r\n", logs);
            example_print("++++++++++++++++++++++++++++++++++++++++++++++++\r\n");
        }

        example_delay(10000);
    }

cleanup:
    sqlite3_shutdown();
    esp_vfs_littlefs_unregister(PARTITION_LABEL);
    example_print("example> FS unmounted\r\n");
    vTaskDelete(NULL);
}

static TaskHandle_t example_tsk;

void app_main(void)
{
    char name[] = "sqlite demo";
    xTaskCreatePinnedToCore(example_task, name, 8192, NULL, 10, &example_tsk, APP_CPU_NUM);
    example_assert(example_tsk != NULL);
    example_print("task> '%s' is created.\r\n", name);
}
