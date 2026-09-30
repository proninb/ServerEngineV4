#include "project/ic/ic_catalog.hpp"

#include <filesystem>

using namespace cw::server;

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() /
        "server_engine_v4_ic_catalog_tests";

    std::error_code error;

    std::filesystem::remove_all(
        directory,
        error);

    error.clear();

    std::filesystem::create_directories(
        directory,
        error);

    if (error) {
        return 1;
    }

    const auto catalog_path =
        directory / "IC.json";

    const std::filesystem::path cold_path{
        "snapshots/cold.ic"};

    ic_catalog catalog;

    if (!upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            cold_path,
            128)) {

        return 2;
    }

    const auto* entry =
        find_ic_catalog_entry(
            catalog,
            catalog_path,
            cold_path);

    if (entry == nullptr ||
        entry->size != 128 ||
        entry->path != cold_path) {

        return 3;
    }

    // Equivalent absolute locator addresses the same IC.
    if (!upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            directory /
                "snapshots" /
                "cold.ic",
            129)) {

        return 4;
    }

    if (catalog.items.size() != 1) {
        return 5;
    }

    entry =
        find_ic_catalog_entry(
            catalog,
            catalog_path,
            cold_path);

    if (entry == nullptr ||
        entry->size != 129) {

        return 6;
    }

    if (!upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            std::filesystem::path{
                "snapshots/warm.ic"},
            256)) {

        return 7;
    }

    if (catalog.items.size() != 2) {
        return 8;
    }

    if (save_ic_catalog(
            catalog_path,
            catalog) !=
        ic_catalog_result::success) {

        return 9;
    }

    ic_catalog loaded;

    if (load_ic_catalog(
            catalog_path,
            loaded) !=
        ic_catalog_result::success) {

        return 10;
    }

    entry =
        find_ic_catalog_entry(
            loaded,
            catalog_path,
            cold_path);

    if (entry == nullptr ||
        entry->size != 129 ||
        entry->path != cold_path) {

        return 11;
    }

    if (!upsert_ic_catalog_entry(
            loaded,
            catalog_path,
            cold_path,
            130)) {

        return 12;
    }

    if (save_ic_catalog(
            catalog_path,
            loaded) !=
        ic_catalog_result::success) {

        return 13;
    }

    auto temporary_catalog_path =
        catalog_path;

    temporary_catalog_path += ".new";

    if (std::filesystem::exists(
            temporary_catalog_path)) {

        return 14;
    }

    ic_catalog replaced;

    if (load_ic_catalog(
            catalog_path,
            replaced) !=
        ic_catalog_result::success) {

        return 15;
    }

    entry =
        find_ic_catalog_entry(
            replaced,
            catalog_path,
            cold_path);

    if (entry == nullptr ||
        entry->size != 130) {

        return 16;
    }

    std::filesystem::path removed_path;

    if (!erase_ic_catalog_entry(
            replaced,
            catalog_path,
            directory /
                "snapshots" /
                "cold.ic",
            removed_path)) {

        return 17;
    }

    if (removed_path != cold_path ||
        find_ic_catalog_entry(
            replaced,
            catalog_path,
            cold_path) != nullptr) {

        return 18;
    }

    std::filesystem::remove_all(
        directory,
        error);

    return 0;
}
