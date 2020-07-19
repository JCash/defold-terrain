local M = {}

M.unit = 1.0

local function set_vertex(index, v, n, uv, col, spos, snormal, stex, scolor)
	local s = M.unit
	spos[index*3+0+1] = v.x * s
	spos[index*3+1+1] = v.y * s
	spos[index*3+2+1] = v.z * s

	snormal[index*3+0+1] = n.x
	snormal[index*3+1+1] = n.y
	snormal[index*3+2+1] = n.z

	stex[index*2+0+1] = uv.x
	stex[index*2+1+1] = uv.y

	scolor[index*4+0+1] = col.x
	scolor[index*4+1+1] = col.y
	scolor[index*4+2+1] = col.z
	scolor[index*4+3+1] = col.w
end


function M.create_patch_flat(size, scale)
	local num_triangles = size*size*2
	local num_vertices = num_triangles * 3

	print("size", size)
	print("num_triangles", num_triangles)
	print("num_vertices", num_vertices)
	
	local newbuf = buffer.create( num_vertices, {
		{name=hash("position"), type=buffer.VALUE_TYPE_FLOAT32, count=3 },
		{name=hash("normal"), type=buffer.VALUE_TYPE_FLOAT32, count=3 },
		{name=hash("texcoord"), type=buffer.VALUE_TYPE_FLOAT32, count=2 },
		{name=hash("color"), type=buffer.VALUE_TYPE_FLOAT32, count=4 },
	})

	local stream_position = buffer.get_stream(newbuf, "position")
	local stream_normal = buffer.get_stream(newbuf, "normal")
	local stream_texcoord = buffer.get_stream(newbuf, "texcoord")
	local stream_color = buffer.get_stream(newbuf, "color")

	local index = 0
	for x=0,size-1 do
		for z=0,size-1 do
			-- topleft, bottom left, bottom right, top right
			local v0 = vmath.vector3(  x, 0, z) * scale
			local v1 = vmath.vector3(  x, 0, z+1) * scale
			local v2 = vmath.vector3(x+1, 0, z+1) * scale
			local v3 = vmath.vector3(x+1, 0, z) * scale
						
			local n0 = vmath.vector3(0,1,0)
			local n1 = vmath.vector3(0,1,0)
			local n2 = vmath.vector3(0,1,0)
			local n3 = vmath.vector3(0,1,0)

			local uv0 = vmath.vector3(0,1,0)
			local uv1 = vmath.vector3(0,1,0)
			local uv2 = vmath.vector3(0,1,0)
			local uv3 = vmath.vector3(0,1,0)

			local col0 = vmath.vector4(1,1,1,1)
			local col1 = vmath.vector4(1,1,1,1)
			local col2 = vmath.vector4(1,1,1,1)
			local col3 = vmath.vector4(1,1,1,1)

			set_vertex(index+0, v0, n0, uv0, col0, stream_position, stream_normal, stream_texcoord, stream_color)
			set_vertex(index+1, v1, n1, uv1, col1, stream_position, stream_normal, stream_texcoord, stream_color)
			set_vertex(index+2, v2, n2, uv2, col2, stream_position, stream_normal, stream_texcoord, stream_color)

			set_vertex(index+3, v2, n2, uv2, col2, stream_position, stream_normal, stream_texcoord, stream_color)
			set_vertex(index+4, v3, n3, uv3, col3, stream_position, stream_normal, stream_texcoord, stream_color)
			set_vertex(index+5, v0, n0, uv0, col0, stream_position, stream_normal, stream_texcoord, stream_color)
			index = index + 6
		end
	end

	return newbuf
end

return M