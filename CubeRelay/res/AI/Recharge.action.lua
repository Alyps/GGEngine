local World = require("res.Scripts.World")
local dockX, dockY = 0, -2.6

return {
    action_available = function(self, context)
        local energy = context.ai:get_float("Energy")
        -- Once charging starts it remains available until it reports success.
        return World.playing() ~= nil and (energy <= 30 or context.ai.current_action == "Recharge")
    end,
    action_entered = function(self, context)
        print("DRONE Recharge")
    end,
    action_executing = function(self, context, dt)
        if not World.playing() then return "success" end
        if not World.move_towards(context.object.transform, dockX, dockY, 2.65, dt) then
            return "running"
        end
        local energy = math.min(100, context.ai:get_float("Energy") + dt * 40)
        context.ai:set_float("Energy", energy)
        if energy == 100 then
            print("DRONE Charged")
            return "success"
        end
        return "running"
    end,
    action_exited = function(self, context, reason)
    end
}
