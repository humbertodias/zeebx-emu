//! A placa da Vita. O vitaGL abre o contexto; o preenchimento do jogo é o shader do núcleo.
//!
//! O vitaGL traduz GLSL ES 1.00 e se apresenta como OpenGL ES 2.0. Se a inicialização falha,
//! o chamador segue no framebuffer do kernel. Se ela funciona, o vitaGL fica dono da tela até
//! o processo acabar — não há `vglEnd` — e o menu sobe como textura em vez de
//! `sceDisplaySetFrameBuf`.

use std::ffi::c_void;
use std::sync::{Arc, Mutex};

use glow::HasContext;

use super::anota;

const LARGURA: f32 = 960.0;
const ALTURA: f32 = 544.0;

struct Estado {
    gl: Arc<glow::Context>,
    programa: glow::Program,
    vbo: glow::Buffer,
    textura_cpu: glow::Texture,
}

static ESTADO: Mutex<Option<Estado>> = Mutex::new(None);

unsafe extern "C" {
    fn vglGetProcAddress(name: *const i8) -> *const c_void;
    fn vglInitExtended(
        legacy_pool_size: i32,
        width: i32,
        height: i32,
        ram_threshold: i32,
        msaa: i32,
    ) -> i32;
    fn vglSwapBuffers(has_common_dialog: u8);
    fn vglWaitVblankStart(enable: u8);
    fn vglSetShaderCachePath(path: *const i8);
}

const VERTICE: &str = r#"
attribute vec2 pos;
attribute vec2 uv;
varying vec2 vuv;
void main() {
    vuv = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}
"#;

const FRAGMENTO: &str = r#"
precision mediump float;
varying vec2 vuv;
uniform sampler2D imagem;
void main() {
    gl_FragColor = texture2D(imagem, vuv);
}
"#;

/// O que deu para fazer com a placa.
pub enum Abertura {
    /// Contexto e shader da tela prontos. O vitaGL é o dono da tela.
    Pronta,
    /// O vitaGL recusou o init. O framebuffer do kernel ainda serve.
    SemPlaca,
    /// O init passou e o vitaGL ficou com a tela, mas o shader de apresentação não ligou.
    /// Voltar ao framebuffer do kernel brigaria com ele.
    TomouATela(String),
}

/// Sobe o vitaGL.
pub fn abre() -> Abertura {
    let _ = std::fs::create_dir_all("ux0:data/zeebx/shader_cache");
    // O cache tem de existir antes do init: a primeira compilação passa pelo SceShaccCg.
    unsafe {
        vglSetShaderCachePath(c"ux0:data/zeebx/shader_cache".as_ptr());
        // 1 MiB de pool legado, 16 MiB de limiar de RAM, sem multisample. A tela é a nativa.
        let codigo = vglInitExtended(0x10_0000, 960, 544, 0x100_0000, 0);
        // O cabeçalho diz `GLboolean`, mas o binário devolve 0 quando 960×544 cabe na
        // tela e 1 quando precisa ajustar. Os dois já subiram o GXM. Ler o 0 como
        // falha pedia de novo a PHYCONT e a CDRAM, que o init tinha acabado de tomar:
        // `0x80024302` e `0x80024309`, sem página física livre.
        anota(&format!("vglInit {codigo}"));
        if codigo < 0 {
            anota("vitaGL não subiu; o quadro fica no processador");
            return Abertura::SemPlaca;
        }
        super::anota_livre();
        vglWaitVblankStart(1);
        // O init deixa uma thread desenhando o logo do vitaGL. Ela só larga a tela na
        // primeira troca. Compilar o shader antes disso deixa o logo na frente o tempo
        // todo, e as duas threads disputam o mesmo contexto.
        anota("vitaGL no ar; tirando o logo");
        vglSwapBuffers(0);
    }
    let gl = Arc::new(unsafe {
        glow::Context::from_loader_function(|nome| {
            let Ok(c) = std::ffi::CString::new(nome) else {
                return std::ptr::null();
            };
            vglGetProcAddress(c.as_ptr())
        })
    });
    unsafe {
        anota(&format!(
            "GL {} / {} / {} / {}",
            gl.get_parameter_string(glow::VENDOR),
            gl.get_parameter_string(glow::RENDERER),
            gl.get_parameter_string(glow::VERSION),
            gl.get_parameter_string(glow::SHADING_LANGUAGE_VERSION),
        ));
    }
    anota("compilando o shader da tela");
    let programa = match unsafe { programa_de_apresentacao(&gl) } {
        Ok(programa) => programa,
        Err(erro) => {
            // O vitaGL já tomou a tela. Sem este shader não há como mostrar o menu nem o jogo,
            // e o framebuffer do kernel não recupera a tela.
            return Abertura::TomouATela(format!("o shader da tela não ligou ({erro})"));
        }
    };
    let (vbo, textura_cpu) = unsafe {
        let vbo = match gl.create_buffer() {
            Ok(vbo) => vbo,
            Err(erro) => {
                return Abertura::TomouATela(format!("sem buffer da tela ({erro})"));
            }
        };
        let textura = match gl.create_texture() {
            Ok(textura) => textura,
            Err(erro) => {
                return Abertura::TomouATela(format!("sem textura da tela ({erro})"));
            }
        };
        gl.bind_texture(glow::TEXTURE_2D, Some(textura));
        // 960×544 não é potência de dois. No ES 2 essa textura só amostra com clamp e sem mip.
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_MIN_FILTER,
            glow::NEAREST as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_MAG_FILTER,
            glow::NEAREST as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_WRAP_S,
            glow::CLAMP_TO_EDGE as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_WRAP_T,
            glow::CLAMP_TO_EDGE as i32,
        );
        gl.bind_texture(glow::TEXTURE_2D, None);
        (vbo, textura)
    };
    let mut guarda = ESTADO.lock().unwrap_or_else(|e| e.into_inner());
    *guarda = Some(Estado {
        gl,
        programa,
        vbo,
        textura_cpu,
    });
    Abertura::Pronta
}

