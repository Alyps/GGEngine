local W=require("res.Scripts.World")
local M={}
function M.on_init(s)
 print("BEGIN UtilityAI")
 s.ai=s.object:get_component(ComponentType.UtilityAI)
 s.elapsed,s.step=0,0
 W.check("AI resource load",s.ai.last_error=="")
 W.check("AI float default",s.ai:get_float("Score")==0)
 W.check("AI int setter",s.ai:set_int("Count",4) and s.ai:get_int("Count")==4)
 local value,err=s.ai:get_float("Count")
 W.check("AI type mismatch",value==nil and err~="")
 W.check("AI unknown parameter",not s.ai:set_float("Unknown",1))
 local copy=s.ai:get_parameters();copy[1].value=900
 W.check("AI snapshot isolated",s.ai:get_float("Score")==0)
end
function M.on_update(s,dt)
 s.elapsed=s.elapsed+dt
 if s.elapsed>.5 and s.step==0 then
  W.check("AI first action",s.ai.current_action=="Low")
  s.ai:set_float("Score",100);s.step=1
 elseif s.elapsed>1 and s.step==1 then
  W.check("AI switches action",s.ai.current_action=="High")
  s.ai.enabled=false;s.step=2
 elseif s.elapsed>1.5 and s.step==2 then
  W.check("AI disabled exits",s.ai.current_action=="")
  s.ai.enabled=true;s.step=3
 elseif s.elapsed>2 and s.step==3 then
  W.check("AI reenabled",s.ai.current_action=="High")
  W.check("AI no callback error",s.ai.last_error=="")
  s.step=4;print("CHECK COMPLETE passes="..W.passes.." failures="..W.failures)
 end
end
return M
