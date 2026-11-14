local base = require("res.Scripts.Player")
local M = {}
for k,v in pairs(base) do M[k] = v end
function M.on_init(self)
    self.auto = true
    base.on_init(self)
end
return M
