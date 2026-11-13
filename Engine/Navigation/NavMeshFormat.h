#pragma once
// Внутренний формат файла навмеша; подключается только в .cpp подсистемы навигации.
#include <DetourNavMesh.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NavMeshFormat {
    // "GGNM" в little-endian; версия меняется при любом изменении раскладки.
    constexpr std::uint32_t Magic = 'G' | ('G' << 8) | ('N' << 16) | ('M' << 24);
    constexpr std::uint32_t Version = 1;

    struct FileHeader {
        std::uint32_t magic = Magic;
        std::uint32_t version = Version;
        // Хеш геометрии, объёмов и настроек на момент генерации; редактор сравнивает его с текущим.
        std::uint64_t inputHash = 0;
        dtNavMeshParams params{};
        std::int32_t tileCount = 0;
    };

    struct TileHeader {
        dtTileRef ref = 0;
        std::int32_t dataSize = 0;
    };

    // Данные тайла выделены dtAlloc. Владелец — Data, пока тайл не передан dtNavMesh::addTile
    // с DT_TILE_FREE_DATA; после передачи указатель обнуляется.
    struct Tile {
        unsigned char* data = nullptr;
        int size = 0;
    };

    // Владеет тайлами; копирование запрещено, перемещение передаёт владение.
    struct Data {
        FileHeader header;
        std::vector<Tile> tiles;
        Data() = default;
        ~Data() { Release(); }
        Data(const Data&) = delete;
        Data& operator=(const Data&) = delete;
        Data(Data&& other) noexcept : header(other.header), tiles(std::move(other.tiles)) { other.tiles.clear(); }
        Data& operator=(Data&& other) noexcept {
            // Прежние тайлы освобождаются: вектор из простых структур сам их не удалит.
            if (this != &other) {
                Release();
                header = other.header;
                tiles = std::move(other.tiles);
                other.tiles.clear();
            }
            return *this;
        }
        void Release() {
            for (auto& tile : tiles) dtFree(tile.data);
            tiles.clear();
        }
    };

    // Пути UTF-8. Запись атомарная: временный файл заменяет целевой только после успеха.
    bool Write(const std::string& path, const Data& data, std::string& error);
    // При ошибке data остаётся пустым.
    bool Read(const std::string& path, Data& data, std::string& error);
}
