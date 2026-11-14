local Arena = require("res.Scripts.Arena")
local AutoArena = {}
for key, method in pairs(Arena) do
    AutoArena[key] = method
end

function AutoArena.on_init(self)
    self.auto = true
    Arena.on_init(self)
end

return AutoArena
