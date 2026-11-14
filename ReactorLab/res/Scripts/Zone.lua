local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    self.kind = self.kind or "Cell"
    self.trigger = self.object:get_component(ComponentType.BoxTrigger)
    self.timer = self.object:get_component(ComponentType.Timer)
    self.active, self.taken, self.inside = true, false, nil
    self.trigger.body_entered:connect(self, function(s, other)
        local component = other:get_component(ComponentType.LuaScript)
        local player = component and component.instance
        if player ~= W.get("player") then return end
        local game = W.get("game")
        if s.kind == "Cell" and not s.taken then
            s.taken = true; s.trigger.enabled = false
            s.transform.position = s.transform.position + Vector3.new(0,0,-20)
            game:collect()
        elseif s.kind == "Exit" then
            if game.cells == 3 then game:finish(true) end
        elseif s.kind == "Hazard" then s.inside = player; player:hit() end
    end)
    self.trigger.body_exited:connect(self, function(s, other)
        if s.kind == "Hazard" then s.inside = nil end
    end)
    if self.timer then self.timer.timeout:connect(self, function(s)
        s.active = not s.active
        s.trigger.enabled = s.active
        s.transform:get_child(0).scale = Vector3.new(s.active and 5 or .75,1.4,1)
    end) end
end
function M.on_ready(self)
    self.start = self.transform.position
    W.get("game").changed:connect(self, function(s, kind)
        if kind ~= "restart" then return end
        s.transform.position = s.start
        s.taken, s.active, s.inside = false, true, nil
        s.trigger.enabled = true
        if s.timer then s.timer:start() end
    end)
    if self.timer then self.timer:start() end
end
function M.on_update(self, dt)
    if self.inside and self.inside.is_valid and self.active then self.inside:hit() end
end
return M
