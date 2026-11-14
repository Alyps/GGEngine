return {on_init=function(s)
 local t=s.object:get_component(ComponentType.Timer)
 print("BEFORE timer.timeout GETTER")
 local e=t.timeout
 print("AFTER timer.timeout GETTER",type(e))
end}
