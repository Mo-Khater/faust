#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

namespace fs = std::filesystem;

static const char* base  = "https://github.com";
static const char* owner = "Mo-Khater";

struct PkgSpec {
    std::string name;
    std::string version;
};

static void print_available_services()
{
    std::cout
        << "faustpkg install <name==version> [--repo <owner/repo>] [--cache <path>]\n"
        << "faustpkg list [--cache <path>]\n"
        << "faustpkg remove <name==version> [--cache <path>]\n";
}

static bool split_spec(const std::string& spec, PkgSpec& out)
{
    const std::string sep = "==";
    size_t pos = spec.find(sep);
    if (pos == std::string::npos || pos == 0 || pos + sep.size() >= spec.size()) {
        return false;
    }
    out.name = spec.substr(0, pos);
    out.version = spec.substr(pos + sep.size());
    return true;
}

static std::string join_path(const fs::path& a, const fs::path& b)
{
    fs::path p = a;
    p /= b;
    return p.u8string();
}

static fs::path default_cache_root(const char* argv0)
{
    std::error_code ec;
    fs::path exe = fs::absolute(argv0, ec);
    if (ec) {
        return fs::path("faust-packages");
    }
    fs::path dir = exe.parent_path();
    std::string s = dir.u8string();
    auto ends_with = [](const std::string& v, const std::string& suffix) {
        return v.size() >= suffix.size() &&
               v.compare(v.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    // When built from repo: .../build/faustpkg or .../build/bin
    if (ends_with(s, "/build/faustpkg") || ends_with(s, "\\build\\faustpkg")) {
        dir = dir.parent_path().parent_path();
    } else if (ends_with(s, "/build/bin") || ends_with(s, "\\build\\bin")) {
        dir = dir.parent_path().parent_path();
    } else if (dir.filename() == "bin") {
        dir = dir.parent_path();
    }

    return dir / "faust-packages";
}

static int run_cmd(const std::string& cmd)
{
    return std::system(cmd.c_str());
}

static bool ensure_dir(const fs::path& p)
{
    std::error_code ec;
    if (fs::exists(p, ec)) {
        return fs::is_directory(p, ec);
    }
    return fs::create_directories(p, ec);
}

static bool copy_tree(const fs::path& src, const fs::path& dst)
{
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        return false;
    }
    fs::create_directories(dst, ec);
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}

static std::string quote(const std::string& s)
{
    std::string q = "\"" + s + "\"";
    return q;
}

static bool has_faust_manifest(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p / "Faust.toml", ec);
}

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string unquote(const std::string& s)
{
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static bool read_faust_toml(const fs::path& p, std::string& name, std::string& version, std::string& entry)
{
    std::ifstream in(p);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));
        val = unquote(val);
        if (key == "name") {
            name = val;
        } else if (key == "version") {
            version = val;
        } else if (key == "entry") {
            entry = val;
        }
    }
    return !name.empty() && !version.empty();
}

static bool ensure_package_entry(const fs::path& root, const std::string& entry)
{
    fs::path pkg = root / "package.dsp";
    if (fs::exists(pkg)) {
        return true;
    }
    if (entry.empty()) {
        return false;
    }
    std::ofstream out(pkg);
    if (!out) {
        return false;
    }
    out << "import(\"" << entry << "\");\n";
    return true;
}

