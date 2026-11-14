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

-- Replan moving targets at a bounded rate without resetting the agent every frame.
function World.navigate(state, agent, target, speed, dt)
    agent.speed = speed
    state.repath = math.max(0, (state.repath or 0) - dt)
    local previous = state.navTarget
    local changed = not previous or (target - previous):length_squared() > 0.16
    local retry = agent.state == "failed" or agent.state == "idle"
        or (agent.state == "arrived" and agent.is_partial)

    if state.repath == 0 and (changed or retry) then
        state.navTarget = target
        state.repath = 0.35
        if not agent:set_destination(target) then
            -- Report one error per failure episode; retry without moving through walls.
            if not state.navError then print("NAVIGATION " .. agent.last_error) end
            state.navError = true
        else
            state.navError = false
        end
    end
    return agent.state == "arrived" and not agent.is_partial and not changed
end

function World.stop_navigation(state, agent)
    agent:stop()
    state.navTarget, state.repath, state.navError = nil, nil, nil
end

return World
