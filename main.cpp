#include "crow_all.h"
#include <sqlite3.h>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random> // For Unique ID

// Helper for URL safety
std::string url_encode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// Function to generate a random 6-digit Unique ID
std::string generate_unique_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    return std::to_string(dis(gen));
}

void init_db() {
    sqlite3* db;
    sqlite3_open("students.db", &db);
    // Added unique_id column to the table
    std::string sql = "CREATE TABLE IF NOT EXISTS students ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "unique_id TEXT,"
                      "name TEXT, roll_no TEXT, branch TEXT, subjects TEXT);";
    sqlite3_exec(db, sql.c_str(), 0, 0, 0);
    sqlite3_close(db);
}

int main() {
    crow::SimpleApp app;
    init_db();

    CROW_ROUTE(app, "/").methods(crow::HTTPMethod::Get)([](){
        return crow::response(crow::mustache::load("form.html").render());
    });

    CROW_ROUTE(app, "/register").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto x = crow::query_string("?" + req.body); 
        std::string name = x.get("name") ? x.get("name") : "Student";
        std::string roll = x.get("roll_no") ? x.get("roll_no") : "N/A";
        std::string branch = x.get("branch") ? x.get("branch") : "General";
        std::string uid = generate_unique_id(); // Generate the Random ID

        sqlite3* db;
        sqlite3_open("students.db", &db);
        std::string sql = "INSERT INTO students (unique_id, name, roll_no, branch, subjects) VALUES ('" 
                          + uid + "', '" + name + "', '" + roll + "', '" + branch + "', 'Mathematics, Physics, Computing');";
        sqlite3_exec(db, sql.c_str(), 0, 0, 0);
        int last_id = (int)sqlite3_last_insert_rowid(db);
        sqlite3_close(db);

        crow::response res;
        res.code = 302;
        res.set_header("Location", "/hallticket/" + std::to_string(last_id));
        return res;
    });

    CROW_ROUTE(app, "/hallticket/<int>").methods(crow::HTTPMethod::Get)
    ([](int id){
        sqlite3* db;
        sqlite3_stmt* stmt;
        sqlite3_open("students.db", &db);
        std::string sql = "SELECT unique_id, name, roll_no, branch, subjects FROM students WHERE id = " + std::to_string(id);
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::mustache::context ctx;
            std::string s_uid = (const char*)sqlite3_column_text(stmt, 0);
            std::string s_name = (const char*)sqlite3_column_text(stmt, 1);
            std::string s_roll = (const char*)sqlite3_column_text(stmt, 2);

            ctx["uid"] = s_uid;
            ctx["name"] = s_name;
            ctx["roll"] = s_roll;
            ctx["branch"] = (const char*)sqlite3_column_text(stmt, 3);
            ctx["subjects"] = (const char*)sqlite3_column_text(stmt, 4);
            
            // QR data now includes the Unique ID for scanning verification
            std::string raw_data = "UID: " + s_uid + " | Student: " + s_name + " | Roll: " + s_roll;
            ctx["qr_url"] = "https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" + url_encode(raw_data);

            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return crow::response(crow::mustache::load("hallticket.html").render(ctx));
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return crow::response(404, "Ticket Not Found");
    });

    char* port_env = getenv("PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::stoi(port_env) : 18080);
    app.port(port).bindaddr("0.0.0.0").multithreaded().run();
}