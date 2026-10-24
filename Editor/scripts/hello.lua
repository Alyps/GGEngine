-- Тестовый скрипт для Transform и компонентов: вращает объект и пульсирует светом.
local M = {}

function M.on_init(self)
	self.t = 0
	-- Ищем компонент света на том же объекте; у куба его нет, и результат nil.
	self.light = self.object:get_component("PointLight")
	if self.light then
		self.base_intensity = self.light.intensity
		self.light.color = Color.new(1.0, 0.5, 0.0)
		print("light script: base intensity", self.base_intensity)
	else
		print("spin script on object at", self.transform.position)
	end
end

function M.on_update(self, dt)
	self.t = self.t + dt

	if self.light then
		-- Интенсивность колеблется вокруг исходной.
		self.light.intensity = self.base_intensity * (1.0 + 0.5 * math.sin(self.t * 3.0))
	else
		-- Вращение вокруг оси Y, 90 градусов в секунду.
		self.transform:rotate(Vector3.unit_y(), math.rad(90) * dt)
	end
end

return M
