local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("testbody",self)
    self.event = Delegate.new(self)
end
return M
