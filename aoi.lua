
local radius = {x=1, y=1, z=0}
local pos1 = {5,5,0}
local pos2 = {7,7,0}

local del_t = {} -- del list
local add_t = {} -- add list

local dx = pos2[1] - pos1[1]
local dy = pos2[2] - pos1[2]
local dz = pos2[3] - pos1[3]
print("dalta = ", dx, dy, dz)
if math.abs(dx)<=2*radius.x and math.abs(dy)<=2*radius.y and math.abs(dz)<=2*radius.z then
	print("intersect\n")
else
	assert(nil, "no intersect")
end

function _fix(v, mv)
	v = math.max(v, 0)
	v = math.min(v, mv)
	return v
end

local range1 = {
	x = {_fix(pos1[1]-radius.x, 9), _fix(pos1[1]+radius.x, 9)},
	y = {_fix(pos1[2]-radius.y, 9), _fix(pos1[2]+radius.y, 9)},
	z = {_fix(pos1[3]-radius.z, 9), _fix(pos1[3]+radius.z, 9)},
}

local range2 = {
	x = {_fix(pos2[1]-radius.x, 9), _fix(pos2[1]+radius.x, 9)},
	y = {_fix(pos2[2]-radius.y, 9), _fix(pos2[2]+radius.y, 9)},
	z = {_fix(pos2[3]-radius.z, 9), _fix(pos2[3]+radius.z, 9)},
}

local foramt = 0

-- x轴方向
for i=1,math.abs(dx) do
	local s = dx/math.abs(dx)
	local x0 = pos1[1] + (i-1)*s
	local x1 = x0 - s * radius.x
	local x2 = x0 + s * radius.x + s
	--print(x1, x2, range1.y[1], range1.y[2])

	for y=range1.y[1], range1.y[2] do
		for z=range1.z[1], range1.z[2] do
			foramt = foramt + 1
			-- del
			if x1>=0 and x1<=9 then
				table.insert(del_t, {x1, y, z})
			end

			-- add
			if x2>=0 and x2<=9 then
				if y>=range2.y[1] and y<=range2.y[2]
					and z>=range2.z[1] and z<=range2.z[2] then
					table.insert(add_t, {x2, y, z})
				end
			end
		end
	end
	print("x move", #del_t, #add_t)
end

-- y轴方向
for i=1,math.abs(dy) do
	local s = dy/math.abs(dy)
	local y0 = pos1[2] + (i-1)*s
	local y1 = y0 - s * radius.y
	local y2 = y0 + s * radius.y + s
	--print(y1, y2, range2.x[1], range2.x[2])

	for x=range2.x[1], range2.x[2] do
		for z=range1.z[1], range1.z[2] do
			foramt = foramt + 1
			-- del
			if y1>=0 and y1<=9 then
				if x>=range1.x[1] and x<=range1.x[2] then
					table.insert(del_t, {x, y1, z})
				end
			end

			-- add
			if y2>=0 and y2<=9 then
				if z>=range2.z[1] and z<=range2.z[2] then
					table.insert(add_t, {x, y2, z})
				end
			end
		end
	end
	print("y move", #del_t, #add_t)
end

-- z轴方向
for i=1,math.abs(dz) do
	local s = dz/math.abs(dz)
	local z0 = pos1[3] + (i-1)*s
	local z1 = z0 - s * radius.z
	local z2 = z0 + s * radius.z + s

	for x=range2.x[1], range2.x[2] do
		for y=range2.y[1], range2.y[2] do
			foramt = foramt + 1
			-- del
			if z1>=0 and z1<=9 then
				if x>=range1.x[1] and x<=range1.x[2]
					and y>=range1.y[1] and y<=range1.y[2] then
					table.insert(del_t, {x, y, z1})
				end
			end

			-- add
			if z2>=0 and z2<=9 then
				table.insert(add_t, {x, y, z2})
			end
		end
	end

	print("z move", #del_t, #add_t)
end

print()
print("foramt = ", foramt)
print()

print(#del_t)
for _,v in ipairs(del_t) do
	print(string.format("del pos = (%d, %d, %d)", v[1], v[2], v[3]))
end

print()
print(#add_t)
for _,v in ipairs(add_t) do
	print(string.format("add pos = (%d, %d, %d)", v[1], v[2], v[3]))
end
