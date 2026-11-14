local W = require("res.Scripts.World")
return {
    action_available = function(self, context) return true end,
    action_entered = function(self, context)
        self.elapsed = 0
        print("AI ENTER Patrol")
    end,
    action_executing = function(self, context, dt)
        self.elapsed = self.elapsed + dt
        local owner = context.object:get_component(ComponentType.LuaScript).instance
        if not owner then return "running" end
        local energy = context.ai:get_float("Energy")
        local position = context.object.transform.position
        local direction = Vector3.zero()
        local x = math.sin(self.elapsed * .6) * 4
        direction = Vector3.new(x-position.x, -2-position.y, 0)
        if direction:length() > .1 then direction = direction:normalized()*1.5 end
        context.ai:set_float("Energy", math.max(0,energy-dt*4))
        owner.body.linear_velocity = direction
        return "running"
    end,
    action_exited = function(self, context, reason)
        local body = context.object:get_component(ComponentType.KinematicRigidBody)
        if body then body.linear_velocity = Vector3.zero() end
        print("AI EXIT Patrol " .. reason)
    end
}
