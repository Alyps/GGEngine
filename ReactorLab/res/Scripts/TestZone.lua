local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("testzone",self)
    self.trigger = self.object:get_component(ComponentType.BoxTrigger)
    self.entered, self.exited = 0, 0
    self.trigger.body_entered:connect(self,function(s, other) s.entered = s.entered + 1 end)
    self.trigger.body_exited:connect(self,function(s, other) s.exited = s.exited + 1 end)
end
return M
