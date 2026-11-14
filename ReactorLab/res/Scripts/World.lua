-- Explicit scene registry: Lua has no object search API. Instances own their state.
local W = { refs = {}, passes = 0, failures = 0 }
function W.register(name, instance) W.refs[name] = instance end
function W.get(name)
    local value = W.refs[name]
    return value and value.is_valid and value or nil
end
function W.check(name, ok)
    if ok then W.passes = W.passes + 1 else W.failures = W.failures + 1 end
    print((ok and "PASS " or "FAIL ") .. name)
end
return W
