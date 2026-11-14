local W = require("res.Scripts.World")

local function canSubscribe(event)
    return type(event) == "table" and type(event.connect) == "function" and event.emit == nil
end

return {
    on_init = function(self)
        local trigger = self.object:get_component(ComponentType.BoxTrigger)
        print("BEFORE trigger.body_entered GETTER")
        local entered = trigger.body_entered
        print("AFTER trigger.body_entered GETTER", type(entered))
        local exited = trigger.body_exited
        W.check("trigger exposes subscription-only enter and exit events", canSubscribe(entered) and canSubscribe(exited))
        W.check("trigger events have distinct stable identities",
            entered ~= exited and entered == trigger.body_entered and exited == trigger.body_exited)
        print("CHECK COMPLETE passes=" .. W.passes .. " failures=" .. W.failures)
    end
}
