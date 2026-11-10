//! The warp renderer: one texture drawn over a deformed mesh.
//!
//! Smithay's texture path draws axis-aligned quads only, so a bent window has
//! to go through raw GL. [`WarpElement`] is an ordinary render element — the
//! damage tracker sees its bounding box and commit like any other — whose
//! `draw` runs a small program of its own inside `GlesFrame::with_context`.
//! Wobbly windows and the magic lamp are the two deformation sources; both
//! hand over a grid of positions and share everything else.

use std::ffi::CStr;
use std::rc::Rc;

use smithay::backend::allocator::Fourcc;
use smithay::backend::renderer::element::{Element, Id, Kind, RenderElement, UnderlyingStorage};
use smithay::backend::renderer::gles::{
    Capability, GlesError, GlesFrame, GlesRenderer, GlesTexture, ffi,
};
use smithay::backend::renderer::utils::CommitCounter;
use smithay::backend::renderer::{Bind as _, Color32F, Frame as _, Offscreen, Renderer as _};
use smithay::utils::user_data::UserDataMap;
use smithay::utils::{Buffer, Physical, Point, Rectangle, Scale, Size, Transform};

use driftwm::magic_lamp::Lamp;
use driftwm::wobbly::{Offsets, patch_offset};

const VERTEX_SRC: &str = include_str!("../shaders/warp.vert");
const FRAGMENT_SRC: &str = include_str!("../shaders/warp.frag");
const BURN_FRAGMENT_SRC: &str = include_str!("../shaders/burn.frag");

/// Offscreen allocations are rounded up to this, so a window whose painted
/// box jitters by a pixel under a fractional zoom keeps its texture.
const ALLOC_QUANTUM: i32 = 64;
/// Larger than any real window; past it the warp is skipped and the caller
/// draws the plain picture.
const MAX_TEXTURE_SIDE: i32 = 8192;
/// Mesh cells are about this many physical pixels on a side...
const CELL_PX: f64 = 24.0;
/// ...within these counts per axis. The upper bound keeps the index in `u16`.
const MIN_CELLS: usize = 8;
const MAX_CELLS: usize = 48;

#[derive(Debug)]
struct ProgramInner {
    program: ffi::types::GLuint,
    a_pos: ffi::types::GLuint,
    a_uv: ffi::types::GLuint,
    u_matrix: ffi::types::GLint,
    u_local: ffi::types::GLint,
    u_tex: ffi::types::GLint,
    u_alpha: ffi::types::GLint,
    u_clip: ffi::types::GLint,
    u_used: ffi::types::GLint,
    u_alloc: ffi::types::GLint,
    /// The close effects' own uniforms; absent (-1) from the plain warp.
    u_effect: ffi::types::GLint,
    u_progress: ffi::types::GLint,
    u_seed: ffi::types::GLint,
    u_pad: ffi::types::GLint,
}

/// The compiled mesh program. Lives as long as the renderer's GL context.
#[derive(Debug, Clone)]
pub struct WarpProgram(Rc<ProgramInner>);

unsafe fn compile_stage(
    gl: &ffi::Gles2,
    kind: ffi::types::GLenum,
    src: &str,
) -> Option<ffi::types::GLuint> {
    unsafe {
        let shader = gl.CreateShader(kind);
        let ptr = src.as_ptr() as *const ffi::types::GLchar;
        let len = src.len() as ffi::types::GLint;
        gl.ShaderSource(shader, 1, &ptr, &len);
        gl.CompileShader(shader);
        let mut status = ffi::FALSE as ffi::types::GLint;
        gl.GetShaderiv(shader, ffi::COMPILE_STATUS, &mut status);
        if status == ffi::FALSE as ffi::types::GLint {
            gl.DeleteShader(shader);
            return None;
        }
        Some(shader)
    }
}

pub fn compile_warp_program(renderer: &mut GlesRenderer) -> Option<WarpProgram> {
    compile_program(renderer, FRAGMENT_SRC, true, "warp mesh")
}

