local base = require("res.Scripts.Zone")
local M = {}
for k,v in pairs(base) do M[k] = v end
function M.on_init(self)
    self.kind = "Exit"
    base.on_init(self)
end
return M
