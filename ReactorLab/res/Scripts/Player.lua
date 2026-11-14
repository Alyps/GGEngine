local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("player", self)
    self.body = self.object:get_component(ComponentType.KinematicRigidBody)
    self.cooldown, self.route = 0, 1
    self.auto = self.auto or false
end
function M.on_ready(self)
    local game = W.get("game")
    game.changed:connect(self, function(s, kind)
        if kind == "restart" then
            s.transform.position = Vector3.new(-8, -4, 0)
            s.cooldown, s.route = 0, 1
        end
    end)
end
function M.hit(self)
    if self.cooldown > 0 then return end
    self.cooldown = 1.5
    W.get("game"):damage()
end
function M.on_update(self, dt)
    self.cooldown = math.max(0, self.cooldown - dt)
    local direction = Vector3.zero()
    local game = W.get("game")
    if game and game.state == "playing" then
        if self.auto then
            -- This route crosses actual pickup and exit triggers, not synthetic emits.
            local route = {{-7,3},{0,4},{7,3},{8,-4}}
            local target = route[self.route]
            if target then
                local p = self.transform.position
                direction = Vector3.new(target[1]-p.x,target[2]-p.y,0)
                if direction:length() < .25 then self.route = self.route + 1; direction = Vector3.zero() end
            end
        elseif self.input and self.input.active then
            if self.input:is_key_down(Keys.W) then direction.y = direction.y + 1 end
            if self.input:is_key_down(Keys.S) then direction.y = direction.y - 1 end
            if self.input:is_key_down(Keys.A) then direction.x = direction.x - 1 end
            if self.input:is_key_down(Keys.D) then direction.x = direction.x + 1 end
        end
    end
    if direction:length_squared() > 0 then direction = direction:normalized() * 4 end
    -- Arena bounds are explicit; kinematic bodies do not collide with static walls.
    local p = self.transform.position
    if (p.x < -9 and direction.x < 0) or (p.x > 9 and direction.x > 0) then direction.x = 0 end
    if (p.y < -4.8 and direction.y < 0) or (p.y > 4.8 and direction.y > 0) then direction.y = 0 end
    self.body.linear_velocity = direction
end
return M