/// The program of the close effects (`[effects.close_animation]`): the warp
/// renderer's mesh stage under a fragment stage that burns, melts or beams
/// the picture away.
pub fn compile_burn_program(renderer: &mut GlesRenderer) -> Option<WarpProgram> {
    compile_program(renderer, BURN_FRAGMENT_SRC, false, "close effect")
}

fn compile_program(
    renderer: &mut GlesRenderer,
    fragment_src: &str,
    derivatives: bool,
    what: &str,
) -> Option<WarpProgram> {
    let compiled = renderer.with_context(|gl| unsafe {
        let vert = compile_stage(gl, ffi::VERTEX_SHADER, VERTEX_SRC)?;
        // Screen-space derivatives are an extension in GLES 2; nearly every
        // driver has it, and the plain variant covers the rest.
        let frag = derivatives
            .then(|| fragment_src.replacen('\n', "\n#define DERIVATIVES\n", 1))
            .and_then(|src| compile_stage(gl, ffi::FRAGMENT_SHADER, &src))
            .or_else(|| compile_stage(gl, ffi::FRAGMENT_SHADER, fragment_src));
        let Some(frag) = frag else {
            gl.DeleteShader(vert);
            return None;
        };
        let program = gl.CreateProgram();
        gl.AttachShader(program, vert);
        gl.AttachShader(program, frag);
        gl.LinkProgram(program);
        gl.DetachShader(program, vert);
        gl.DetachShader(program, frag);
        gl.DeleteShader(vert);
        gl.DeleteShader(frag);
        let mut status = ffi::FALSE as ffi::types::GLint;
        gl.GetProgramiv(program, ffi::LINK_STATUS, &mut status);
        if status == ffi::FALSE as ffi::types::GLint {
            gl.DeleteProgram(program);
            return None;
        }
        let attrib = |s: &CStr| gl.GetAttribLocation(program, s.as_ptr());
        let uniform = |s: &CStr| gl.GetUniformLocation(program, s.as_ptr());
        let (a_pos, a_uv) = (attrib(c"a_pos"), attrib(c"a_uv"));
        if a_pos < 0 || a_uv < 0 {
            gl.DeleteProgram(program);
            return None;
        }
        Some(ProgramInner {
            program,
            a_pos: a_pos as ffi::types::GLuint,
            a_uv: a_uv as ffi::types::GLuint,
            u_matrix: uniform(c"matrix"),
            u_local: uniform(c"local"),
            u_tex: uniform(c"tex"),
            u_alpha: uniform(c"alpha"),
            u_clip: uniform(c"clip"),
            u_used: uniform(c"used"),
            u_alloc: uniform(c"alloc"),
            u_effect: uniform(c"effect"),
            u_progress: uniform(c"progress"),
            u_seed: uniform(c"seed"),
            u_pad: uniform(c"pad"),
        })
    });
    match compiled {
        Ok(Some(inner)) => Some(WarpProgram(Rc::new(inner))),
        _ => {
            tracing::error!("Failed to compile the {what} program");
            None
        }
    }
}

/// A reusable offscreen the warped picture is rendered into each frame.
#[derive(Debug)]
pub struct WarpTarget {
    texture: GlesTexture,
    alloc: Size<i32, Physical>,
}

fn quantized(side: i32) -> i32 {
    (side + ALLOC_QUANTUM - 1) / ALLOC_QUANTUM * ALLOC_QUANTUM
}

/// A picture ready to be warped: the texture and the part of it in use.
#[derive(Debug, Clone)]
pub struct WarpSource {
    pub texture: GlesTexture,
    /// Texels the picture occupies, from the texture's top-left corner.
    pub used: Size<i32, Physical>,
    /// Texels the texture was allocated with.
    pub alloc: Size<i32, Physical>,
}

impl WarpSource {
    /// A texture used edge to edge.
    pub fn whole(texture: GlesTexture, texels: Size<i32, Physical>) -> Self {
        Self {
            texture,
            used: texels,
            alloc: texels,
        }
    }
}

