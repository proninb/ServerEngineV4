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

    if (!upsert_ic_catalog_node(
            catalog,
            {"Startup", "Commissioning"},
            "Cold",
            std::filesystem::path{
                "snapshots/cold.ic"},
            128)) {

        return 2;
    }

    const auto* node =
        find_ic_catalog_node(
            catalog,
            {"Startup", "Commissioning"},
            "Cold");

    if (node == nullptr ||
        node->kind !=
            ic_catalog_node_kind::ic ||
        node->size != 128 ||
        node->path !=
            std::filesystem::path{
                "snapshots/cold.ic"}) {

        return 3;
    }

    if (save_ic_catalog(
            catalog_path,
            catalog) !=
        ic_catalog_result::success) {

        return 4;
    }

    ic_catalog loaded;

    if (load_ic_catalog(
            catalog_path,
            loaded) !=
        ic_catalog_result::success) {

        return 5;
    }

    node =
        find_ic_catalog_node(
            loaded,
            {"Startup", "Commissioning"},
            "Cold");

    if (node == nullptr ||
        node->size != 128 ||
        node->path !=
            std::filesystem::path{
                "snapshots/cold.ic"}) {

        return 6;
    }

    std::filesystem::path removed_path;

    if (!erase_ic_catalog_node(
            loaded,
            {"Startup", "Commissioning"},
            "Cold",
            removed_path)) {

        return 7;
    }

    if (removed_path !=
            std::filesystem::path{
                "snapshots/cold.ic"} ||
        find_ic_catalog_node(
            loaded,
            {"Startup", "Commissioning"},
            "Cold") != nullptr) {

        return 8;
    }

    std::filesystem::remove_all(
        directory,
        error);

    return 0;
}
