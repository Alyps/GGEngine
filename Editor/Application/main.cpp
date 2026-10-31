#include <core.h>
#include "Editor.h"
#include <filesystem>

int main()
{
    Game game(L"GGEngine Editor");
    // Встроенные файлы движка не зависят от выбранного проекта и рабочей папки.
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    module.resize(length);
    const auto bundled = std::filesystem::path(module).parent_path() / L"StandartResources";
    std::error_code error;
    if (length && std::filesystem::is_directory(bundled, error)) {
        // Менеджер ресурсов принимает пути в UTF-8.
        const auto utf8 = bundled.generic_u8string();
        game.getResourceManager().SetStandardResourceDirectory(std::string(utf8.begin(), utf8.end()));
    } else {
        game.getResourceManager().SetStandardResourceDirectory(GG_STANDARD_RESOURCE_DIRECTORY);
    }
    game.Init();
    // Редактор начинает с демосцены и временного каталога ресурсов в LocalAppData.
    Editor editor(game);
    return editor.Run();
}