/// Render `elements` (front to back, already placed in output physical
/// pixels) into `slot`'s texture, covering exactly `rect`. The texture is
/// kept between frames and only reallocated when `rect` outgrows it.
pub fn bake_elements<E: RenderElement<GlesRenderer>>(
    renderer: &mut GlesRenderer,
    slot: &mut Option<WarpTarget>,
    elements: &[E],
    rect: Rectangle<i32, Physical>,
    scale: Scale<f64>,
) -> Option<WarpSource> {
    let size = rect.size;
    if size.w <= 0 || size.h <= 0 || size.w > MAX_TEXTURE_SIDE || size.h > MAX_TEXTURE_SIDE {
        return None;
    }
    let fits = slot.as_ref().is_some_and(|t| {
        t.alloc.w >= size.w
            && t.alloc.h >= size.h
            && t.alloc.w <= quantized(size.w) + ALLOC_QUANTUM
            && t.alloc.h <= quantized(size.h) + ALLOC_QUANTUM
    });
    if !fits {
        let alloc: Size<i32, Physical> = Size::from((quantized(size.w), quantized(size.h)));
        let buffer_size = alloc.to_logical(1).to_buffer(1, Transform::Normal);
        let texture =
            Offscreen::<GlesTexture>::create_buffer(renderer, Fourcc::Abgr8888, buffer_size)
                .ok()?;
        *slot = Some(WarpTarget { texture, alloc });
    }
    let target_slot = slot.as_mut()?;
    let alloc = target_slot.alloc;
    {
        let mut target = renderer.bind(&mut target_slot.texture).ok()?;
        let mut frame = renderer
            .render(&mut target, alloc, Transform::Normal)
            .ok()?;
        let _ = frame.clear(Color32F::TRANSPARENT, &[Rectangle::from_size(alloc)]);
        let cache = UserDataMap::new();
        for element in elements.iter().rev() {
            let mut dst = element.geometry(scale);
            dst.loc -= rect.loc;
            let Some(mut local) = Rectangle::from_size(size).intersection(dst) else {
                continue;
            };
            local.loc -= dst.loc;
            let _ = element.draw(&mut frame, element.src(), dst, &[local], &[], Some(&cache));
        }
        let _sync = frame.finish().ok()?;
    }
    Some(WarpSource {
        texture: target_slot.texture.clone(),
        used: size,
        alloc,
    })
}

/// Vertex grid of a warp: interleaved `(x, y, u, v)` in element-local physical
/// pixels, plus the triangle list over it.
#[derive(Debug)]
pub struct WarpMesh {
    vertices: Vec<f32>,
    indices: Vec<u16>,
    bounds: Rectangle<i32, Physical>,
}

fn cells_for(extent: f64) -> usize {
    ((extent / CELL_PX).ceil() as usize).clamp(MIN_CELLS, MAX_CELLS)
}

