local base = require("res.Scripts.Hud")
local M = {}
for k,v in pairs(base) do M[k] = v end
function M.on_init(self)
    self.kind = "Won"
    base.on_init(self)
end
return M
