#include <sqlite3.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

namespace fs = std::filesystem;

// Logs a message to the specified log file, optionally with a duration.
void log_message(const std::string& log_file, const std::string& message, double duration = -1.0) {
    fs::create_directories(fs::path(log_file).parent_path());
    std::ofstream log_stream(log_file, std::ios::app);
    if (!log_stream) {
        return;
    }

    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " - " << message;
    if (duration >= 0) {
        ss << " (duration: " << std::fixed << std::setprecision(3) << duration << "s)";
    }
    ss << std::endl;
    log_stream << ss.str();
    log_stream.close();
}

// Initializes the SQLite database and creates the necessary tables.
sqlite3* initialize_database(const std::string& db_path, const std::string& data_dir, const std::string& log_file) {
    log_message(log_file, "Initializing database at " + db_path);
    auto start = std::chrono::steady_clock::now();

    fs::create_directories(data_dir);
    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        log_message(log_file, "Failed to open database: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        throw std::runtime_error("Failed to open database");
    }

    const char* create_metadata_table =
        "CREATE TABLE IF NOT EXISTS metadata ("
        "cid TEXT PRIMARY KEY,"
        "filename TEXT NOT NULL,"
        "file_type TEXT NOT NULL,"
        "file_size INTEGER NOT NULL,"
        "ref_count INTEGER NOT NULL);";

    rc = sqlite3_exec(db, create_metadata_table, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_message(log_file, "Failed to create metadata table: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        throw std::runtime_error("Failed to create metadata table");
    }

    const char* create_chunk_references_table =
        "CREATE TABLE IF NOT EXISTS chunk_references ("
        "chunk_hash TEXT PRIMARY KEY,"
        "ref_count INTEGER NOT NULL);";

    rc = sqlite3_exec(db, create_chunk_references_table, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_message(log_file, "Failed to create chunk_references table: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        throw std::runtime_error("Failed to create chunk_references table");
    }

    const char* create_chunk_hashes_table =
        "CREATE TABLE IF NOT EXISTS chunk_hashes ("
        "cid TEXT,"
        "chunk_index INTEGER,"
        "chunk_hash TEXT,"
        "PRIMARY KEY (cid, chunk_index),"
        "FOREIGN KEY (cid) REFERENCES metadata(cid),"
        "FOREIGN KEY (chunk_hash) REFERENCES chunk_references(chunk_hash));";

    rc = sqlite3_exec(db, create_chunk_hashes_table, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_message(log_file, "Failed to create chunk_hashes table: " + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        throw std::runtime_error("Failed to create chunk_hashes table");
    }

    auto end = std::chrono::steady_clock::now();
    log_message(log_file, "Database initialized successfully", std::chrono::duration<double>(end - start).count());
    return db;
}