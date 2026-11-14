local World = {}
local instances = {}

function World.register(name, instance)
    instances[name] = instance
end

function World.get(name)
    local instance = instances[name]
    -- The engine invalidates script instances before releasing their objects.
    if instance and instance.is_valid then
        return instance
    end
    instances[name] = nil
    return nil
end

function World.playing()
    local arena = World.get("arena")
    if arena and arena.state == "playing" then
        return arena
    end
    return nil
end

function World.move_towards(transform, x, y, speed, dt)
    local position = transform.position
    local dx, dy = x - position.x, y - position.y
    local distance = math.sqrt(dx * dx + dy * dy)
    local step = speed * dt
    -- Clip the last step to the target so the drone settles exactly at the dock.
    if distance <= step then
        transform.position = Vector3.new(x, y, position.z)
        return true
    end
    local fraction = step / distance
    transform.position = Vector3.new(position.x + dx * fraction, position.y + dy * fraction, position.z)
    return false
end

return World
