local W=require("res.Scripts.World")
local M={}
function M.on_init(s)
 print("BEGIN Timer")
 s.timer=s.object:get_component(ComponentType.Timer)
 W.check("timer initial stopped",not s.timer.running and s.timer.time_left==0)
 s.timer.wait_time=.2
 local previous=s.timer.wait_time
 s.timer.wait_time=-1
 W.check("timer invalid interval rejected",s.timer.wait_time==previous)
 s.timer:start()
 W.check("timer start",s.timer.running and s.timer.time_left>0)
 s.elapsed,s.step=0,0
end
function M.on_update(s,dt)
 s.elapsed=s.elapsed+dt
 if s.elapsed>.5 and s.step==0 then
  W.check("timer one-shot expires",not s.timer.running and s.timer.time_left==0)
  s.timer.repeating=true; s.timer:start();s.step=1
 elseif s.elapsed>1 and s.step==1 then
  W.check("timer repeats",s.timer.running and s.timer.time_left>0 and s.timer.time_left<=s.timer.wait_time)
  s.timer:stop()
  W.check("timer stop",not s.timer.running and s.timer.time_left==0)
  s.timer:start()
  W.check("timer restart",s.timer.running and s.timer.time_left==s.timer.wait_time)
  s.timer:stop();s.step=2
  print("CHECK COMPLETE passes="..W.passes.." failures="..W.failures)
 end
end
return M
