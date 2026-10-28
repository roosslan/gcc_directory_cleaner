/* upd 16.10.2026 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include "sensitive_data.h" /* exclude_directory, mail_to */

namespace fs = std::filesystem;

const std::string program_name = "studcleaner";
const fs::path root = fs::path("/xchange") / exclude_directory;                    /* очищаемая директория */
const fs::path recycle_root = fs::path("/xchange") / "#recycle" / exclude_directory; /* куда переносятся старые файлы */
const auto max_age = std::chrono::hours(24 * 7);

/* Директории первого уровня внутри root, которые не трогаются */
const std::set<std::string> excluded_top = {};

struct Report {
    std::vector<std::string> moved;
    std::vector<std::string> removed_dirs;
    std::vector<std::string> errors;
};

void add_error(Report& report, const std::string& what, const fs::path& p, const std::error_code& ec) {
    report.errors.push_back(what + " " + p.string() + ": " + ec.message());
}

/* @eaDir - служебная директория NAS ("extended attributes directory") */
bool is_service_dir(const fs::path& p) {
    return p.filename() == "@eaDir";
}

bool is_old(const fs::path& p, std::error_code& ec) {
    const auto lwt = fs::last_write_time(p, ec);
    return !ec && fs::file_time_type::clock::now() - lwt > max_age;
}

bool exists_nofollow(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}

/* Свободное имя в корзине, чтобы не затереть ранее удалённый файл: a.txt -> a (1).txt -> a (2).txt ... */
fs::path free_name(const fs::path& target) {
    if (!exists_nofollow(target))
        return target;
    const std::string stem = target.stem().string();
    const std::string ext = target.extension().string();
    for (int i = 1; ; ++i) {
        const fs::path candidate = target.parent_path() / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!exists_nofollow(candidate))
            return candidate;
    }
}

/* Сбор кандидатов. Дерево не меняется во время обхода: сначала список, потом перенос. */
void collect(std::vector<fs::path>& files, std::vector<fs::path>& dirs, Report& report) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        add_error(report, "cannot open", root, ec);
        return;
    }
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::error_code type_ec;
        const bool is_dir = it->is_directory(type_ec) && !it->is_symlink(type_ec);

        /* Исключения: точное совпадение имени на первом уровне и служебные директории NAS на любом */
        if ((it.depth() == 0 && excluded_top.count(p.filename().string())) || is_service_dir(p)) {
            if (is_dir)
                it.disable_recursion_pending();
            continue;
        }

        std::error_code age_ec;
        const bool old = is_old(p, age_ec);
        if (age_ec) {
            add_error(report, "cannot stat", p, age_ec);
            continue;
        }
        if (old)
            (is_dir ? dirs : files).push_back(p);
    }
    if (ec)
        add_error(report, "directory walk interrupted in", root, ec);
}

void move_files(const std::vector<fs::path>& files, std::set<fs::path>& gone, Report& report, bool dry_run) {
    for (const auto& src : files) {
        const fs::path target = free_name(recycle_root / src.lexically_relative(root));
        if (!dry_run) {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            if (ec) {
                add_error(report, "cannot create", target.parent_path(), ec);
                continue;
            }
            /* rename в пределах одного тома атомарен и сохраняет mtime и права; при ошибке оригинал остаётся на месте */
            fs::rename(src, target, ec);
            if (ec) {
                add_error(report, "cannot move", src, ec);
                continue;
            }
        }
        gone.insert(src);
        report.moved.push_back(src.string() + " -> " + target.string());
    }
}

/* Директория считается пустой, если в ней остались только @eaDir и то, что уже перенесено или удалено */
bool only_service_left(const fs::path& dir, const std::set<fs::path>& gone, std::error_code& ec) {
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!is_service_dir(it->path()) && !gone.count(it->path()))
            return false;
    }
    return !ec;
}

void remove_empty_dirs(std::vector<fs::path> dirs, std::set<fs::path>& gone, Report& report, bool dry_run) {
    /* Сначала самые глубокие, чтобы освободившиеся родители тоже удалились */
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!only_service_left(dir, gone, ec)) {
            if (ec)
                add_error(report, "cannot read", dir, ec);
            continue;
        }
        if (!dry_run) {
            std::vector<fs::path> service;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                if (is_service_dir(it->path()))
                    service.push_back(it->path());
            for (const auto& s : service)
                fs::remove_all(s, ec);
            /* Нерекурсивное удаление: если кто-то успел положить файл, директория останется */
            if (!ec)
                fs::remove(dir, ec);
            if (ec) {
                add_error(report, "cannot remove", dir, ec);
                continue;
            }
        }
        gone.insert(dir);
        report.removed_dirs.push_back(dir.string());
    }
}

std::string format_report(const Report& report, bool dry_run) {
    std::ostringstream out;
    if (dry_run)
        out << "DRY RUN: nothing was changed\n\n";
    out << "Moved files: " << report.moved.size() << "\n";
    for (const auto& s : report.moved)
        out << "  " << s << "\n";
    out << "\nRemoved empty directories: " << report.removed_dirs.size() << "\n";
    for (const auto& s : report.removed_dirs)
        out << "  " << s << "\n";
    out << "\nErrors: " << report.errors.size() << "\n";
    for (const auto& s : report.errors)
        out << "  " << s << "\n";
    return out.str();
}

/* Отправка отчёта через msmtp (настройки SMTP в ~/.msmtprc) */
bool send_mail(const Report& report, const std::string& body) {
    FILE* pipe = popen("msmtp -t", "w");
    if (!pipe) {
        std::cerr << "cannot start msmtp\n";
        return false;
    }
    std::ostringstream msg;
    msg << "To: " << mail_to << "\n"
        << "Subject: " << program_name << ": moved " << report.moved.size()
        << ", removed dirs " << report.removed_dirs.size()
        << ", errors " << report.errors.size() << "\n"
        << "MIME-Version: 1.0\n"
        << "Content-Type: text/plain; charset=UTF-8\n"
        << "Content-Transfer-Encoding: 8bit\n"
        << "\n"
        << body;
    const std::string s = msg.str();
    const bool written = fwrite(s.data(), 1, s.size(), pipe) == s.size();
    const int status = pclose(pipe);
    if (!written || status != 0) {
        std::cerr << "msmtp failed, status " << status << "\n";
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    bool dry_run = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--dry-run") {
            dry_run = true;
        } else {
            std::cerr << "usage: " << argv[0] << " [--dry-run]\n";
            return 2;
        }
    }

    Report report;
    std::vector<fs::path> files, dirs;
    std::set<fs::path> gone;
    collect(files, dirs, report);
    move_files(files, gone, report, dry_run);
    remove_empty_dirs(dirs, gone, report, dry_run);

    const std::string body = format_report(report, dry_run);
    std::cout << body;

    bool ok = report.errors.empty();
    if (!dry_run)
        ok = send_mail(report, body) && ok;
    return ok ? 0 : 1;
}
