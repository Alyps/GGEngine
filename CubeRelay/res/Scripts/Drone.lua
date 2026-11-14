local World = require("res.Scripts.World")
local Drone = {}

function Drone.on_init(self)
    self.start = self.transform.position
    self.ai = assert(self.object:get_component(ComponentType.UtilityAI), "Drone needs Utility AI")
    self.agent = assert(self.object:get_component(ComponentType.NavAgent), "Drone needs NavAgent")
    World.register("drone", self)
end

function Drone.resetRound(self)
    -- Cancel the active action before restoring the round state.
    self.ai.enabled = false
    self.awaitReset = true
    self.agent:stop()
    self.transform.position = self.start
    assert(self.ai:set_float("Energy", 100))
    assert(self.ai:set_float("Distance", 100))
end

function Drone.on_update(self)
    local arena = World.get("arena")
    if not arena or arena.state ~= "playing" then return end

    local player = arena.transform.position
    local position = self.transform.position
    local dx, dz = player.x - position.x, player.z - position.z
    self.ai:set_float("Distance", math.sqrt(dx * dx + dz * dz))

    if self.awaitReset and self.ai.current_action == "" then
        self.awaitReset = false
        self.ai.enabled = true
    end
end

return Drone
