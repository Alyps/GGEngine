local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("game", self)
    self.changed = Delegate.new(self)
    self.finished = Delegate.new(self)
    self.timer = self.object:get_component(ComponentType.Timer)
    self.cells, self.health, self.state = 0, 3, "playing"
    self.timer.timeout:connect(self, function(s) s:finish(false) end)
end
function M.on_ready(self)
    self.timer:start()
    print("REACTOR LAB | WASD move | collect 3 gold cells, reach EXIT | R restart")
end
function M.finish(self, won)
    if self.state ~= "playing" then return end
    self.state = won and "won" or "lost"
    self.timer:stop()
    self.finished:emit(self.state)
    print(won and "GAME WIN" or "GAME LOST")
end
function M.collect(self)
    if self.state ~= "playing" then return end
    self.cells = self.cells + 1
    self.changed:emit("cell", self.cells)
    print("CELLS " .. self.cells .. "/3")
end
function M.damage(self)
    if self.state ~= "playing" then return end
    self.health = self.health - 1
    self.changed:emit("health", self.health)
    if self.health <= 0 then self:finish(false) end
end
function M.on_update(self, dt)
    if self.input and self.input:was_key_pressed(Keys.R) then
        self.cells, self.health, self.state = 0, 3, "playing"
        self.timer:start()
        self.changed:emit("restart", 0)
        print("GAME RESTART")
    end
end
return M
