#include "mongoose.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// 1. Initialize Database in /tmp
void init_db() {
    sqlite3 *db;
    sqlite3_open("/tmp/students.db", &db);
    const char *sql = "CREATE TABLE IF NOT EXISTS students ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "unique_id TEXT, name TEXT, roll TEXT, branch TEXT, subjects TEXT);";
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

// 3. Event Handler
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // --- HOME ROUTE (The Form) ---
        if (mg_match(hm->uri, mg_str("/"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: text/html\r\n",
                "<html><body style='font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background:#f4f7f6;margin:0;'>"
                "<form action='/register' method='POST' style='background:#fff;padding:35px;border-radius:12px;box-shadow:0 8px 20px rgba(0,0,0,0.1);width:350px;'>"
                "<h2 style='text-align:center;color:#1a2a6c;'>Registration</h2>"
                "<label>Full Name</label><input type='text' name='name' required style='width:100%%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:5px;'>"
                "<label>Roll Number</label><input type='text' name='roll_no' required style='width:100%%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:5px;'>"
                "<label>Branch</label><select name='branch' style='width:100%%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:5px;'><option>CS</option><option>IT</option><option>ECE</option><option>ME</option></select>"
                "<label>Subjects</label><input type='text' name='subjects' value='Maths, Physics, Computing' style='width:100%%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:5px;'>"
                "<button type='submit' style='width:100%%;padding:12px;background:#1a2a6c;color:#fff;border:none;border-radius:5px;cursor:pointer;margin-top:15px;'>Generate Hall Ticket</button>"
                "</form></body></html>");
        } 
        
        // --- REGISTER ROUTE ---
        else if (mg_match(hm->uri, mg_str("/register"), NULL)) {
            char name[100]="", roll[100]="", branch[100]="", subs[100]="";
            mg_http_get_var(&hm->body, "name", name, sizeof(name));
            mg_http_get_var(&hm->body, "roll_no", roll, sizeof(roll));
            mg_http_get_var(&hm->body, "branch", branch, sizeof(branch));
            mg_http_get_var(&hm->body, "subjects", subs, sizeof(subs));

            srand(time(NULL));
            int uid = 100000 + (rand() % 900000);

            sqlite3 *db;
            sqlite3_open("/tmp/students.db", &db);
            char q[1024];
            snprintf(q, sizeof(q), "INSERT INTO students (unique_id, name, roll, branch, subjects) VALUES ('%d','%s','%s','%s','%s');", uid, name, roll, branch, subs);
            sqlite3_exec(db, q, NULL, NULL, NULL);
            int last_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_close(db);

            // JS-Based Redirect: Fixes the "loading" hang issue
            mg_http_reply(c, 200, "Content-Type: text/html\r\n", 
                "<html><body><script>window.location.href='/hallticket?id=%d';</script></body></html>", last_id);
        }
        
        // --- HALL TICKET ROUTE ---
        else if (mg_match(hm->uri, mg_str("/hallticket"), NULL)) {
            char id_str[10] = {0};
            mg_http_get_var(&hm->query, "id", id_str, sizeof(id_str));
            
            sqlite3 *db;
            sqlite3_stmt *res;
            sqlite3_open("/tmp/students.db", &db);
            char sql[256];
            snprintf(sql, sizeof(sql), "SELECT unique_id, name, roll, branch, subjects FROM students WHERE id = %s", id_str);
            
            if (sqlite3_prepare_v2(db, sql, -1, &res, 0) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                const char *uid = (const char*)sqlite3_column_text(res, 0);
                const char *name = (const char*)sqlite3_column_text(res, 1);
                const char *roll = (const char*)sqlite3_column_text(res, 2);
                const char *branch = (const char*)sqlite3_column_text(res, 3);
                const char *subs = (const char*)sqlite3_column_text(res, 4);

                char raw_qr[256], enc_qr[512];
                snprintf(raw_qr, sizeof(raw_qr), "UID:%s|Name:%s", uid, name);
                url_encode(raw_qr, enc_qr);

                mg_http_reply(c, 200, "Content-Type: text/html\r\n", 
                    "<html><head><script src='https://cdnjs.cloudflare.com/ajax/libs/html2pdf.js/0.10.1/html2pdf.bundle.min.js'></script>"
                    "<style>body{font-family:sans-serif;text-align:center;padding:40px;background:#eee;}"
                    ".ticket{background:#fff;width:600px;margin:auto;padding:40px;border:2px solid #1a2a6c;text-align:left;position:relative;box-shadow:0 5px 15px rgba(0,0,0,0.2);}"
                    "h2{text-align:center;color:#1a2a6c;border-bottom:2px solid #1a2a6c;padding-bottom:10px;}"
                    ".qr{position:absolute;top:100px;right:40px;text-align:center;}"
                    "button{padding:12px 25px;background:#1a2a6c;color:#fff;border:none;border-radius:5px;cursor:pointer;margin-bottom:20px;font-weight:bold;}"
                    "</style></head><body>"
                    "<button onclick='html2pdf().set({margin:10,filename:\"Ticket.pdf\",html2canvas:{useCORS:true,scale:2}}).from(document.getElementById(\"t\")).save()'>Download PDF</button>"
                    "<div id='t' class='ticket'><h2>EXAM HALL TICKET</h2>"
                    "<p><b>Candidate ID:</b> %s</p><p><b>Name:</b> %s</p><p><b>Roll:</b> %s</p>"
                    "<p><b>Branch:</b> %s</p><p><b>Subjects:</b> %s</p>"
                    "<div class='qr'><img src='https://api.qrserver.com/v1/create-qr-code/?size=130x130&data=%s' width='130'><br><small>VERIFY</small></div>"
                    "<hr><p style='font-size:11px;text-align:center;'>Computer Generated - Verify via QR Code</p></div></body></html>", 
                    uid, name, roll, branch, subs, enc_qr);
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
    printf("Server running on %s\n", url);
    mg_http_listen(&mgr, url, fn, NULL);
    for (;;) mg_mgr_poll(&mgr, 1000);
    return 0;
}