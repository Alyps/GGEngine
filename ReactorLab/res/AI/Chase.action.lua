local W = require("res.Scripts.World")
return {
    action_available = function(self, context) return true end,
    action_entered = function(self, context)
        self.elapsed = 0
        print("AI ENTER Chase")
    end,
    action_executing = function(self, context, dt)
        self.elapsed = self.elapsed + dt
        local owner = context.object:get_component(ComponentType.LuaScript).instance
        if not owner then return "running" end
        local energy = context.ai:get_float("Energy")
        local position = context.object.transform.position
        local direction = Vector3.zero()
        local player = W.get("player")
        if player then direction = player.transform.position - position end
        if direction:length() > .1 then direction = direction:normalized()*2.2 end
        context.ai:set_float("Energy", math.max(0,energy-dt*12))
        owner.body.linear_velocity = direction
        return "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Chase " .. reason)
    end
}