impl WarpMesh {
    /// Build a `cols` × `rows` grid. `position(u, v)` gives the output-space
    /// physical position of the picture's point at fraction `(u, v)`; it is
    /// also asked for points `overhang` texels outside `[0, 1]`, the
    /// antialiasing band. That band must be half a frame pixel wide, so a
    /// picture squeezed to 1/n of its size needs n/2 texels of it.
    pub fn build(
        cols: usize,
        rows: usize,
        texels: Size<i32, Physical>,
        overhang: f64,
        position: impl Fn(f64, f64) -> [f64; 2],
    ) -> Self {
        let cols = cols.clamp(1, MAX_CELLS);
        let rows = rows.clamp(1, MAX_CELLS);
        let overhang = overhang.max(1.0);
        let margin_u = overhang / texels.w.max(1) as f64;
        let margin_v = overhang / texels.h.max(1) as f64;
        let mut vertices = Vec::with_capacity((cols + 1) * (rows + 1) * 4);
        let (mut x0, mut y0, mut x1, mut y1) = (f64::MAX, f64::MAX, f64::MIN, f64::MIN);
        for row in 0..=rows {
            let v = -margin_v + (1.0 + 2.0 * margin_v) * row as f64 / rows as f64;
            for col in 0..=cols {
                let u = -margin_u + (1.0 + 2.0 * margin_u) * col as f64 / cols as f64;
                let [x, y] = position(u, v);
                x0 = x0.min(x);
                y0 = y0.min(y);
                x1 = x1.max(x);
                y1 = y1.max(y);
                vertices.extend_from_slice(&[x as f32, y as f32, u as f32, v as f32]);
            }
        }
        let mut indices = Vec::with_capacity(cols * rows * 6);
        let stride = (cols + 1) as u16;
        for row in 0..rows as u16 {
            for col in 0..cols as u16 {
                let a = row * stride + col;
                let (b, c, d) = (a + 1, a + stride, a + stride + 1);
                indices.extend_from_slice(&[a, b, c, b, d, c]);
            }
        }
        let loc = Point::<i32, Physical>::from((x0.floor() as i32, y0.floor() as i32));
        let bounds = if x1 >= x0 && y1 >= y0 && x0.is_finite() && x1.is_finite() {
            Rectangle::new(
                loc,
                Size::from((
                    (x1.ceil() as i32 - loc.x).max(1),
                    (y1.ceil() as i32 - loc.y).max(1),
                )),
            )
        } else {
            Rectangle::default()
        };
        // Element-local, so the element can be drawn at any destination.
        for vertex in vertices.as_chunks_mut::<4>().0 {
            vertex[0] -= bounds.loc.x as f32;
            vertex[1] -= bounds.loc.y as f32;
        }
        Self {
            vertices,
            indices,
            bounds,
        }
    }

    /// A wobbling window. `baked` is the output rect the picture covers,
    /// `frame` the window's frame (content plus title bar) the spring grid
    /// spans, both in physical pixels; `px_per_unit` converts the grid's
    /// logical offsets.
    pub fn wobbly(
        offsets: &Offsets,
        baked: Rectangle<i32, Physical>,
        frame: Rectangle<f64, Physical>,
        px_per_unit: f64,
    ) -> Self {
        let (w, h) = (baked.size.w as f64, baked.size.h as f64);
        let fw = frame.size.w.max(1.0);
        let fh = frame.size.h.max(1.0);
        // A wobble stretches and shears; it never squeezes by much.
        Self::build(cells_for(w), cells_for(h), baked.size, 1.5, |u, v| {
            let x = baked.loc.x as f64 + u * w;
            let y = baked.loc.y as f64 + v * h;
            let o = patch_offset(offsets, (x - frame.loc.x) / fw, (y - frame.loc.y) / fh);
            [x + o[0] * px_per_unit, y + o[1] * px_per_unit]
        })
    }

    /// A picture part-way down the lamp.
    pub fn lamp(lamp: &Lamp, progress: f64, texels: Size<i32, Physical>) -> Self {
        let overhang = 1.0 + lamp.max_squeeze() / 2.0;
        Self::build(MAX_CELLS / 2, MAX_CELLS, texels, overhang, |u, v| {
            lamp.warp(u, v, progress)
        })
    }

    /// The picture undeformed over `rect` (`x, y, w, h` in output physical
    /// pixels), with `pad` texels of room around it for a close effect's
    /// flames and streaks.
    pub fn quad(rect: [f64; 4], texels: Size<i32, Physical>, pad: f64) -> Self {
        Self::build(1, 1, texels, pad, |u, v| {
            [rect[0] + u * rect[2], rect[1] + v * rect[3]]
        })
    }

    #[cfg(test)]
    pub fn bounds(&self) -> Rectangle<i32, Physical> {
        self.bounds
    }
}

/// Identity of a warp across frames, for the damage tracker: the id stays,
/// the commit moves whenever the shape or the picture did.
#[derive(Debug, Clone)]
pub struct WarpStamp {
    id: Id,
    commit: CommitCounter,
}

impl Default for WarpStamp {
    fn default() -> Self {
        Self {
            id: Id::new(),
            commit: CommitCounter::default(),
        }
    }
}

impl WarpStamp {
    pub fn bump(&mut self) {
        self.commit.increment();
    }
}