pub fn ativa() -> bool {
    ESTADO.lock().unwrap_or_else(|e| e.into_inner()).is_some()
}

pub fn contexto() -> Option<Arc<glow::Context>> {
    ESTADO
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .as_ref()
        .map(|estado| Arc::clone(&estado.gl))
}

/// O menu e o quadro de software. A palavra é `0xAABBGGRR`, que na memória sai R, G, B, A.
pub fn mostra_rgba(pixels: &[u32], w: i32, h: i32) {
    let guarda = ESTADO.lock().unwrap_or_else(|e| e.into_inner());
    let Some(estado) = guarda.as_ref() else {
        return;
    };
    unsafe {
        let gl = &estado.gl;
        gl.bind_texture(glow::TEXTURE_2D, Some(estado.textura_cpu));
        gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 4);
        let bytes = std::slice::from_raw_parts(pixels.as_ptr().cast::<u8>(), pixels.len() * 4);
        gl.tex_image_2d(
            glow::TEXTURE_2D,
            0,
            glow::RGBA as i32,
            w,
            h,
            0,
            glow::RGBA,
            glow::UNSIGNED_BYTE,
            glow::PixelUnpackData::Slice(Some(bytes)),
        );
        let id = estado.textura_cpu.0.get();
        pinta(estado, id, 0, 0, w, h);
        vglSwapBuffers(0);
    }
}

/// O quadro do jogo, já na placa. `x` e `y` são o canto superior esquerdo, em pixels da tela.
pub fn mostra_textura(id: u32, x: i32, y: i32, w: i32, h: i32) {
    let guarda = ESTADO.lock().unwrap_or_else(|e| e.into_inner());
    let Some(estado) = guarda.as_ref() else {
        return;
    };
    unsafe {
        pinta(estado, id, x, y, w, h);
        vglSwapBuffers(0);
    }
}

unsafe fn programa_de_apresentacao(gl: &glow::Context) -> Result<glow::Program, String> {
    let mut ultimo = String::from("shader da tela");
    for cabecalho in ["#version 100\n", "\n"] {
        match unsafe { liga(gl, cabecalho) } {
            Ok(programa) => return Ok(programa),
            Err(erro) => ultimo = erro,
        }
    }
    Err(ultimo)
}

unsafe fn liga(gl: &glow::Context, cabecalho: &str) -> Result<glow::Program, String> {
    unsafe {
        let programa = gl.create_program()?;
        let mut shaders = Vec::new();
        for (tipo, fonte) in [
            (glow::VERTEX_SHADER, VERTICE),
            (glow::FRAGMENT_SHADER, FRAGMENTO),
        ] {
            let shader = gl.create_shader(tipo)?;
            gl.shader_source(shader, &format!("{cabecalho}{fonte}"));
            gl.compile_shader(shader);
            if !gl.get_shader_compile_status(shader) {
                let erro = gl.get_shader_info_log(shader);
                gl.delete_shader(shader);
                for shader in shaders {
                    gl.delete_shader(shader);
                }
                gl.delete_program(programa);
                return Err(erro);
            }
            gl.attach_shader(programa, shader);
            shaders.push(shader);
        }
        gl.bind_attrib_location(programa, 0, "pos");
        gl.bind_attrib_location(programa, 1, "uv");
        gl.link_program(programa);
        // O `vglGetProcAddress` do vitaGL não tem `glDetachShader`. O glow aborta
        // quando o ponteiro vem nulo ("called glDetachShader but it was not loaded",
        // no coredump). Apagar o shader ainda ligado é o que a especificação prevê:
        // ele é liberado quando o programa morre.
        for shader in shaders {
            gl.delete_shader(shader);
        }
        if !gl.get_program_link_status(programa) {
            let erro = gl.get_program_info_log(programa);
            gl.delete_program(programa);
            return Err(erro);
        }
        Ok(programa)
    }
}

