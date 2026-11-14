local W = require("res.Scripts.World")
return {
    on_ready = function(self)
        local component = self.object:get_component(ComponentType.LuaScript)
        print("BEFORE LuaScript.instance GETTER")
        local instance = component.instance
        print("AFTER LuaScript.instance GETTER", type(instance))
        W.check("LuaScript instance is the live callback self",
            instance == self and instance.is_valid and instance.object == self.object)
        local agent = self.object:get_component(ComponentType.NavAgent)
        local path = agent.path
        local emptyPath = type(path) == "table" and #path == 0
        if emptyPath then path[1] = Vector3.zero() end
        W.check("NavAgent path returns an independent snapshot", emptyPath and #agent.path == 0)
        print("CHECK COMPLETE passes=" .. W.passes .. " failures=" .. W.failures)
    end
}