/// What a close effect's fragment stage is told on top of the warp's own
/// uniforms.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BurnUniforms {
    /// `CloseEffect::shader_id`.
    pub effect: f32,
    pub progress: f32,
    pub seed: [f32; 2],
    /// The mesh's overhang, in texels.
    pub pad: f32,
}

#[derive(Debug)]
pub struct WarpElement {
    stamp: WarpStamp,
    program: WarpProgram,
    source: WarpSource,
    mesh: WarpMesh,
    alpha: f32,
    burn: Option<BurnUniforms>,
}

impl WarpElement {
    pub fn new(
        stamp: &WarpStamp,
        program: &WarpProgram,
        source: WarpSource,
        mesh: WarpMesh,
        alpha: f32,
    ) -> Self {
        Self {
            stamp: stamp.clone(),
            program: program.clone(),
            source,
            mesh,
            alpha,
            burn: None,
        }
    }

    /// Draw with a close effect's uniforms; `program` must then be the one
    /// from [`compile_burn_program`].
    pub fn burning(mut self, burn: BurnUniforms) -> Self {
        self.burn = Some(burn);
        self
    }
}

impl Element for WarpElement {
    fn id(&self) -> &Id {
        &self.stamp.id
    }

    fn current_commit(&self) -> CommitCounter {
        self.stamp.commit
    }

    fn src(&self) -> Rectangle<f64, Buffer> {
        Rectangle::from_size(Size::from((
            self.source.used.w as f64,
            self.source.used.h as f64,
        )))
    }

    fn geometry(&self, _scale: Scale<f64>) -> Rectangle<i32, Physical> {
        self.mesh.bounds
    }

    fn alpha(&self) -> f32 {
        self.alpha
    }

    fn kind(&self) -> Kind {
        Kind::Unspecified
    }
}

