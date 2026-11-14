local World = require("res.Scripts.World")
local Hud = {}

-- Child order matches the HUD object in the level asset.
local Time = 0
local Life = 1
local Cell = 4
local Patrol = 7
local Chase = 8
local Recharge = 9
local Won = 10
local Lost = 11
local Safe = 12
local Danger = 13
local Start = 14

local function setVisible(item, visible)
    if item.visible == visible then return end
    item.visible = visible
    if visible then
        item.node.position = item.position
    else
        -- There is no mesh visibility property in Lua. Park hidden quads behind the camera.
        item.node.position = Vector3.new(item.position.x, item.position.y, 40)
    end
end

function Hud.on_init(self)
    self.items = {}
    for index = Time, Start do
        local node = self.transform:get_child(index)
        assert(node, "Cube Relay HUD is missing a display node")
        self.items[index] = { node = node, position = node.position, scale = node.scale, visible = true }
    end
    for index = Patrol, Danger do
        setVisible(self.items[index], false)
    end
end

function Hud.on_update(self, dt)
    local arena = World.get("arena")
    if not arena then return end

    -- Keep the left edge of the bar fixed while its fill drains to zero.
    local fill = self.items[Time]
    local fraction = arena.state == "ready" and 1
        or math.max(0, math.min(1, arena.timer.time_left / arena.timer.wait_time))
    local width = fill.scale.x * math.max(0.001, fraction)
    fill.node.scale = Vector3.new(width, fill.scale.y, fill.scale.z)
    fill.node.position = Vector3.new(fill.position.x - (fill.scale.x - width) * 0.5, fill.position.y, fill.position.z)

    for index = 0, 2 do
        setVisible(self.items[Life + index], index < arena.health)
        setVisible(self.items[Cell + index], index < arena.cells)
    end

    local drone = World.get("drone")
    local action = drone and drone.ai.current_action
    setVisible(self.items[Patrol], action == "Patrol")
    setVisible(self.items[Chase], action == "Chase")
    setVisible(self.items[Recharge], action == "Recharge")

    local pulse = World.get("pulse")
    setVisible(self.items[Safe], arena.state == "playing" and pulse ~= nil and not pulse.active)
    setVisible(self.items[Danger], arena.state == "playing" and pulse ~= nil and pulse.active)
    setVisible(self.items[Won], arena.state == "won")
    setVisible(self.items[Lost], arena.state == "lost")
    setVisible(self.items[Start], arena.state == "ready")
end

return Hud
