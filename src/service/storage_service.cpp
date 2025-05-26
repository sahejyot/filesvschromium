#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <base/task/thread_pool.h>
#include <base/synchronization/waitable_event.h>
#include <third_party/boringssl/src/include/openssl/sha.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\cpp-httplib\httplib.h"
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <C:/Users/Qikfox/Desktop/modular-chromium-threading/src/base/task/thread_pool/thread_pool_instance.h>
#include <fstream>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\json\single_include\nlohmann\json.hpp"
#include "database_initializer.h"


constexpr char kDataFolder[] = "data";
constexpr char kDbPath[] = "data/storage.db";
constexpr char kLogFile[] = "data/storage.log";
constexpr int kStoragePort = 8081;

constexpr char kStorageServerCert[] = "C:/Users/Qikfox/Desktop/modular-chromium-threading/src/service/certs/main_service/server.crt";
constexpr char kStorageServerKey[] = "C:/Users/Qikfox/Desktop/modular-chromium-threading/src/service/certs/main_service/server.key";

int main() {
    sqlite3* db = initialize_database(kDbPath, kDataFolder, kLogFile);
    if (!db) {
        std::cerr << "Failed to initialize database" << std::endl;
        return 1;
    }

    httplib::SSLServer svr(kStorageServerCert, kStorageServerKey);

    svr.Get("/check_cid", [db](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::steady_clock::now();

        if (!req.has_param("cid")) {
            res.set_content("{\"error\": \"Missing cid parameter\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string cid = req.get_param_value("cid");
        sqlite3_stmt* stmt;
        const char* query = "SELECT ref_count FROM metadata WHERE cid = ?;";
        int rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content("{\"error\": \"Failed to prepare query\"}", "application/json");
            res.status = 500;
            return;
        }

        sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
        bool exists = false;
        int ref_count = 0;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
            ref_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);

        nlohmann::json response;
        if (exists) {
            const char* update = "UPDATE metadata SET ref_count = ref_count + 1 WHERE cid = ?;";
            rc = sqlite3_prepare_v2(db, update, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                res.set_content("{\"error\": \"Failed to prepare update\"}", "application/json");
                res.status = 500;
                return;
            }

            sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) {
                res.set_content("{\"error\": \"Failed to increment ref_count\"}", "application/json");
                res.status = 500;
                return;
            }

            response["exists"] = true;
            response["ref_count"] = ref_count + 1;
        } else {
            response["exists"] = false;
        }

        res.set_content(response.dump(), "application/json");
        res.status = 200;

        auto end = std::chrono::steady_clock::now();
        std::cout << "check_cid took " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
    });

    svr.Post("/store_chunk", [db](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::steady_clock::now();

        if (!req.is_multipart_form_data()) {
            res.set_content("{\"error\": \"Expected multipart form data\"}", "application/json");
            res.status = 400;
            return;
        }

        if (!req.has_file("hash") || !req.has_file("chunk")) {
            res.set_content("{\"error\": \"Missing hash or chunk in request\"}", "application/json");
            res.status = 400;
            return;
        }

        const auto& hash_part = req.get_file_value("hash");
        const auto& chunk_part = req.get_file_value("chunk");

        if (hash_part.content.empty() || chunk_part.content.empty()) {
            res.set_content("{\"error\": \"Empty hash or chunk data\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string hash = hash_part.content;
        std::vector<uint8_t> chunk_data(chunk_part.content.begin(), chunk_part.content.end());

        sqlite3_stmt* stmt;
        const char* query = "SELECT ref_count FROM chunk_references WHERE chunk_hash = ?;";
        int rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content("{\"error\": \"Failed to prepare query\"}", "application/json");
            res.status = 500;
            return;
        }

        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
        bool chunk_exists = false;
        int ref_count = 0;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            chunk_exists = true;
            ref_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);

        if (chunk_exists) {
            const char* update = "UPDATE chunk_references SET ref_count = ref_count + 1 WHERE chunk_hash = ?;";
            rc = sqlite3_prepare_v2(db, update, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                res.set_content("{\"error\": \"Failed to prepare update\"}", "application/json");
                res.status = 500;
                return;
            }

            sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) {
                res.set_content("{\"error\": \"Failed to increment ref_count\"}", "application/json");
                res.status = 500;
                return;
            }
        } else {
            std::filesystem::create_directory(kDataFolder);
            std::string filename = std::string(kDataFolder) + "/" + hash + ".chunk";
            std::ofstream out_file(filename, std::ios::binary);
            if (!out_file) {
                res.set_content("{\"error\": \"Failed to write chunk to file\"}", "application/json");
                res.status = 500;
                return;
            }
            out_file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
            out_file.close();

            const char* insert = "INSERT INTO chunk_references (chunk_hash, ref_count) VALUES (?, 1);";
            rc = sqlite3_prepare_v2(db, insert, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                res.set_content("{\"error\": \"Failed to prepare insert\"}", "application/json");
                res.status = 500;
                return;
            }

            sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) {
                res.set_content("{\"error\": \"Failed to insert chunk reference\"}", "application/json");
                res.status = 500;
                return;
            }
        }

        std::cout << "Stored chunk with hash: " << hash << ", size: " << chunk_data.size() << " bytes, ref_count: " << (chunk_exists ? ref_count + 1 : 1) << std::endl;

        nlohmann::json response;
        response["status"] = "success";
        response["hash"] = hash;
        res.set_content(response.dump(), "application/json");
        res.status = 200;

        auto end = std::chrono::steady_clock::now();
        std::cout << "store_chunk took " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
    });

    svr.Post("/store_metadata", [db](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::steady_clock::now();

        nlohmann::json metadata;
        try {
            metadata = nlohmann::json::parse(req.body);
        } catch (const std::exception& e) {
            res.set_content("{\"error\": \"Invalid JSON\"}", "application/json");
            res.status = 400;
            return;
        }

        if (!metadata.contains("cid") || !metadata.contains("filename") || !metadata.contains("file_type") ||
            !metadata.contains("file_size") || !metadata.contains("chunk_hashes")) {
            res.set_content("{\"error\": \"Missing required fields\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string cid = metadata["cid"];
        std::string filename = metadata["filename"];
        std::string file_type = metadata["file_type"];
        int64_t file_size = metadata["file_size"];
        std::vector<std::string> chunk_hashes = metadata["chunk_hashes"];

        sqlite3_stmt* stmt;
        const char* insert_metadata = "INSERT INTO metadata (cid, filename, file_type, file_size, ref_count) VALUES (?, ?, ?, ?, 1);";
        int rc = sqlite3_prepare_v2(db, insert_metadata, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content("{\"error\": \"Failed to prepare metadata insert\"}", "application/json");
            res.status = 500;
            return;
        }

        sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, file_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, file_size);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            res.set_content("{\"error\": \"Failed to insert metadata\"}", "application/json");
            res.status = 500;
            return;
        }

        const char* insert_chunk_hash = "INSERT INTO chunk_hashes (cid, chunk_index, chunk_hash) VALUES (?, ?, ?);";
        for (size_t i = 0; i < chunk_hashes.size(); ++i) {
            rc = sqlite3_prepare_v2(db, insert_chunk_hash, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                res.set_content("{\"error\": \"Failed to prepare chunk hash insert\"}", "application/json");
                res.status = 500;
                return;
            }

            sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, static_cast<int>(i));
            sqlite3_bind_text(stmt, 3, chunk_hashes[i].c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) {
                res.set_content("{\"error\": \"Failed to insert chunk hash at index " + std::to_string(i) + "\"}", "application/json");
                res.status = 500;
                return;
            }
        }

        nlohmann::json response;
        response["status"] = "success";
        res.set_content(response.dump(), "application/json");
        res.status = 200;

        auto end = std::chrono::steady_clock::now();
        std::cout << "store_metadata took " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
    });

    svr.Get("/get", [db](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::steady_clock::now();

        if (!req.has_param("cid")) {
            res.set_content("{\"error\": \"Missing cid parameter\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string cid = req.get_param_value("cid");

        sqlite3_stmt* stmt;
        const char* query_metadata = "SELECT filename, file_type, file_size FROM metadata WHERE cid = ?;";
        int rc = sqlite3_prepare_v2(db, query_metadata, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content("{\"error\": \"Failed to prepare metadata query\"}", "application/json");
            res.status = 500;
            return;
        }

        sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            res.set_content("{\"error\": \"File not found\"}", "application/json");
            res.status = 404;
            return;
        }

        std::string filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string file_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int64_t file_size = sqlite3_column_int64(stmt, 2);
        sqlite3_finalize(stmt);

        std::vector<std::string> chunk_hashes;
        const char* query_chunks = "SELECT chunk_hash FROM chunk_hashes WHERE cid = ? ORDER BY chunk_index;";
        rc = sqlite3_prepare_v2(db, query_chunks, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            res.set_content("{\"error\": \"Failed to prepare chunk query\"}", "application/json");
            res.status = 500;
            return;
        }

        sqlite3_bind_text(stmt, 1, cid.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            chunk_hashes.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);

        std::vector<uint8_t> file_data;
        for (const auto& hash : chunk_hashes) {
            std::string chunk_path = std::string(kDataFolder) + "/" + hash + ".chunk";
            std::ifstream in_file(chunk_path, std::ios::binary);
            if (!in_file) {
                res.set_content("{\"error\": \"Failed to read chunk with hash " + hash + "\"}", "application/json");
                res.status = 500;
                return;
            }

            std::vector<uint8_t> chunk_data((std::istreambuf_iterator<char>(in_file)), std::istreambuf_iterator<char>());
            in_file.close();
            file_data.insert(file_data.end(), chunk_data.begin(), chunk_data.end());
        }

        res.set_content(std::string(file_data.begin(), file_data.end()), file_type);
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        res.status = 200;

        auto end = std::chrono::steady_clock::now();
        std::cout << "get took " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
    });

    std::cout << "Storage server listening on port " << kStoragePort << "..." << std::endl;
    svr.listen("0.0.0.0", kStoragePort);

    sqlite3_close(db);
    return 0;
}