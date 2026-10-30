#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/detail/md5.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using Hash = std::string;

Hash crc32(const std::vector<char>& data)
{
    boost::crc_32_type crc;
    crc.process_bytes(data.data(), data.size());
    auto v = crc.checksum();
    return Hash(reinterpret_cast<const char*>(&v), sizeof(v));
}

Hash md5(const std::vector<char>& data)
{
    boost::uuids::detail::md5 h;
    h.process_bytes(data.data(), data.size());
    boost::uuids::detail::md5::digest_type d;
    h.get_digest(d);
    return Hash(reinterpret_cast<const char*>(&d), sizeof(d));
}

class File {
public:
    File(fs::path path, std::size_t size, std::size_t blockSize, Hash (*hash)(const std::vector<char>&))
        : path_(std::move(path)), blockSize_(blockSize), hash_(hash)
    {
        blockCount_ = (size + blockSize - 1) / blockSize;
    }

    const fs::path& path() const { return path_; }
    std::size_t blockCount() const { return blockCount_; }

    const Hash& block(std::size_t i)
    {
        while (hashes_.size() <= i) {
            std::vector<char> buf(blockSize_, 0);
            std::ifstream in(path_.string(), std::ios::binary);
            in.seekg(static_cast<std::streamoff>(hashes_.size() * blockSize_));
            in.read(buf.data(), static_cast<std::streamsize>(blockSize_));
            hashes_.push_back(hash_(buf));
        }
        return hashes_[i];
    }

private:
    fs::path path_;
    std::size_t blockSize_;
    std::size_t blockCount_;
    Hash (*hash_)(const std::vector<char>&);
    std::vector<Hash> hashes_;
};

using Group = std::vector<std::shared_ptr<File>>;

void findDuplicates(const Group& group, std::size_t block, int& groups)
{
    if (block == group.front()->blockCount()) {
        std::cout << (groups++ ? "\n" : "");
        for (const auto& f : group)
            std::cout << f->path().string() << '\n';
        return;
    }
    std::map<Hash, Group> parts;
    for (const auto& f : group)
        parts[f->block(block)].push_back(f);
    for (const auto& p : parts)
        if (p.second.size() > 1)
            findDuplicates(p.second, block + 1, groups);
}

std::regex maskToRegex(std::string mask)
{
    mask = std::regex_replace(mask, std::regex(R"([.^$|()\[\]{}+\\])"), R"(\$&)");
    mask = std::regex_replace(mask, std::regex(R"(\*)"), ".*");
    return std::regex(std::regex_replace(mask, std::regex(R"(\?)"), "."), std::regex::icase);
}

int main(int argc, char* argv[])
{
    std::vector<std::string> dirs, excludes, masks;
    int level = 0;
    std::size_t minSize = 1, blockSize = 1024;
    std::string algorithm = "crc32";

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("dir,d", po::value(&dirs)->multitoken(), "directories to scan")
        ("exclude,e", po::value(&excludes)->multitoken(), "directories to exclude")
        ("level,l", po::value(&level)->default_value(0), "scan depth, 0 - only given directory")
        ("min-size,m", po::value(&minSize)->default_value(1), "minimum file size in bytes")
        ("mask,M", po::value(&masks)->multitoken(), "file name masks, case insensitive")
        ("block-size,b", po::value(&blockSize)->default_value(1024), "block size S in bytes")
        ("hash,H", po::value(&algorithm)->default_value("crc32"), "hash algorithm H: crc32 or md5");
    po::positional_options_description pos;
    pos.add("dir", -1);

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(pos).run(), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << e.what() << '\n' << desc;
        return 1;
    }
    if (vm.count("help") || dirs.empty() || blockSize == 0 || (algorithm != "crc32" && algorithm != "md5")) {
        std::cout << "Usage: bayan [options] <dir>...\n" << desc;
        return vm.count("help") ? 0 : 1;
    }
    auto hash = algorithm == "md5" ? md5 : crc32;

    std::set<fs::path> excluded;
    for (const auto& e : excludes)
        if (fs::exists(e))
            excluded.insert(fs::canonical(e));

    std::vector<std::regex> regexes;
    for (const auto& m : masks)
        regexes.push_back(maskToRegex(m));

    std::set<fs::path> seen;
    std::map<std::uintmax_t, Group> bySize;
    for (const auto& d : dirs) {
        if (!fs::is_directory(d))
            continue;
        for (fs::recursive_directory_iterator it(d, fs::directory_options::skip_permission_denied), end; it != end; ++it) {
            const auto& p = it->path();
            if (fs::is_directory(p)) {
                if (excluded.count(fs::canonical(p)) || it.depth() >= level)
                    it.disable_recursion_pending();
                continue;
            }
            if (!fs::is_regular_file(p))
                continue;
            auto size = fs::file_size(p);
            if (size < minSize)
                continue;
            auto name = p.filename().string();
            if (!regexes.empty() && std::none_of(regexes.begin(), regexes.end(),
                                                 [&](const std::regex& r) { return std::regex_match(name, r); }))
                continue;
            auto full = fs::canonical(p);
            if (seen.insert(full).second)
                bySize[size].push_back(std::make_shared<File>(full, size, blockSize, hash));
        }
    }

    int groups = 0;
    for (const auto& s : bySize)
        if (s.second.size() > 1)
            findDuplicates(s.second, 0, groups);
    return 0;
}
