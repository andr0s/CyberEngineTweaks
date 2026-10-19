// Unit tests for src/lsqlite3/authorizer.h (what SQL a mod's database may run).
// Build: clang++ -std=c++20 -I../../src/lsqlite3 SqliteAuthorizerTests.cpp -lsqlite3 -o SqliteAuthorizerTests

#include <authorizer.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static int s_failures = 0;

static void Expect(const bool aCondition, const std::string& aWhat)
{
    std::printf("%s %s\n", aCondition ? "PASS" : "FAIL", aWhat.c_str());
    if (!aCondition)
        ++s_failures;
}

static int Exec(sqlite3* apDb, const std::string& acSql)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(apDb, acSql.c_str(), nullptr, nullptr, &error);
    sqlite3_free(error);
    return rc;
}

int main()
{
    char tmpl[] = "/tmp/cet-sqlite-XXXXXX";
    const fs::path dir = mkdtemp(tmpl);
    const auto dbPath = dir / "db.sqlite3";
    const auto target = dir / "elsewhere.db";

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
        return EXIT_FAILURE;
    sqlite3_set_authorizer(db, lsqlite_authorizer, nullptr);

    Expect(Exec(db, "CREATE TABLE t(a); INSERT INTO t VALUES (1); SELECT a FROM t;") == SQLITE_OK, "ordinary SQL works");
    Expect(Exec(db, "ATTACH DATABASE '" + target.string() + "' AS other;") != SQLITE_OK, "ATTACH of a file refused");
    Expect(!fs::exists(target), "...and the file was not created");
    Expect(Exec(db, "ATTACH DATABASE ('" + dir.string() + "' || '/x.db') AS other;") != SQLITE_OK, "ATTACH of a computed name refused");
    Expect(Exec(db, "ATTACH DATABASE '' AS tmp1; DETACH DATABASE tmp1;") == SQLITE_OK, "ATTACH of a temporary database allowed");
    Expect(Exec(db, "ATTACH DATABASE ':memory:' AS mem1; DETACH DATABASE mem1;") == SQLITE_OK, "ATTACH of :memory: allowed");
    Expect(Exec(db, "VACUUM INTO '" + target.string() + "';") != SQLITE_OK, "VACUUM INTO a file refused");
    Expect(!fs::exists(target), "...and the file was not created");
    Expect(Exec(db, "VACUUM;") == SQLITE_OK, "plain VACUUM allowed");
    Expect(Exec(db, "PRAGMA temp_store_directory = '" + dir.string() + "';") != SQLITE_OK, "PRAGMA temp_store_directory refused");
    Expect(Exec(db, "PRAGMA user_version = 3;") == SQLITE_OK, "other PRAGMAs allowed");
    Expect(Exec(db, "SELECT load_extension('" + (dir / "libnothing.dylib").string() + "');") != SQLITE_OK, "load_extension() refused");

    sqlite3_close(db);
    fs::remove_all(dir);
    std::printf("%d failure(s)\n", s_failures);
    return s_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
