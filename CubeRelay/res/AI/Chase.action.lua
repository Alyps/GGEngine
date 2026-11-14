local World = require("res.Scripts.World")

return {
    action_available = function(self, context)
        local energy = context.ai:get_float("Energy")
        local distance = context.ai:get_float("Distance")
        -- Use a slightly larger exit radius to avoid flickering at the boundary.
        local radius = context.ai.current_action == "Chase" and 6.2 or 5.5
        return World.playing() ~= nil and energy > 30 and distance < radius
            and context.ai.current_action ~= "Recharge"
    end,
    action_entered = function(self, context)
        self.agent = assert(context.object:get_component(ComponentType.NavAgent), "Drone needs NavAgent")
        World.stop_navigation(self, self.agent)
        print("DRONE Chase")
    end,
    action_executing = function(self, context, dt)
        local arena = World.playing()
        if not arena then return "success" end
        local position = arena.transform.position
        World.navigate(self, self.agent, position, 2.35, dt)
        local energy = context.ai:get_float("Energy")
        context.ai:set_float("Energy", math.max(0, energy - dt * 16))
        return "running"
    end,
    action_exited = function(self, context, reason)
        World.stop_navigation(self, self.agent)
    end
}
