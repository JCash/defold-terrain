local M = {}

M.defaults = {}
M.options = {}

function M.init()
	M.add_value('wireframe', false)
end

function M._commit_values(t)
	--local polygon_mode = t['wireframe'] and render.POLYGON_MODE_LINE or render.POLYGON_MODE_FILL
	--render.set_polygon_mode(render.FACE_FRONT_AND_BACK, polygon_mode)
end

function M.commit_values()
	M._commit_values(M.options)
end

function M.commit_default_values()
	M._commit_values(M.defaults)
end

function M.add_value(name, default)
	M.options[name] = default
	M.defaults[name] = default
end

function M.get_default(name)
	return M.defaults[name]
end

function M.get_value(name)
	return M.options[name]
end

function M.set_value(name, value)
	M.options[name] = value
end

function M.toggle_value(name)
	M.options[name] = not M.options[name]
end

function M.on_message(message_id, message)
	if message_id == hash('gfx_dbg_toggle') then
		for i, name in ipairs(message) do
			M.toggle_value(name)
		end
	end
end

return M