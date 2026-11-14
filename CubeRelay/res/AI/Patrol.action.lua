local World = require("res.Scripts.World")
local patrolX, patrolZ = 5.5, 0.9

return {
    action_available = function(self, context)
        local energy = context.ai:get_float("Energy")
        return World.playing() ~= nil and energy > 30 and context.ai.current_action ~= "Recharge"
    end,
    action_entered = function(self, context)
        self.agent = assert(context.object:get_component(ComponentType.NavAgent), "Drone needs NavAgent")
        World.stop_navigation(self, self.agent)
        local position = context.object.transform.position
        self.targetX = position.x >= 0 and -patrolX or patrolX
        print("DRONE Patrol")
    end,
    action_executing = function(self, context, dt)
        if not World.playing() then return "success" end
        local target = Vector3.new(self.targetX, 0, patrolZ)
        if World.navigate(self, self.agent, target, 1.8, dt) then
            self.targetX = -self.targetX
        end
        local energy = context.ai:get_float("Energy")
        context.ai:set_float("Energy", math.max(0, energy - dt * 7))
        return "running"
    end,
    action_exited = function(self, context, reason)
        World.stop_navigation(self, self.agent)
        self.targetX = nil
    end
}
