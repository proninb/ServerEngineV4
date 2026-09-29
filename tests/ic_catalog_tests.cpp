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

    ic_catalog catalog;

    if (!upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            "Cold",
            std::filesystem::path{
                "snapshots/cold.ic"},
            128)) {

        return 2;
    }

    const auto* entry =
        find_ic_catalog_entry(
            catalog,
            "Cold");

    if (entry == nullptr ||
        entry->size != 128 ||
        entry->path !=
            std::filesystem::path{
                "snapshots/cold.ic"}) {

        return 3;
    }

    if (upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            "Duplicate",
            directory /
                "snapshots" /
                "cold.ic",
            64)) {

        return 4;
    }

    if (!upsert_ic_catalog_entry(
            catalog,
            catalog_path,
            "Warm",
            std::filesystem::path{
                "snapshots/warm.ic"},
            256)) {

        return 5;
    }

    if (save_ic_catalog(
            catalog_path,
            catalog) !=
        ic_catalog_result::success) {

        return 6;
    }

    ic_catalog loaded;

    if (load_ic_catalog(
            catalog_path,
            loaded) !=
        ic_catalog_result::success) {

        return 7;
    }

    entry =
        find_ic_catalog_entry(
            loaded,
            "Cold");

    if (entry == nullptr ||
        entry->size != 128 ||
        entry->path !=
            std::filesystem::path{
                "snapshots/cold.ic"}) {

        return 8;
    }

    if (!upsert_ic_catalog_entry(
            loaded,
            catalog_path,
            "Cold",
            std::filesystem::path{
                "snapshots/cold.ic"},
            129)) {

        return 9;
    }

    if (save_ic_catalog(
            catalog_path,
            loaded) !=
        ic_catalog_result::success) {

        return 10;
    }

    auto temporary_catalog_path =
        catalog_path;

    temporary_catalog_path += ".new";

    if (std::filesystem::exists(
            temporary_catalog_path)) {

        return 11;
    }

    ic_catalog replaced;

    if (load_ic_catalog(
            catalog_path,
            replaced) !=
        ic_catalog_result::success) {

        return 12;
    }

    entry =
        find_ic_catalog_entry(
            replaced,
            "Cold");

    if (entry == nullptr ||
        entry->size != 129) {

        return 13;
    }

    std::filesystem::path removed_path;

    if (!erase_ic_catalog_entry(
            replaced,
            "Cold",
            removed_path)) {

        return 14;
    }

    if (removed_path !=
            std::filesystem::path{
                "snapshots/cold.ic"} ||
        find_ic_catalog_entry(
            replaced,
            "Cold") != nullptr) {

        return 15;
    }

    std::filesystem::remove_all(
        directory,
        error);

    return 0;
}
