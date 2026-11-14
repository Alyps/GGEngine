local World = require("res.Scripts.World")
local dockX, dockZ = 0, 2.6

return {
    action_available = function(self, context)
        local energy = context.ai:get_float("Energy")
        -- Once charging starts it remains available until it reports success.
        return World.playing() ~= nil and (energy <= 30 or context.ai.current_action == "Recharge")
    end,
    action_entered = function(self, context)
        self.agent = assert(context.object:get_component(ComponentType.NavAgent), "Drone needs NavAgent")
        World.stop_navigation(self, self.agent)
        print("DRONE Recharge")
    end,
    action_executing = function(self, context, dt)
        if not World.playing() then return "success" end
        local target = Vector3.new(dockX, 0, dockZ)
        if not World.navigate(self, self.agent, target, 2.65, dt) then
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
        World.stop_navigation(self, self.agent)
    end
}
