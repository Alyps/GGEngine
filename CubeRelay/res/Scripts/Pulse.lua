local World = require("res.Scripts.World")
local Pulse = {}

function Pulse.setActive(self, active)
    self.active = active
    self.live.position = active and self.livePosition or Vector3.new(1000, 0, 0)
    self.safe.position = active and Vector3.new(1000, 0, 0) or self.safePosition
    print(active and "PULSE LIVE" or "PULSE SAFE")
end

function Pulse.on_init(self)
    self.timer = assert(self.object:get_component(ComponentType.Timer), "Pulse needs a repeating Timer")
    self.live = assert(self.transform:get_child(0), "Pulse needs the Live node")
    self.safe = assert(self.transform:get_child(1), "Pulse needs the Safe node")
    self.livePosition, self.safePosition = self.live.position, self.safe.position
    World.register("pulse", self)

    self.timer.timeout:connect(self, function (pulse)
        pulse:setActive(not pulse.active)
    end)
end

function Pulse.resetRound(self)
    self:setActive(false)
    self.timer:start()
end

return Pulse
