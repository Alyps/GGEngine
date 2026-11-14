local World = require("res.Scripts.World")
local patrolX, patrolY = 3.2, -0.9

return {
    action_available = function(self, context)
        local energy = context.ai:get_float("Energy")
        return World.playing() ~= nil and energy > 30 and context.ai.current_action ~= "Recharge"
    end,
    action_entered = function(self, context)
        local position = context.object.transform.position
        self.targetX = position.x >= 0 and -patrolX or patrolX
        print("DRONE Patrol")
    end,
    action_executing = function(self, context, dt)
        if not World.playing() then return "success" end
        if World.move_towards(context.object.transform, self.targetX, patrolY, 1.8, dt) then
            self.targetX = -self.targetX
        end
        local energy = context.ai:get_float("Energy")
        context.ai:set_float("Energy", math.max(0, energy - dt * 7))
        return "running"
    end,
    action_exited = function(self, context, reason)
        self.targetX = nil
    end
}
