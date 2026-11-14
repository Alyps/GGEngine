return {
    action_available = function(self, context)
        return context.ai:get_float("Energy") > 30 and context.ai.current_action ~= "Recharge"
    end,
    action_entered = function(self, context)
        self.elapsed = 0
        print("AI ENTER Patrol")
    end,
    action_executing = function(self, context, dt)
        self.elapsed = self.elapsed + dt
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if not body then return "failure" end
        local energy = context.ai:get_float("Energy")
        local position = context.object.transform.position
        local x = math.sin(self.elapsed * .6) * 4
        local direction = Vector3.new(x - position.x, -2 - position.y, 0)
        body.linear_velocity = direction:length() > .1 and direction:normalized() * 1.5 or Vector3.zero()
        context.ai:set_float("Energy", math.max(0, energy - dt * 4))
        return "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Patrol " .. reason)
    end
}
