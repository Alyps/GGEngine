local W = require("res.Scripts.World")
local M = {}
function M.on_init(self) self.base = self.transform:get_child(0).scale end
function M.on_update(self, dt)
    local g = W.get("game")
    if not g then return end
    local value = 1
    if self.kind == "TimeBar" then value = g.timer.time_left / 90 end
    if self.kind == "HealthBar" then value = g.health / 3 end
    if self.kind == "CellBar" then value = g.cells / 3 end
    if self.kind == "Won" then value = g.state == "won" and 1 or 0 end
    if self.kind == "Lost" then value = g.state == "lost" and 1 or 0 end
    if self.kind == "Patrol" or self.kind == "Chase" or self.kind == "Recharge" then
        local d = W.get("drone")
        value = d and d.ai.current_action == self.kind and 1 or 0
    end
    self.transform:get_child(0).scale = Vector3.new(self.base.x*math.max(.001,value),self.base.y,self.base.z)
end
return M
