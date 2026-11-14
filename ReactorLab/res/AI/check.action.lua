return {
 action_entered=function(s,context) print("CHECK AI ENTER",context.ai.current_action) end,
 action_executing=function(s,context,dt) return "running" end,
 action_exited=function(s,context,reason) print("CHECK AI EXIT",reason) end
}