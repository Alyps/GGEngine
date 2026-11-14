local World = require("res.Scripts.World")
local Drone = {}

function Drone.on_init(self)
    self.start = self.transform.position
    self.ai = assert(self.object:get_component(ComponentType.UtilityAI), "Drone needs Utility AI")
    World.register("drone", self)
end

function Drone.resetRound(self)
    -- Отмена текущего действия сбрасывает его фазу перед новым выбором.
    self.ai.enabled = false
    self.awaitReset = true
    self.transform.position = self.start
    assert(self.ai:set_float("Energy", 100))
    assert(self.ai:set_float("Distance", 100))
end

function Drone.on_update(self)
    local arena = World.get("arena")
    if not arena or arena.state ~= "playing" then return end

    local player = arena.transform.position
    local position = self.transform.position
    local dx, dy = player.x - position.x, player.y - position.y
    self.ai:set_float("Distance", math.sqrt(dx * dx + dy * dy))

    if self.awaitReset and self.ai.current_action == "" then
        self.awaitReset = false
        self.ai.enabled = true
    end
end

return Drone
