#include "crow_all.h"
#include <sqlite3.h>
#include <string>
#include <iostream>

void init_db() {
    sqlite3* db;
    sqlite3_open("students.db", &db);
    std::string sql = "CREATE TABLE IF NOT EXISTS students ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "name TEXT, roll_no TEXT, branch TEXT, subjects TEXT);";
    sqlite3_exec(db, sql.c_str(), 0, 0, 0);
    sqlite3_close(db);
}

int main() {
    crow::SimpleApp app;
    init_db();

    // 1. Home Page
    CROW_ROUTE(app, "/")([](){
        return crow::response(crow::mustache::load("form.html").render());
    });

    // 2. Handle Registration (POST)
    CROW_ROUTE(app, "/register").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto x = crow::query_string("?" + req.body); 
        
        std::string name = x.get("name") ? x.get("name") : "Student";
        std::string roll = x.get("roll_no") ? x.get("roll_no") : "N/A";
        std::string branch = x.get("branch") ? x.get("branch") : "General";
        std::string subjects = "Mathematics, Physics, Computing"; 

        sqlite3* db;
        sqlite3_open("students.db", &db);
        std::string sql = "INSERT INTO students (name, roll_no, branch, subjects) VALUES ('" 
                          + name + "', '" + roll + "', '" + branch + "', '" + subjects + "');";
        
        sqlite3_exec(db, sql.c_str(), 0, 0, 0);
        int last_id = (int)sqlite3_last_insert_rowid(db);
        sqlite3_close(db);

        crow::response res;
        // Redirect to ID without trailing slash
        res.redirect("/hallticket/" + std::to_string(last_id));
        return res;
    });

    // 3. Hall Ticket Page (GET)
    CROW_ROUTE(app, "/hallticket/<int>").methods(crow::HTTPMethod::Get)
    ([](int id){
        sqlite3* db;
        sqlite3_stmt* stmt;
        sqlite3_open("students.db", &db);
        
        std::string sql = "SELECT name, roll_no, branch, subjects FROM students WHERE id = " + std::to_string(id);
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::mustache::context ctx;
            std::string s_name = (const char*)sqlite3_column_text(stmt, 0);
            std::string s_roll = (const char*)sqlite3_column_text(stmt, 1);

            ctx["name"] = s_name;
            ctx["roll"] = s_roll;
            ctx["branch"] = (const char*)sqlite3_column_text(stmt, 2);
            ctx["subjects"] = (const char*)sqlite3_column_text(stmt, 3);
            
            std::string qr_data = "Student:" + s_name + "|Roll:" + s_roll;
            ctx["qr_url"] = "https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" + qr_data;

            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return crow::response(crow::mustache::load("hallticket.html").render(ctx));
        }
        
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return crow::response(404, "Student Not Found");
    });

    char* port_env = getenv("PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::stoi(port_env) : 18080);
    app.port(port).bindaddr("0.0.0.0").multithreaded().run();
}