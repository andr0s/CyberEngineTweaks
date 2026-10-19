// Unit tests for src/platform/macos/PathJail.h (the macOS mod file-access jail).
// Build: clang++ -std=c++20 -I../../src PathJailTests.cpp -o PathJailTests

#include <platform/macos/PathJail.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

static int s_failures = 0;

static void Expect(const bool aCondition, const char* aWhat)
{
    std::printf("%s %s\n", aCondition ? "PASS" : "FAIL", aWhat);
    if (!aCondition)
        ++s_failures;
}

static void Touch(const fs::path& aPath)
{
    std::ofstream(aPath) << "x";
}

int main()
{
    char tmpl[] = "/tmp/cet-pathjail-XXXXXX";
    const fs::path base = mkdtemp(tmpl);
    const auto root = base / "mods" / "mymod";
    const auto outside = base / "outside";
    fs::create_directories(root / "data");
    fs::create_directories(outside);
    Touch(root / "init.lua");
    Touch(root / "data" / "a.json");
    Touch(outside / "secret.txt");
    fs::create_directory_symlink(outside, root / "link_out");
    fs::create_symlink(root / "init.lua", root / "data" / "link_file");

    const auto inside = [&root](const fs::path& aPath)
    {
        return !aPath.empty() && PathJail::IsInside(aPath, fs::weakly_canonical(root));
    };

    Expect(inside(PathJail::Resolve("init.lua", root, false)), "plain file inside the mod");
    Expect(inside(PathJail::Resolve("data\\a.json", root, false)), "Windows separators are accepted");
    Expect(inside(PathJail::Resolve("DATA/A.JSON", root, false)), "case-insensitive match (APFS)");
    Expect(inside(PathJail::Resolve("nothere/../init.lua", root, false)), "'..' that stays inside");
    Expect(inside(PathJail::Resolve(".", root, false)), "the mod folder itself");
    Expect(inside(PathJail::Resolve("new.txt", root, true)), "new file allowed when non-existing is allowed");

    Expect(PathJail::Resolve("new.txt", root, false).empty(), "missing file refused when it must exist");
    Expect(PathJail::Resolve("", root, true).empty(), "empty path refused");
    Expect(PathJail::Resolve("../outside/secret.txt", root, true).empty(), "'..' escape refused");
    Expect(PathJail::Resolve("..\\..\\outside\\secret.txt", root, true).empty(), "'..\\' escape refused");
    Expect(PathJail::Resolve("data/../../../outside/secret.txt", root, true).empty(), "nested '..' escape refused");
    Expect(PathJail::Resolve("data/../..", root, true).empty(), "parent folder refused");
    Expect(PathJail::Resolve("/etc/passwd", root, true).empty(), "absolute path refused");
    Expect(PathJail::Resolve((outside / "secret.txt").string(), root, true).empty(), "absolute path to outside refused");
    Expect(PathJail::Resolve("link_out/secret.txt", root, true).empty(), "symlinked folder pointing outside refused");
    Expect(PathJail::Resolve("link_out/new.txt", root, true).empty(), "new file through symlinked folder refused");
    Expect(PathJail::Resolve("data/link_file", root, true).empty(), "symlink as the file itself refused");

    Expect(PathJail::IsSamePath(fs::path("/a/b/DB.SQLITE3"), fs::path("/a/b/db.sqlite3")), "IsSamePath ignores case");
    Expect(!PathJail::IsSamePath(fs::path(), fs::path("/a")), "IsSamePath rejects empty");

    fs::remove_all(base);
    std::printf("%d failure(s)\n", s_failures);
    return s_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