impl RenderElement<GlesRenderer> for WarpElement {
    fn draw(
        &self,
        frame: &mut GlesFrame<'_, '_>,
        _src: Rectangle<f64, Buffer>,
        dst: Rectangle<i32, Physical>,
        damage: &[Rectangle<i32, Physical>],
        _opaque_regions: &[Rectangle<i32, Physical>],
        _user_data: Option<&UserDataMap>,
    ) -> Result<(), GlesError> {
        let bounds = self.mesh.bounds;
        if damage.is_empty() || self.mesh.indices.is_empty() || bounds.size.is_empty() {
            return Ok(());
        }
        let projection = *frame.projection();
        let instancing = frame.capabilities().contains(&Capability::Instancing);
        let sx = dst.size.w as f32 / bounds.size.w as f32;
        let sy = dst.size.h as f32 / bounds.size.h as f32;
        let local = [
            sx,
            0.0,
            0.0,
            0.0,
            sy,
            0.0,
            dst.loc.x as f32,
            dst.loc.y as f32,
            1.0,
        ];
        let program = &*self.program.0;
        let source = &self.source;
        let mesh = &self.mesh;
        let alpha = self.alpha;
        let burn = self.burn;
        // Smithay re-establishes program, texture unit, attribute arrays and
        // divisors on each of its own draws, and never binds an element array
        // buffer or VAO, so nothing here needs restoring beyond what is
        // switched back off below. Blending is already premultiplied-over.
        frame.with_context(|gl| unsafe {
            let stride = (4 * std::mem::size_of::<f32>()) as ffi::types::GLsizei;
            let base = mesh.vertices.as_ptr();
            gl.UseProgram(program.program);
            gl.ActiveTexture(ffi::TEXTURE0);
            gl.BindTexture(ffi::TEXTURE_2D, source.texture.tex_id());
            for (name, value) in [
                (ffi::TEXTURE_MIN_FILTER, ffi::LINEAR),
                (ffi::TEXTURE_MAG_FILTER, ffi::LINEAR),
                (ffi::TEXTURE_WRAP_S, ffi::CLAMP_TO_EDGE),
                (ffi::TEXTURE_WRAP_T, ffi::CLAMP_TO_EDGE),
            ] {
                gl.TexParameteri(ffi::TEXTURE_2D, name, value as ffi::types::GLint);
            }
            gl.Uniform1i(program.u_tex, 0);
            gl.UniformMatrix3fv(program.u_matrix, 1, ffi::FALSE, projection.as_ptr());
            gl.UniformMatrix3fv(program.u_local, 1, ffi::FALSE, local.as_ptr());
            gl.Uniform1f(program.u_alpha, alpha);
            if let Some(burn) = burn {
                gl.Uniform1f(program.u_effect, burn.effect);
                gl.Uniform1f(program.u_progress, burn.progress);
                gl.Uniform2f(program.u_seed, burn.seed[0], burn.seed[1]);
                gl.Uniform1f(program.u_pad, burn.pad);
            }
            gl.Uniform2f(program.u_used, source.used.w as f32, source.used.h as f32);
            gl.Uniform2f(
                program.u_alloc,
                source.alloc.w as f32,
                source.alloc.h as f32,
            );

            gl.BindBuffer(ffi::ARRAY_BUFFER, 0);
            gl.BindBuffer(ffi::ELEMENT_ARRAY_BUFFER, 0);
            gl.EnableVertexAttribArray(program.a_pos);
            gl.EnableVertexAttribArray(program.a_uv);
            gl.VertexAttribPointer(
                program.a_pos,
                2,
                ffi::FLOAT,
                ffi::FALSE,
                stride,
                base as *const _,
            );
            gl.VertexAttribPointer(
                program.a_uv,
                2,
                ffi::FLOAT,
                ffi::FALSE,
                stride,
                base.add(2) as *const _,
            );
            if instancing {
                // Smithay leaves a divisor of 1 on whichever location its
                // instanced rect attribute had.
                gl.VertexAttribDivisor(program.a_pos, 0);
                gl.VertexAttribDivisor(program.a_uv, 0);
            }
            for rect in damage {
                let x0 = (dst.loc.x + rect.loc.x) as f32;
                let y0 = (dst.loc.y + rect.loc.y) as f32;
                gl.Uniform4f(
                    program.u_clip,
                    x0,
                    y0,
                    x0 + rect.size.w as f32,
                    y0 + rect.size.h as f32,
                );
                gl.DrawElements(
                    ffi::TRIANGLES,
                    mesh.indices.len() as ffi::types::GLsizei,
                    ffi::UNSIGNED_SHORT,
                    mesh.indices.as_ptr() as *const _,
                );
            }
            gl.DisableVertexAttribArray(program.a_pos);
            gl.DisableVertexAttribArray(program.a_uv);
            gl.BindTexture(ffi::TEXTURE_2D, 0);
        })
    }

    fn underlying_storage(&self, _renderer: &mut GlesRenderer) -> Option<UnderlyingStorage<'_>> {
        None
    }
}

