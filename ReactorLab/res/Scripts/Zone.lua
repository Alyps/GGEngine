local W = require("res.Scripts.World")
local M = {}

local function matchingPlayer(other)
    local player = W.get("player")
    if not player then return nil end
    local component = other:get_component(ComponentType.LuaScript)
    return component and component.instance == player and player or nil
end

local function setHazardActive(self, active)
    self.active = active
    self.trigger.enabled = active
    if not active then self.inside = nil end
    local scale = self.hazardScale
    self.transform:get_child(0).scale = Vector3.new(active and scale.x or .75, scale.y, scale.z)
end

function M.on_init(self)
    self.kind = self.kind or "Cell"
    self.trigger = self.object:get_component(ComponentType.BoxTrigger)
    self.timer = self.object:get_component(ComponentType.Timer)
    self.active, self.taken, self.inside = true, false, nil
    self.trigger.body_entered:connect(self, function(s, other)
        local player = matchingPlayer(other)
        local game = W.get("game")
        if not player or not game or game.state ~= "playing" then return end
        if s.kind == "Cell" and not s.taken then
            s.taken = true
            s.trigger.enabled = false
            s.transform.position = s.transform.position + Vector3.new(0,0,-20)
            game:collect()
        elseif s.kind == "Exit" then
            if game.cells == 3 then game:finish(true) end
        elseif s.kind == "Hazard" and s.active then
            s.inside = player
            player:hit()
        end
    end)
    if self.kind == "Hazard" then
        -- Другие тела пересекают полосу независимо от игрока.
        self.hazardScale = self.transform:get_child(0).scale
        self.trigger.body_exited:connect(self, function(s, other)
            if matchingPlayer(other) then s.inside = nil end
        end)
        self.timer.timeout:connect(self, function(s)
            setHazardActive(s, not s.active)
        end)
    end
end
function M.on_ready(self)
    self.start = self.transform.position
    W.get("game").changed:connect(self, function(s, kind)
        if kind ~= "restart" then return end
        s.transform.position = s.start
        s.taken, s.inside = false, nil
        if s.kind == "Hazard" then
            setHazardActive(s, true)
            s.timer:start()
        else
            s.trigger.enabled = true
        end
    end)
    if self.timer then self.timer:start() end
end
function M.on_update(self, dt)
    if self.inside and self.inside.is_valid and self.active then self.inside:hit() end
end
return M
