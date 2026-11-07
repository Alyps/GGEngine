#include <core.h>
#include "Editor.h"
#include "EditorFileUtils.h"

int main()
{
    // Пути релиза считаются от EXE независимо от рабочей папки.
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) {
        MessageBoxW(nullptr, L"Cannot locate Editor.exe.", L"GGEngine Editor", MB_OK | MB_ICONERROR);
        return 1;
    }
    module.resize(length);
    const auto directory = std::filesystem::path(module).parent_path();
    const auto bundled = directory / L"StandartResources";
    std::error_code error;
    if (!std::filesystem::is_directory(bundled, error)) {
        MessageBoxW(nullptr, L"StandartResources is missing or inaccessible next to Editor.exe.",
            L"GGEngine Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    Game game(L"GGEngine Editor");
    game.getResourceManager().SetStandardResourceDirectory(EditorFiles::Utf8Path(bundled));
    game.Init();
    // Черновые проекты хранятся в res рядом с редактором.
    Editor editor(game, EditorFiles::Utf8Path(directory / L"res"));
    return editor.Run();
}