static int install_pkg(const PkgSpec& spec, const std::string& repo_override,
                       const fs::path& cache_root)
{
    fs::path pkg_root = cache_root / spec.name;
    fs::path ver_root = pkg_root / spec.version;

    if (fs::exists(ver_root)) {
        std::cout << "already installed: " << spec.name << "==" << spec.version << "\n";
        // Ensure current pointer
        copy_tree(ver_root, pkg_root);
        return 0;
    }

    if (!ensure_dir(cache_root)) {
        std::cerr << "cannot create cache root: " << cache_root.u8string() << "\n";
        return 1;
    }

    // Build repo URL and repo name
    std::string repo_path = repo_override.empty()
        ? (std::string(owner) + "/" + spec.name)
        : repo_override;
    std::string repo = std::string(base) + "/" + repo_path;
    std::string repo_name = repo_path;
    size_t slash = repo_name.find_last_of('/');
    if (slash != std::string::npos) {
        repo_name = repo_name.substr(slash + 1);
    }

    // Download zip into temp
    fs::path tmp = cache_root / (spec.name + "-tmp");
    std::error_code ec;
    if (fs::exists(tmp, ec)) {
        fs::remove_all(tmp, ec);
    }
    ensure_dir(tmp);

    std::string tag = "v" + spec.version;
    std::string zip_url = repo + "/archive/refs/tags/" + tag + ".zip";
    fs::path zip_path = tmp / (repo_name + "-" + tag + ".zip");

    std::cout << "downloading " << zip_url << " ...\n";
    std::stringstream dl;
    dl << "powershell -NoProfile -Command "
       << quote("Invoke-WebRequest -Uri '" + zip_url + "' -OutFile '" + zip_path.u8string() + "'");
    if (run_cmd(dl.str()) != 0) {
        std::cerr << "download failed\n";
        fs::remove_all(tmp, ec);
        return 1;
    }

    std::stringstream unz;
    unz << "powershell -NoProfile -Command "
        << quote("Expand-Archive -Path '" + zip_path.u8string() + "' -DestinationPath '" + tmp.u8string() + "' -Force");
    if (run_cmd(unz.str()) != 0) {
        std::cerr << "unzip failed\n";
        fs::remove_all(tmp, ec);
        return 1;
    }

    fs::path extracted = tmp / (repo_name + "-" + tag);
    if (!has_faust_manifest(extracted)) {
        // Fallback: find any top-level folder that contains Faust.toml
        fs::path found;
        for (const auto& entry : fs::directory_iterator(tmp)) {
            if (!entry.is_directory()) {
                continue;
            }
            if (has_faust_manifest(entry.path())) {
                found = entry.path();
                break;
            }
        }
        if (!found.empty()) {
            extracted = found;
        } else {
            std::cerr << "invalid package: missing Faust.toml\n";
            fs::remove_all(tmp, ec);
            return 1;
        }
    }

    std::string pname;
    std::string pver;
    std::string entry;
    if (!read_faust_toml(extracted / "Faust.toml", pname, pver, entry)) {
        std::cerr << "invalid package: cannot read Faust.toml\n";
        fs::remove_all(tmp, ec);
        return 1;
    }
    std::cout << "package metadata:\n"
              << "  name = \"" << pname << "\"\n"
              << "  version = \"" << pver << "\"\n"
              << "  entry = \"" << entry << "\"\n";
    if (pname != spec.name || pver != spec.version) {
        std::cerr << "warning: name/version mismatch (toml=" << pname << "==" << pver
                  << ", requested=" << spec.name << "==" << spec.version << ")\n";
    }
    if (!ensure_package_entry(extracted, entry)) {
        std::cerr << "invalid package: missing entry\n";
        fs::remove_all(tmp, ec);
        return 1;
    }

    if (!copy_tree(extracted, ver_root)) {
        std::cerr << "copy failed\n";
        fs::remove_all(tmp, ec);
        return 1;
    }

    // Update current pointer by copying version to pkg_root
    copy_tree(ver_root, pkg_root);

    fs::remove_all(tmp, ec);
    std::cout << "installed " << spec.name << "==" << spec.version << "\n";
    return 0;
}

static int list_pkgs(const fs::path& cache_root)
{
    std::error_code ec;
    if (!fs::exists(cache_root, ec)) {
        std::cout << "cache empty\n";
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(cache_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        std::cout << entry.path().filename().u8string() << "\n";
    }
    return 0;
}

static int remove_pkg(const PkgSpec& spec, const fs::path& cache_root)
{
    fs::path ver_root = cache_root / spec.name / spec.version;
    std::error_code ec;
    if (!fs::exists(ver_root, ec)) {
        std::cerr << "not installed: " << spec.name << "==" << spec.version << "\n";
        return 1;
    }
    fs::remove_all(ver_root, ec);
    if (ec) {
        std::cerr << "remove failed\n";
        return 1;
    }
    std::cout << "removed " << spec.name << "==" << spec.version << "\n";
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        print_available_services();
        return 1;
    }

    std::string cmd = argv[1];
    std::string repo_override;
    fs::path cache_root = default_cache_root(argv[0]);

    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--repo" && i + 1 < args.size()) {
            repo_override = args[i + 1];
            i++;
        } else if (args[i] == "--cache" && i + 1 < args.size()) {
            cache_root = args[i + 1];
            i++;
        }
    }

    if (cmd == "install") {
        if (args.empty()) {
            print_available_services();
            return 1;
        }
        PkgSpec spec;
        if (!split_spec(args[0], spec)) {
            std::cerr << "invalid spec, expected name==version\n";
            return 1;
        }
        return install_pkg(spec, repo_override, cache_root);
    } else if (cmd == "list") {
        return list_pkgs(cache_root);
    } else if (cmd == "remove") {
        if (args.empty()) {
            print_available_services();
            return 1;
        }
        PkgSpec spec;
        if (!split_spec(args[0], spec)) {
            std::cerr << "invalid spec, expected name==version\n";
            return 1;
        }
        return remove_pkg(spec, cache_root);
    } else {
        print_available_services();
        return 1;
    }
}
