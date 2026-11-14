local World = require("res.Scripts.World")
local Arena = {}

local PLAYER_SPEED = 4.5
local PICKUP_RADIUS = 0.335
local EXIT_RADIUS = 0.385
-- Contact must reach the two character spheres (0.30 + 0.32), including skin.
local DRONE_RADIUS = 0.68
local MAX_HEALTH = 3

local function closeTo(first, second, radius)
    local dx = first.x - second.x
    local dz = first.z - second.z
    return dx * dx + dz * dz <= radius * radius
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
    return Vector3.new(x, 0, -y)
end

function Arena.on_init(self)
    self.timer = assert(self.object:get_component(ComponentType.Timer), "Player needs a round Timer")
    self.character = assert(self.object:get_component(ComponentType.CharacterMovement), "Player needs CharacterMovement")
    self.agent = assert(self.object:get_component(ComponentType.NavAgent), "Player needs NavAgent for autoplay")
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
    World.stop_navigation(self, self.agent)
    self.character:move(Vector3.zero())
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
    World.stop_navigation(self, self.agent)
    self.character:move(Vector3.zero())
    local pulse = World.get("pulse")
    if pulse then pulse.timer:stop() end
    local drone = World.get("drone")
    if drone then
        drone.ai.enabled = false
        drone.agent:stop()
    end

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
    if self.auto then
        -- Collect the next untaken cell; both collectibles and exit use world XZ.
        while self.collectibles[self.routeIndex] and self.collectibles[self.routeIndex].taken do
            self.routeIndex = self.routeIndex + 1
            World.stop_navigation(self, self.agent)
        end
        local cell = self.collectibles[self.routeIndex]
        local target = cell and cell.position or self.exitPosition
        World.navigate(self, self.agent, target, PLAYER_SPEED, dt)
    else
        -- CharacterMovement resolves floor and wall contacts; never teleport for walking.
        local direction = manualDirection(self)
        if direction:length_squared() > 0 then direction = direction:normalized() end
        self.character:move(direction * PLAYER_SPEED)
    end
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
        if math.abs(position.x - center.x) <= 1.25 and math.abs(position.z - center.z) <= 0.225 then
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
