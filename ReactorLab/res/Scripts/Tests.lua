local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    self.elapsed, self.step, self.hits = 0, 0, 0
    self.timer = self.object:get_component(ComponentType.Timer)
    self.timer.timeout:connect(self,function(s)
        s.hits = s.hits + 1
        if s.hits == 1 then
            W.check("timer one-shot stopped before callback",not s.timer.running and s.timer.time_left == 0)
            s.timer:start()
        elseif s.hits == 2 then
            s.timer.repeating = true; s.timer:start()
        elseif s.hits == 5 then
            s.timer:stop()
        end
    end)
end
function M.on_ready(self)
    self.zone, self.body = W.get("testzone"), W.get("testbody")
    self.ai = W.get("drone").ai
    W.check("timer autostart",self.timer.running)
    local old = self.timer.wait_time
    self.timer.wait_time = -1
    W.check("timer rejects nonpositive interval",self.timer.wait_time == old)
    -- Native delegate payload preserves nil slots and table identity.
    self.event = Delegate.new(self)
    local token, count = {}, 0
    local sub = self.event:connect(self,function(s, ...)
        local a,b,c = ...
        count = count + 1
        W.check("delegate payload and trailing nil",select("#",...) == 3 and a == token and b == 7 and c == nil)
    end)
    W.check("delegate live emit",self.event:emit(token,7,nil))
    W.check("delegate disconnect idempotent",sub:disconnect() and not sub:disconnect())
    self.event:emit(token,7,nil)
    W.check("delegate disconnected callback skipped",count == 1)
    -- Disconnect a later listener while dispatch is already in progress.
    local event, second, calls = Delegate.new(self), nil, 0
    event:connect(self,function(s) second:disconnect() end)
    second = event:connect(self,function(s) calls = calls + 1 end)
    event:emit()
    W.check("delegate disconnect during emission",calls == 0)
    local delayed, added, late = Delegate.new(self), false, 0
    delayed:connect(self,function(s)
        if not added then
            added = true
            delayed:connect(s,function() late = late + 1 end)
        end
    end)
    delayed:emit()
    W.check("delegate new subscription deferred",late == 0)
    delayed:emit()
    W.check("delegate new subscription next emission",late == 1)
    self.stale = self.body.event
    self.stale:connect(self,function() end)
    W.check("AI table loaded",self.ai.last_error == "" and #self.ai:get_parameters() == 3)
    local ok = self.ai:set_int("Mode",3)
    W.check("AI int roundtrip",ok and self.ai:get_int("Mode") == 3)
    local value, err = self.ai:get_float("Mode")
    W.check("AI type mismatch rejected",value == nil and err ~= "")
    ok = self.ai:set_float("Missing",1)
    W.check("AI unknown parameter rejected",not ok)
    local copy = self.ai:get_parameters(); copy[1].value = -500
    W.check("AI parameter snapshot isolated",self.ai:get_float("Energy") == 100)
end
function M.on_update(self, dt)
    self.elapsed = self.elapsed + dt
    if self.done then return end
    local t, z = self.elapsed, self.zone
    -- Keep scores deterministic while the real component executes action callbacks.
    if t < 1 then self.ai:set_float("Energy",100); self.ai:set_float("Distance",20)
    elseif t < 2 then self.ai:set_float("Energy",100); self.ai:set_float("Distance",1)
    elseif t < 3 then self.ai:set_float("Energy",0); self.ai:set_float("Distance",20) end
    if t >= .5 and self.step == 0 then
        W.check("trigger initially outside",z.entered == 0 and z.exited == 0)
        self.body.transform.position = Vector3.new(40,0,0)
        self.step = 1
    elseif t >= 1 and self.step == 1 then
        W.check("trigger enter deduplicates two colliders",z.entered == 1 and z.exited == 0)
        W.check("AI selects patrol",self.ai.current_action == "Patrol")
        z.trigger.enabled = false; self.step = 2
    elseif t >= 1.5 and self.step == 2 then
        W.check("trigger disabled emits exit",z.entered == 1 and z.exited == 1)
        W.check("timer restart repeat stop",self.hits == 5 and not self.timer.running and self.timer.time_left == 0)
        z.trigger.enabled = true; self.step = 3
    elseif t >= 2 and self.step == 3 then
        W.check("trigger reenabled emits enter",z.entered == 2 and z.exited == 1)
        W.check("AI switches to chase",self.ai.current_action == "Chase")
        self.body.transform.position = Vector3.new(44,0,0); self.step = 4
    elseif t >= 2.5 and self.step == 4 then
        W.check("trigger physical exit",z.entered == 2 and z.exited == 2)
        self.body.transform.position = Vector3.new(40,0,0); self.step = 5
    elseif t >= 3 and self.step == 5 then
        W.check("trigger physical reentry",z.entered == 3 and z.exited == 2)
        W.check("AI switches to recharge",self.ai.current_action == "Recharge")
        self.ai.enabled = false
        self.level:destroy_object(self.body.object)
        self.body = nil; self.step = 6
    elseif t >= 3.5 and self.step == 6 then
        W.check("trigger destroyed body has no exit",z.exited == 2)
        W.check("delegate destroyed owner emit false",self.stale:emit() == false)
        W.check("delegate destroyed owner connect rejected",not pcall(function() self.stale:connect(self,function() end) end))
        W.check("AI disabled exits action",self.ai.current_action == "")
        self.ai.enabled = true
        self.ai:set_float("Energy",100); self.ai:set_float("Distance",20)
        self.step = 7
    elseif t >= 4.5 and self.step == 7 then
        W.check("AI enabled resumes",self.ai.current_action == "Patrol")
        W.check("AI callbacks have no errors",self.ai.last_error == "")
        W.check("timer remains stopped",self.hits == 5)
        -- At the dock, a nearby player must not pull an undercharged drone away.
        local drone = W.get("drone")
        drone.body.linear_velocity = Vector3.zero()
        drone.transform.position = Vector3.new(0, -3.5, 0)
        self.ai:set_float("Distance", 1)
        self.ai:set_float("Energy", 29)
        self.rechargeStarted = t
        self.step = 8
    elseif self.step == 8 or self.step == 9 then
        local energy = self.ai:get_float("Energy")
        self.fullyCharged = self.fullyCharged or energy == 100
        if self.step == 8 and t - self.rechargeStarted >= 1 then
            W.check("AI keeps charging near a player",self.ai.current_action == "Recharge" and energy > 30 and energy < 100)
            self.step = 9
        elseif self.step == 9 and t - self.rechargeStarted >= 3.5 then
            W.check("AI resumes pursuit after a full charge",self.fullyCharged and self.ai.current_action == "Chase")
            print("TEST COMPLETE passes=" .. W.passes .. " failures=" .. W.failures)
            self.done = true
        end
    end
end
return M