/// Desenha a textura. O `v` zero é o topo: o preenchimento do jogo inverte o Y, e a primeira
/// linha do menu também é o topo visual.
unsafe fn pinta(estado: &Estado, textura: u32, x: i32, y: i32, w: i32, h: i32) {
    let (x0, y1) = ndc(x as f32, y as f32);
    let (x1, y0) = ndc((x + w) as f32, (y + h) as f32);
    // Dois triângulos. O topo da tela (y de NDC maior) amostra v = 0.
    let vertices: [f32; 24] = [
        x0, y1, 0.0, 0.0, x1, y1, 1.0, 0.0, x0, y0, 0.0, 1.0, x1, y1, 1.0, 0.0, x1, y0, 1.0, 1.0,
        x0, y0, 0.0, 1.0,
    ];
    let gl = &estado.gl;
    unsafe {
        gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        gl.viewport(0, 0, LARGURA as i32, ALTURA as i32);
        gl.disable(glow::SCISSOR_TEST);
        gl.disable(glow::DEPTH_TEST);
        gl.disable(glow::BLEND);
        gl.disable(glow::CULL_FACE);
        gl.disable(glow::STENCIL_TEST);
        gl.color_mask(true, true, true, true);
        // O vitaGL tem dois buffers. O quadrado do jogo só cobre 640×480 no centro;
        // o resto ficava com a lista de jogos, e ela aparecia atrás da cena. A limpeza
        // é deste buffer, a cada troca — limpar uma vez só apaga o outro na alternância.
        gl.clear_color(0.0, 0.0, 0.0, 1.0);
        gl.clear(glow::COLOR_BUFFER_BIT);
        gl.bind_vertex_array(None);
        gl.use_program(Some(estado.programa));
        let imagem = gl.get_uniform_location(estado.programa, "imagem");
        gl.uniform_1_i32(imagem.as_ref(), 0);
        gl.active_texture(glow::TEXTURE0);
        let nativa = glow::NativeTexture(std::num::NonZeroU32::new(textura).unwrap());
        gl.bind_texture(glow::TEXTURE_2D, Some(nativa));
        // A cor do jogo é 640×480, também fora de potência de dois.
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_MIN_FILTER,
            glow::NEAREST as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_MAG_FILTER,
            glow::NEAREST as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_WRAP_S,
            glow::CLAMP_TO_EDGE as i32,
        );
        gl.tex_parameter_i32(
            glow::TEXTURE_2D,
            glow::TEXTURE_WRAP_T,
            glow::CLAMP_TO_EDGE as i32,
        );
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(estado.vbo));
        gl.buffer_data_u8_slice(
            glow::ARRAY_BUFFER,
            bytemuck_f32(&vertices),
            glow::STREAM_DRAW,
        );
        let passo = 4 * 4;
        gl.enable_vertex_attrib_array(0);
        gl.vertex_attrib_pointer_f32(0, 2, glow::FLOAT, false, passo, 0);
        gl.enable_vertex_attrib_array(1);
        gl.vertex_attrib_pointer_f32(1, 2, glow::FLOAT, false, passo, 8);
        for indice in 2..5 {
            gl.disable_vertex_attrib_array(indice);
        }
        gl.draw_arrays(glow::TRIANGLES, 0, 6);
        gl.bind_texture(glow::TEXTURE_2D, None);
        gl.bind_buffer(glow::ARRAY_BUFFER, None);
        gl.use_program(None);
    }
}

fn ndc(px: f32, py: f32) -> (f32, f32) {
    (px / LARGURA * 2.0 - 1.0, 1.0 - py / ALTURA * 2.0)
}

fn bytemuck_f32(vertices: &[f32; 24]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(vertices.as_ptr().cast::<u8>(), vertices.len() * 4) }
}
