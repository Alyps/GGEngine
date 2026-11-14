local World = require("res.Scripts.World")
local Arena = {}

local PLAYER_SPEED = 4.5
local ARENA_X = 7.85
local ARENA_Y = 3.95
local PICKUP_RADIUS = 0.67
local EXIT_RADIUS = 0.77
local DRONE_RADIUS = 0.73
local MAX_HEALTH = 3

local function closeTo(first, second, radius)
    local dx = first.x - second.x
    local dy = first.y - second.y
    return dx * dx + dy * dy <= radius * radius
end

local function away(position)
    return Vector3.new(1000, position.y, position.z)
end

local function pressed(input, first, second)
    return input:is_key_down(first) or input:is_key_down(second)
end

local function manualDirection(self)
    local input = self.input
    if not input or not input.active then return Vector3.zero() end

    local x, y = 0, 0
    if pressed(input, Keys.W, Keys.Up) then y = y + 1 end
    if pressed(input, Keys.S, Keys.Down) then y = y - 1 end
    if pressed(input, Keys.D, Keys.Right) then x = x + 1 end
    if pressed(input, Keys.A, Keys.Left) then x = x - 1 end
    return Vector3.new(x, y, 0)
end

function Arena.on_init(self)
    self.timer = assert(self.object:get_component(ComponentType.Timer), "Player needs a round Timer")
    self.start = self.transform.position
    self.collectibles = {}
    for index = 0, 2 do
        local node = assert(self.transform:get_child(index), "Player needs three Cell nodes")
        self.collectibles[index + 1] = { node = node, position = node.position }
    end
    local exit = assert(self.transform:get_child(3), "Player needs the Exit node")
    self.exitPosition = exit.position
    self.state = "ready"
    self.cells, self.health = 0, MAX_HEALTH
    World.register("arena", self)

    self.timer.timeout:connect(self, function (arena)
        arena:finish("lost", "timeout")
    end)
end

function Arena.on_ready(self)
    local pulse = World.get("pulse")
    if pulse then pulse:setActive(false) end
    if self.auto then self:restart() end
    print("CUBE RELAY | SPACE start | WASD or arrows | collect 3 yellow cubes | R retry")
end

function Arena.restart(self)
    self.transform.position = self.start
    self.cells, self.health = 0, MAX_HEALTH
    self.cooldown, self.routeIndex = 0, 1
    self.state, self.lossReason = "playing", nil

    -- Collectible nodes are in world space via their disabled inheritance flags.
    for _, cell in ipairs(self.collectibles) do
        cell.taken = false
        cell.node.position = cell.position
    end

    local pulse = World.get("pulse")
    if pulse then pulse:resetRound() end

    local drone = World.get("drone")
    if drone then drone:resetRound() end
    self.timer:start()
end

function Arena.finish(self, state, reason)
    if self.state ~= "playing" then return end

    self.state, self.lossReason = state, reason
    self.timer:stop()
    local pulse = World.get("pulse")
    if pulse then pulse.timer:stop() end
    local drone = World.get("drone")
    if drone then drone.ai.enabled = false end

    if reason then print("LOSS " .. reason) end
    print(state == "won" and "GAME WIN" or "GAME LOST")
end

function Arena.damage(self, source)
    if self.cooldown > 0 then return end
    self.health = self.health - 1
    self.cooldown = 1.25
    print("HIT " .. source .. " " .. self.health .. "/" .. MAX_HEALTH)
    if self.health == 0 then self:finish("lost", "damage") end
end

function Arena.updatePlayer(self, dt)
    local position = self.transform.position
    local step = PLAYER_SPEED * math.min(dt, 0.1)

    if self.auto then
        local cell = self.collectibles[self.routeIndex]
        local target = cell and cell.position or self.exitPosition
        local offset = Vector3.new(target.x - position.x, target.y - position.y, 0)
        local distance = offset:length()
        if distance <= step then
            position = Vector3.new(target.x, target.y, position.z)
            if cell then self.routeIndex = self.routeIndex + 1 end
        else
            position = position + offset * (step / distance)
        end
    else
        local direction = manualDirection(self)
        if direction:length_squared() > 0 then
            position = position + direction:normalized() * step
        end
    end

    position.x = math.max(-ARENA_X, math.min(ARENA_X, position.x))
    position.y = math.max(-ARENA_Y, math.min(ARENA_Y, position.y))
    self.transform.position = position
    return position
end

function Arena.on_update(self, dt)
    if self.input and self.input:was_key_pressed(Keys.R) then
        self:restart()
        print("GAME RESTART")
        return
    end
    if self.state == "ready" and self.input and self.input:was_key_pressed(Keys.Space) then
        self:restart()
        print("GAME START")
        return
    end
    if self.state ~= "playing" then return end

    self.cooldown = math.max(0, self.cooldown - dt)
    local position = self:updatePlayer(dt)

    local pulse = World.get("pulse")
    if pulse and pulse.active then
        local center = pulse.transform.position
        if math.abs(position.x - center.x) <= 2.5 and math.abs(position.y - center.y) <= 0.45 then
            self:damage("pulse")
        end
    end
    local drone = World.get("drone")
    if self.state == "playing" and drone and closeTo(position, drone.transform.position, DRONE_RADIUS) then
        self:damage("drone")
    end
    if self.state ~= "playing" then return end

    for _, cell in ipairs(self.collectibles) do
        if not cell.taken and closeTo(position, cell.position, PICKUP_RADIUS) then
            cell.taken = true
            cell.node.position = away(cell.position)
            self.cells = self.cells + 1
            print("CELLS " .. self.cells .. "/3")
        end
    end
    if self.cells == #self.collectibles and closeTo(position, self.exitPosition, EXIT_RADIUS) then
        self:finish("won")
    end
end

return Arena
