#include <core.h>
#include "Editor.h"

int main()
{
    Game game(L"GGEngine Editor");
    // Встроенные файлы движка не зависят от выбранного проекта и рабочей папки.
    game.getResourceManager().SetStandardResourceDirectory(GG_STANDARD_RESOURCE_DIRECTORY);
    game.Init();
    // Редактор начинает с демосцены и временного каталога ресурсов в LocalAppData.
    Editor editor(game);
    return editor.Run();
}
