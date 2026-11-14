return {on_init=function(s)
 local t=s.object:get_component(ComponentType.BoxTrigger)
 print("BEFORE trigger.body_entered GETTER")
 local e=t.body_entered
 print("AFTER trigger.body_entered GETTER",type(e))
end}
