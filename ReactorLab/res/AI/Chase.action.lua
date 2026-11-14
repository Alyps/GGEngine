local W = require("res.Scripts.World")
return {
    action_available = function(self, context)
        return context.ai:get_float("Energy") > 30 and context.ai.current_action ~= "Recharge"
            and context.ai:get_float("Distance") < 7
    end,
    action_entered = function(self, context)
        print("AI ENTER Chase")
    end,
    action_executing = function(self, context, dt)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if not body then return "failure" end
        local energy = context.ai:get_float("Energy")
        local position = context.object.transform.position
        local player = W.get("player")
        local direction = player and player.transform.position - position or Vector3.zero()
        body.linear_velocity = direction:length() > .1 and direction:normalized() * 2.2 or Vector3.zero()
        context.ai:set_float("Energy", math.max(0, energy - dt * 12))
        return "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Chase " .. reason)
    end
}
