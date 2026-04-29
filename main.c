#include "mongoose.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// 1. Initialize Database in /tmp (Writable on Render)
void init_db() {
    sqlite3 *db;
    // Using /tmp/students.db ensures the app has permission to write data
    sqlite3_open("/tmp/students.db", &db);
    const char *sql = "CREATE TABLE IF NOT EXISTS students ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "unique_id TEXT, name TEXT, roll TEXT, branch TEXT);";
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
}

// 2. URL Encoder
void url_encode(const char *str, char *buf) {
    char *p = buf;
    while (*str) {
        if (isalnum((unsigned char)*str)) *p++ = *str;
        else { sprintf(p, "%%%02X", (unsigned char)*str); p += 3; }
        str++;
    }
    *p = '\0';
}

// 3. Web Event Handler
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // ROUTE: Home (Embedded HTML to prevent path errors)
        if (mg_match(hm->uri, mg_str("/"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: text/html\r\n",
                "<html><body style='font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background:#f0f2f5;margin:0;'>"
                "<form action='/register' method='POST' style='background:#fff;padding:40px;border-radius:10px;box-shadow:0 4px 10px rgba(0,0,0,0.1);width:300px;'>"
                "<h2>Register</h2>"
                "<input type='text' name='name' placeholder='Name' required style='width:100%;padding:10px;margin:10px 0;'><br>"
                "<input type='text' name='roll_no' placeholder='Roll' required style='width:100%;padding:10px;margin:10px 0;'><br>"
                "<button type='submit' style='width:100%;padding:10px;background:#1a2a6c;color:#fff;border:none;border-radius:5px;cursor:pointer;'>Generate</button>"
                "</form></body></html>");
        } 
        
        // ROUTE: Register
        else if (mg_match(hm->uri, mg_str("/register"), NULL)) {
            char name[100] = "User", roll[100] = "000";
            mg_http_get_var(&hm->body, "name", name, sizeof(name));
            mg_http_get_var(&hm->body, "roll_no", roll, sizeof(roll));

            srand(time(NULL));
            int uid = 100000 + (rand() % 900000);

            sqlite3 *db;
            if (sqlite3_open("/tmp/students.db", &db) == SQLITE_OK) {
                char query[512];
                snprintf(query, sizeof(query), "INSERT INTO students (unique_id, name, roll) VALUES ('%d', '%s', '%s');", uid, name, roll);
                sqlite3_exec(db, query, NULL, NULL, NULL);
                int last_id = (int)sqlite3_last_insert_rowid(db);
                sqlite3_close(db);

                // Using 303 See Other for better browser handling
                mg_http_reply(c, 303, "Location: /hallticket?id=%d\r\nContent-Length: 0\r\n\r\n", last_id);
            }
        }
        
        // ROUTE: Hall Ticket
        else if (mg_match(hm->uri, mg_str("/hallticket"), NULL)) {
            char id_str[10] = {0};
            mg_http_get_var(&hm->query, "id", id_str, sizeof(id_str));
            
            sqlite3 *db;
            sqlite3_stmt *res;
            sqlite3_open("/tmp/students.db", &db);
            char sql[128];
            snprintf(sql, sizeof(sql), "SELECT unique_id, name, roll FROM students WHERE id = %s", id_str);
            
            if (sqlite3_prepare_v2(db, sql, -1, &res, 0) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                const char *uid = (const char*)sqlite3_column_text(res, 0);
                const char *name = (const char*)sqlite3_column_text(res, 1);
                const char *roll = (const char*)sqlite3_column_text(res, 2);

                char raw_qr[256], encoded_qr[512];
                snprintf(raw_qr, sizeof(raw_qr), "UID:%s|Name:%s", uid, name);
                url_encode(raw_qr, encoded_qr);

                mg_http_reply(c, 200, "Content-Type: text/html\r\n", 
                    "<html><head><script src='https://cdnjs.cloudflare.com/ajax/libs/html2pdf.js/0.10.1/html2pdf.bundle.min.js'></script></head>"
                    "<body style='font-family:sans-serif;text-align:center;padding:50px;'>"
                    "<div id='t' style='border:2px solid #333;padding:40px;width:500px;margin:auto;position:relative;text-align:left;'>"
                    "<h2>HALL TICKET</h2><p>ID: %s</p><p>Name: %s</p><p>Roll: %s</p>"
                    "<div style='position:absolute;top:50px;right:40px;'><img src='https://api.qrserver.com/v1/create-qr-code/?size=100x100&data=%s' width='100'></div>"
                    "</div><br><button onclick='html2pdf().from(document.getElementById(\"t\")).save()'>Download PDF</button></body></html>", 
                    uid, name, roll, encoded_qr);
            }
            sqlite3_finalize(res);
            sqlite3_close(db);
        }
    }
}

int main() {
    init_db();
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char *port = getenv("PORT");
    if (!port) port = "18080";
    char url[64];
    snprintf(url, sizeof(url), "http://0.0.0.0:%s", port);
    printf("Server live on %s\n", url);
    mg_http_listen(&mgr, url, fn, NULL);
    for (;;) mg_mgr_poll(&mgr, 1000);
    return 0;
}