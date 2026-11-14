local W = require("res.Scripts.World")
return {
    action_available = function(self, context) return true end,
    action_entered = function(self, context)
        self.elapsed = 0
        print("AI ENTER Recharge")
    end,
    action_executing = function(self, context, dt)
        self.elapsed = self.elapsed + dt
        local owner = context.object:get_component(ComponentType.LuaScript).instance
        if not owner then return "running" end
        local energy = context.ai:get_float("Energy")
        local position = context.object.transform.position
        local direction = Vector3.zero()
        direction = Vector3.new(-position.x,-3.5-position.y,0)
        if direction:length() > .4 then direction = direction:normalized()*2.5
        else
            direction = Vector3.zero()
            context.ai:set_float("Energy", math.min(100,energy+dt*28))
        end
        owner.body.linear_velocity = direction
        return "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Recharge " .. reason)
    end
}
