return {
    action_available = function(self, context)
        -- Возвращение на док продолжается после пересечения порога заряда.
        return context.ai:get_float("Energy") <= 30 or context.ai.current_action == "Recharge"
    end,
    action_entered = function(self, context)
        print("AI ENTER Recharge")
    end,
    action_executing = function(self, context, dt)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if not body then return "failure" end
        local position = context.object.transform.position
        local direction = Vector3.new(-position.x, -3.5 - position.y, 0)
        if direction:length() > .4 then
            body.linear_velocity = direction:normalized() * 2.5
            return "running"
        end
        body.linear_velocity = Vector3.zero()
        local energy = math.min(100, context.ai:get_float("Energy") + dt * 28)
        context.ai:set_float("Energy", energy)
        return energy >= 100 and "success" or "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Recharge " .. reason)
    end
}
