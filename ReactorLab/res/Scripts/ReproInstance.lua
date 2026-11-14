return {on_ready=function(s)
 local component=s.object:get_component(ComponentType.LuaScript)
 print("BEFORE LuaScript.instance GETTER")
 local value=component.instance
 print("AFTER LuaScript.instance GETTER",type(value))
end}
