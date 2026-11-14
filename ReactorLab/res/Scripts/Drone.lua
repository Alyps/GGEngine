local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("drone", self)
    self.ai = self.object:get_component(ComponentType.UtilityAI)
    self.body = self.object:get_component(ComponentType.KinematicRigidBody)
end
function M.on_ready(self)
    local game = W.get("game")
    game.changed:connect(self, function(s, kind)
        if kind == "restart" then
            -- Дать действию прошлого раунда завершиться перед новым выбором.
            s.ai.enabled = false
            s.restartPending = true
            s.body.linear_velocity = Vector3.zero()
            s.transform.position = Vector3.new(0, -2, 0)
            s.ai:set_float("Energy", 100)
        end
    end)
    game.finished:connect(self, function(s)
        s.ai.enabled = false
        s.body.linear_velocity = Vector3.zero()
    end)
end
function M.on_update(self, dt)
    local player, game = W.get("player"), W.get("game")
    if not player or not game then return end
    if game.state ~= "playing" then self.ai.enabled = false; self.body.linear_velocity = Vector3.zero(); return end
    if self.restartPending and self.ai.current_action == "" then
        self.restartPending = false
        self.ai.enabled = true
    end
    local distance = (player.transform.position - self.transform.position):length()
    self.ai:set_float("Distance", distance)
    if distance < .85 then player:hit() end
end
return M
