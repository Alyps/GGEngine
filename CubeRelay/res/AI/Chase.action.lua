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
        print("DRONE Chase")
    end,
    action_executing = function(self, context, dt)
        local arena = World.playing()
        if not arena then return "success" end
        local position = arena.transform.position
        World.move_towards(context.object.transform, position.x, position.y, 2.35, dt)
        local energy = context.ai:get_float("Energy")
        context.ai:set_float("Energy", math.max(0, energy - dt * 16))
        return "running"
    end,
    action_exited = function(self, context, reason)
    end
}
