#include "mongoose.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// 1. Initialize Database
void init_db() {
    sqlite3 *db;
    sqlite3_open("students.db", &db);
    const char *sql = "CREATE TABLE IF NOT EXISTS students ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "unique_id TEXT, name TEXT, roll TEXT, branch TEXT);";
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
}

// 2. URL Encoder for QR Code
void url_encode(const char *str, char *buf) {
    char *p = buf;
    while (*str) {
        if (isalnum((unsigned char)*str) || *str == '-' || *str == '_' || *str == '.' || *str == '~') {
            *p++ = *str;
        } else {
            sprintf(p, "%%%02X", (unsigned char)*str);
            p += 3;
        }
        str++;
    }
    *p = '\0';
}

// 3. Web Event Handler
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // ROUTE: Home
        if (mg_match(hm->uri, mg_str("/"), NULL)) {
            struct mg_http_serve_opts opts = {.root_dir = "."};
            mg_http_serve_file(c, hm, "templates/form.html", &opts);
        } 
        
        // ROUTE: Register (POST)
        else if (mg_match(hm->uri, mg_str("/register"), NULL)) {
            char name[100] = {0}, roll[100] = {0}, branch[100] = {0};
            
            mg_http_get_var(&hm->body, "name", name, sizeof(name));
            mg_http_get_var(&hm->body, "roll_no", roll, sizeof(roll));
            mg_http_get_var(&hm->body, "branch", branch, sizeof(branch));

            srand(time(NULL));
            int uid = 100000 + (rand() % 900000);

            sqlite3 *db;
            sqlite3_open("students.db", &db);
            char query[512];
            snprintf(query, sizeof(query), "INSERT INTO students (unique_id, name, roll, branch) VALUES ('%d', '%s', '%s', '%s');", 
                    uid, name, roll, branch);
            sqlite3_exec(db, query, NULL, NULL, NULL);
            int last_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_close(db);

            // Fix for hanging: 303 Redirect with explicit headers
            mg_http_reply(c, 303, "Location: /hallticket?id=%d\r\nContent-Length: 0\r\n\r\n", last_id);
        }
        
        // ROUTE: Hall Ticket
        else if (mg_match(hm->uri, mg_str("/hallticket"), NULL)) {
            char id_str[10] = {0};
            mg_http_get_var(&hm->query, "id", id_str, sizeof(id_str));
            
            sqlite3 *db;
            sqlite3_stmt *res;
            sqlite3_open("students.db", &db);
            char sql[128];
            snprintf(sql, sizeof(sql), "SELECT unique_id, name, roll, branch FROM students WHERE id = %s", id_str);
            
            if (sqlite3_prepare_v2(db, sql, -1, &res, 0) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                const char *uid = (const char*)sqlite3_column_text(res, 0);
                const char *name = (const char*)sqlite3_column_text(res, 1);
                const char *roll = (const char*)sqlite3_column_text(res, 2);
                const char *branch = (const char*)sqlite3_column_text(res, 3);

                char raw_qr[256], encoded_qr[512];
                snprintf(raw_qr, sizeof(raw_qr), "UID:%s|Student:%s|Roll:%s", uid, name, roll);
                url_encode(raw_qr, encoded_qr);

                mg_http_reply(c, 200, "Content-Type: text/html\r\n", 
                    "<!DOCTYPE html><html><head><title>Hall Ticket</title>"
                    "<script src='https://cdnjs.cloudflare.com/ajax/libs/html2pdf.js/0.10.1/html2pdf.bundle.min.js'></script>"
                    "<style>"
                    "body{font-family:sans-serif; background:#f0f2f5; padding:40px; text-align:center;}"
                    ".ticket{background:#fff; width:600px; margin:auto; padding:40px; border:2px solid #1a2a6c; text-align:left; position:relative; box-shadow:0 4px 15px rgba(0,0,0,0.2); border-radius:10px;}"
                    "h2{text-align:center; color:#1a2a6c; border-bottom:2px solid #1a2a6c; padding-bottom:10px;}"
                    ".qr{position:absolute; top:90px; right:40px; text-align:center;}"
                    "button{padding:12px 24px; background:#1a2a6c; color:#fff; border:none; border-radius:5px; cursor:pointer; margin-bottom:20px; font-weight:bold;}"
                    "p{font-size:18px; margin:12px 0;}"
                    "</style></head><body>"
                    "<button onclick='dl()'>Download Hall Ticket (PDF)</button>"
                    "<div id='t' class='ticket'><h2>EXAM HALL TICKET</h2>"
                    "<p><b>Candidate ID:</b> %s</p>"
                    "<p><b>Student Name:</b> %s</p>"
                    "<p><b>Roll Number:</b> %s</p>"
                    "<p><b>Branch:</b> %s</p>"
                    "<p><b>Subjects:</b> Core Engineering Subjects</p>"
                    "<div class='qr'><img src='https://api.qrserver.com/v1/create-qr-code/?size=140x140&data=%s' width='140'><br><small>VERIFY QR</small></div>"
                    "<hr><p style='font-size:12px; text-align:center;'>Computer Generated Document - Verify via QR Code.</p>"
                    "</div>"
                    "<script>function dl(){const e=document.getElementById('t'); html2pdf().set({margin:10, filename:'HallTicket_%s.pdf', html2canvas:{useCORS:true, scale:2}}).from(e).save();}</script>"
                    "</body></html>", uid, name, roll, branch, encoded_qr, uid);
            } else {
                mg_http_reply(c, 404, "", "Student Record Not Found");
            }
            sqlite3_finalize(res);
            sqlite3_close(db);
        }
    }
}

// 4. Main Function
int main() {
    init_db();
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    char *port = getenv("PORT");
    if (!port) port = "18080";
    char url[64];
    snprintf(url, sizeof(url), "http://0.0.0.0:%s", port);

    printf("Starting C Server on %s\n", url);
    mg_http_listen(&mgr, url, fn, NULL);
    for (;;) mg_mgr_poll(&mgr, 1000);
    
    return 0;
}