/// Replace what a wobbling window pushed onto `bucket` from `start` on with
/// one element: that picture, bent through the window's spring grid. `frame`
/// is the window's frame (content plus title bar) in the output's logical
/// pixels before zoom. Returns whether it did; a window at rest, and one whose
/// picture could not be flattened, is left exactly as pushed.
#[allow(clippy::too_many_arguments)]
pub(crate) fn push_wobbling_window(
    wobbly: &mut crate::state::Wobbly,
    program: Option<&WarpProgram>,
    renderer: &mut GlesRenderer,
    output: &smithay::output::Output,
    bucket: &mut Vec<super::OutputRenderElements>,
    start: usize,
    surface: &smithay::reexports::wayland_server::protocol::wl_surface::WlSurface,
    frame: Rectangle<f64, smithay::utils::Logical>,
    zoom: f64,
    scale: Scale<f64>,
) -> bool {
    let (Some(program), Some(entry)) = (program, wobbly.get_mut(surface)) else {
        return false;
    };
    let px_per_unit = zoom * scale.x;
    let Some(painted) = bucket[start..]
        .iter()
        .map(|e| e.geometry(scale))
        .reduce(|a, b| a.merge(b))
    else {
        return false;
    };
    // Nothing further off the output than the jelly can stretch back onto it
    // is worth flattening: a window parked half off-screen keeps a texture
    // the size of what can show.
    let reach = (entry.reach() * px_per_unit).ceil() as i32 + 2;
    let mode = output
        .current_mode()
        .map(|m| output.current_transform().transform_size(m.size))
        .unwrap_or_default();
    let visible: Rectangle<i32, Physical> = Rectangle::new(
        Point::from((-reach, -reach)),
        Size::from((mode.w + 2 * reach, mode.h + 2 * reach)),
    );
    let Some(rect) = painted.intersection(visible) else {
        return false;
    };
    let slot = entry.targets.entry(output.name()).or_default();
    let Some(source) = bake_elements(renderer, slot, &bucket[start..], rect, scale) else {
        return false;
    };
    let frame_phys: Rectangle<f64, Physical> = Rectangle::new(
        Point::from((frame.loc.x * px_per_unit, frame.loc.y * px_per_unit)),
        Size::from((frame.size.w * px_per_unit, frame.size.h * px_per_unit)),
    );
    let mesh = WarpMesh::wobbly(&entry.offsets(), rect, frame_phys, px_per_unit);
    entry.stamp.bump();
    let element = WarpElement::new(&entry.stamp, program, source, mesh, 1.0);
    bucket.truncate(start);
    bucket.push(super::OutputRenderElements::Warp(element));
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_undisturbed_mesh_maps_every_texel_onto_itself() {
        let baked = Rectangle::new(Point::from((40, 60)), Size::from((200, 100)));
        let frame = Rectangle::new(Point::from((50.0, 70.0)), Size::from((180.0, 80.0)));
        let mesh = WarpMesh::wobbly(&[[0.0; 2]; 16], baked, frame, 1.5);
        // The antialiasing band's texel and a half on every side, rounded out.
        assert_eq!(mesh.bounds().loc, Point::from((38, 58)));
        assert_eq!(mesh.bounds().size, Size::from((204, 104)));
        for v in mesh.vertices.as_chunks::<4>().0 {
            let x = v[0] + mesh.bounds().loc.x as f32 - 40.0;
            let y = v[1] + mesh.bounds().loc.y as f32 - 60.0;
            assert!((x - v[2] * 200.0).abs() < 1e-3 && (y - v[3] * 100.0).abs() < 1e-3);
        }
    }

    #[test]
    fn the_bounds_follow_the_deformation() {
        let baked = Rectangle::new(Point::from((0, 0)), Size::from((200, 100)));
        let frame = Rectangle::new(Point::from((0.0, 0.0)), Size::from((200.0, 100.0)));
        let mut offsets = [[0.0; 2]; 16];
        offsets[15] = [30.0, 20.0];
        let mesh = WarpMesh::wobbly(&offsets, baked, frame, 2.0);
        let b = mesh.bounds();
        assert!(
            b.loc.x + b.size.w >= 258 && b.loc.y + b.size.h >= 138,
            "{b:?}"
        );
    }

    #[test]
    fn indices_stay_inside_the_vertex_grid() {
        let mesh = WarpMesh::build(MAX_CELLS + 10, 3, Size::from((10, 10)), 1.0, |u, v| [u, v]);
        let count = (mesh.vertices.len() / 4) as u16;
        assert!(mesh.indices.iter().all(|i| *i < count));
        assert_eq!(mesh.indices.len(), MAX_CELLS * 3 * 6);
    }

    #[test]
    fn a_quad_overhangs_its_picture_by_the_pad() {
        // A 100x50 texel picture drawn twice as large: 10 texels of pad are
        // 20 pixels of room.
        let mesh = WarpMesh::quad([40.0, 60.0, 200.0, 100.0], Size::from((100, 50)), 10.0);
        assert_eq!(mesh.bounds().loc, Point::from((20, 40)));
        assert_eq!(mesh.bounds().size, Size::from((240, 140)));
        assert_eq!(mesh.indices.len(), 6);
    }

    #[test]
    fn allocations_are_quantized() {
        assert_eq!(quantized(1), 64);
        assert_eq!(quantized(64), 64);
        assert_eq!(quantized(65), 128);
    }
}
