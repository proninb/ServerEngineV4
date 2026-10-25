#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace source_locality01 {
struct event {
    char kind = 0;
    std::uint32_t root = 0, file = 0, scope = 0, name = 0, who = 0;
    std::uint32_t object_type = 0, leaf_type = 0, source = 0, source_type = 0;
    std::uint32_t endpoint = 0, category = 0, offset = 0, length = 0;
};
struct capture_state {
    std::uint32_t root = 0;
    bool failed = false;
    std::vector<event> events;
    std::unordered_map<std::uint32_t, std::string> files;
    template<class Path> void file(std::uint32_t id, Path path) noexcept {
        if (files.contains(id)) return;
        try {
            const auto text = std::filesystem::path{path}.generic_u8string();
            files.emplace(id, std::string{reinterpret_cast<const char*>(text.data()), text.size()});
        } catch (...) { failed = true; }
    }
    std::size_t add(event value) noexcept {
        value.root = root;
        try { events.push_back(value); return events.size() - 1; }
        catch (...) { failed = true; return (std::numeric_limits<std::size_t>::max)(); }
    }
    void classify(std::size_t index, std::uint32_t category,
        std::uint32_t source = 0, std::uint32_t source_type = 0) noexcept {
        if (index >= events.size()) return;
        events[index].category = category;
        events[index].source = source;
        events[index].source_type = source_type;
    }
    void dump(const std::filesystem::path& directory) const {
        if (failed) throw std::runtime_error("Source locality capture allocation failed");
        std::filesystem::create_directories(directory);
        std::ofstream out{directory / "events.csv", std::ios::binary};
        out << "kind,root,file,scope,name,who,object_type,leaf_type,source,source_type,endpoint,category,offset,length\n";
        for (const auto& e : events)
            out << e.kind << ',' << e.root << ',' << e.file << ',' << e.scope << ',' << e.name << ','
                << e.who << ',' << e.object_type << ',' << e.leaf_type << ',' << e.source << ','
                << e.source_type << ',' << e.endpoint << ',' << e.category << ',' << e.offset << ',' << e.length << '\n';
        if (!out) throw std::runtime_error("Cannot write Source locality events");
        std::ofstream paths{directory / "files.csv", std::ios::binary};
        paths << "file,path\n";
        for (const auto& [id, path] : files) {
            paths << id << ",\"";
            for (const auto c : path) { if (c == '"') paths << '"'; paths << c; }
            paths << "\"\n";
        }
        if (!paths) throw std::runtime_error("Cannot write Source locality files");
    }
};
// Only the sequential Source owner invokes these hooks; lexer workers do not.
inline capture_state capture;
}
