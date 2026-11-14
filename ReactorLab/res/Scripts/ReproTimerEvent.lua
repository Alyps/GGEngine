local W = require("res.Scripts.World")
return {
    on_init = function(self)
        local timer = self.object:get_component(ComponentType.Timer)
        print("BEFORE timer.timeout GETTER")
        local event = timer.timeout
        print("AFTER timer.timeout GETTER", type(event))
        W.check("timer timeout exposes a subscription-only event",
            type(event) == "table" and type(event.connect) == "function" and event.emit == nil)
        W.check("timer timeout preserves event identity", event ~= nil and event == timer.timeout)
        print("CHECK COMPLETE passes=" .. W.passes .. " failures=" .. W.failures)
    end
}
