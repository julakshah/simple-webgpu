struct VertexIn {
	@location(0) pos: vec3f,
};

struct VertexOut {
	@builtin(position) pos: vec4f,
	@location(0) color: vec3f,
};

struct Transforms {
    tf1: mat4x4<f32>,
    tf2: mat4x4<f32>,
};

//@group(0) @binding(0) var<uniform> pointBuffer: array<f32>;
//@group(0) @binding(1) var<uniform> indexBuffer: array<i32>;
@group(0) @binding(0) var<uniform> transformBuffer: Transforms;

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    var out: VertexOut;
	let ratio = 640.0 / 480.0;
	var offset = vec2f(0.0);

    let angle = 0.5;
    let alpha = cos(angle);
	let beta = sin(angle);
	var pos = vec3f(
		in.pos.x,
		alpha * in.pos.y + beta * in.pos.z,
		alpha * in.pos.z - beta * in.pos.y,
	);
	out.pos = vec4f(pos.x, pos.y * ratio, pos.z * 0.5 + 0.5, 1.0);	

	out.color = vec3f(0.5, 0.5, 0.9);
	return out;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4f {
	let color: vec3<f32> = in.color;
	return vec4f(color, 1.0);
}