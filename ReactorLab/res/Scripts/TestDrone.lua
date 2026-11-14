local W = require("res.Scripts.World")
local M = {}
function M.on_init(self)
    W.register("drone", self)
    self.ai = self.object:get_component(ComponentType.UtilityAI)
    self.body = self.object:get_component(ComponentType.KinematicRigidBody)
end
return M
