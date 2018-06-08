#include "renderer.hpp"

namespace render {

Renderer::Renderer(int width, int height) : _width(width), _height(height) {
  for (int i = 0; i < 10; i++) {
    ubo.lights[i].enabled = 1;
    ubo.lights[i].intensity = 1.0f;
    ubo.lights[i].range = 3.0f;
    ubo.lights[i].color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    ubo.lights[i].direction_ws =
        glm::normalize(glm::vec4(0.0f, -1.0f, 0.2f, 0.0f));
    ubo.lights[i].position_ws = glm::vec4(0.0f, 0.0f + i * 10.0f, 0.0f, 1.0f);
  }
  ubo.lights[0].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  ubo.lights[0].type = 1;
  glGenBuffers(1, &ubo_id);
  glBindBuffer(GL_UNIFORM_BUFFER, ubo_id);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(UBO), &ubo, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // Gen and setup our depth map FBO for the depth prepass
  glGenFramebuffers(1, &depthmap_fbo);

  glGenTextures(1, &depthmap_id);
  glBindTexture(GL_TEXTURE_2D, depthmap_id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, _width, _height, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  glBindFramebuffer(GL_FRAMEBUFFER, depthmap_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depthmap_id, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Renderer::Renderer(Renderer const &src) { *this = src; }

Renderer::~Renderer(void) {}

Renderer &Renderer::operator=(Renderer const &rhs) {
  if (this != &rhs) {
    this->_attribs = rhs._attribs;
  }
  return (*this);
}

void Renderer::renderText(float pos_x, float pos_y, float scale,
                          std::string text, glm::vec3 color) {
  RenderState backup_state = _state;

  switchPolygonMode(PolygonMode::Fill);
  switchDepthTestState(true);
  switchBlendingFunc(BlendFunc::OneMinusSrcAlpha);
  switchBlendingState(true);

  TextProperties prop = {};
  prop.color = color;
  _textRenderer.renderText(_shaderCache.getShader("text"), pos_x, pos_y, scale,
                           text, prop);

  setState(backup_state);
}

void Renderer::renderUI(std::string filename, float pos_x, float pos_y,
                        float scale, bool centered) {
  RenderState backup_state = _state;

  switchPolygonMode(PolygonMode::Fill);
  switchDepthTestState(false);
  switchBlendingFunc(BlendFunc::OneMinusSrcAlpha);
  switchBlendingState(true);

  glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(this->_width),
                                    0.0f, static_cast<float>(this->_height));
  _uiRenderer.renderUI(_shaderCache.getShader("ui"), filename, pos_x, pos_y,
                       scale, projection, centered);

  setState(backup_state);
}
void Renderer::bindTexture(Texture *texture, GLenum tex_slot) {
  if (texture != nullptr && texture->id != 0) {
    glActiveTexture(tex_slot);
    glBindTexture(GL_TEXTURE_2D, texture->id);
  } else {
    glActiveTexture(tex_slot);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

void Renderer::addAttrib(const Attrib &attrib) {
  this->_attribs.push_back(attrib);
}

void Renderer::switchShader(GLuint shader_id, int &current_shader_id) {
  if (shader_id > 0 && shader_id != current_shader_id) {
    glUseProgram(shader_id);
    setUniform(glGetUniformLocation(shader_id, "P"), this->uniforms.proj);
    setUniform(glGetUniformLocation(shader_id, "V"), this->uniforms.view);
    current_shader_id = shader_id;
  }
}

void Renderer::updateUniforms(const Attrib &attrib, const int shader_id) {
  if (shader_id > 0 && attrib.vaos.size() > 0) {
    glm::mat4 mvp = uniforms.view_proj * attrib.model;
    setUniform(glGetUniformLocation(shader_id, "MVP"), mvp);
    setUniform(glGetUniformLocation(shader_id, "MV"),
               uniforms.view * attrib.model);
    setUniform(glGetUniformLocation(shader_id, "M"), attrib.model);
    setUniform(glGetUniformLocation(shader_id, "diffuse_tex"), 0);
    setUniform(glGetUniformLocation(shader_id, "specular_tex"), 1);
  }
}

void Renderer::draw() {
  RenderState backup_state = _state;
  int current_shader_id = -1;

  Shader *depthprepass = _shaderCache.getShader("depthprepass");
  Shader *lightculling = _shaderCache.getShader("lightculling");
  Shader *default = _shaderCache.getShader("default");

  glViewport(0, 0, _width, _height);

  // Depth prepass :
  // Bind the framebuffer and render scene geometry
  glBindFramebuffer(GL_FRAMEBUFFER, depthmap_fbo);
  glClear(GL_DEPTH_BUFFER_BIT);

  switchDepthTestState(true);
  switchShader(depthprepass->id, current_shader_id);
  for (const auto &attrib : this->_attribs) {
    setState(attrib.state);
    updateUniforms(attrib, depthprepass->id);
    drawVAOs(attrib.vaos, attrib.state.primitiveMode);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  for (const auto &attrib : this->_attribs) {
    setState(attrib.state);

    glm::mat4 mv = uniforms.view * attrib.model;
    for (int i = 0; i < 10; i++) {
      ubo.lights[i].direction_vs =
          glm::normalize(mv * ubo.lights[i].direction_ws);
      ubo.lights[i].position_vs = mv * ubo.lights[i].position_ws;
    }
    ubo.material = attrib.material;

    glBindBuffer(GL_UNIFORM_BUFFER, ubo_id);
    GLvoid *p = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
    memcpy(p, &ubo, sizeof(UBO));
    glUnmapBuffer(GL_UNIFORM_BUFFER);

    switchShader(default->id, current_shader_id);

    unsigned int block_index = glGetUniformBlockIndex(default->id, "data");
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_id);
    glUniformBlockBinding(default->id, block_index, 0);

    updateUniforms(attrib, default->id);

    bindTexture(attrib.diffuse, GL_TEXTURE0);
    bindTexture(attrib.specular, GL_TEXTURE0 + 1);

    drawVAOs(attrib.vaos, attrib.state.primitiveMode);
  }

  setState(_state);
  glBindVertexArray(0);
}

void Renderer::drawVAOs(const std::vector<VAO *> &vaos,
                        render::PrimitiveMode primitive_mode) {
  GLenum mode = getGLRenderMode(primitive_mode);
  for (const auto &vao : vaos) {
    if (vao != nullptr) {
      if (vao->indices_size != 0) {
        glBindVertexArray(vao->vao);
        glDrawElements(mode, vao->indices_size, GL_UNSIGNED_INT, 0);
      } else if (vao->vertices_size != 0) {
        glBindVertexArray(vao->vao);
        glDrawArrays(mode, 0, vao->vertices_size);
      }
    }
  }
}

void Renderer::update(const Env &env) {
  if (env.width != _width || env.height != _height) {
    _width = env.width;
    _height = env.height;
  }
  _textRenderer.update(env);
  _shaderCache.update();
}

void Renderer::flushAttribs() { this->_attribs.clear(); }

int Renderer::getScreenWidth() { return (this->_width); }

int Renderer::getScreenHeight() { return (this->_height); }

void Renderer::clearScreen() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

GLenum Renderer::getGLRenderMode(enum PrimitiveMode mode) {
  GLenum gl_primitive_modes[12] = {GL_POINTS,
                                   GL_LINE_STRIP,
                                   GL_LINE_LOOP,
                                   GL_LINES,
                                   GL_LINE_STRIP_ADJACENCY,
                                   GL_LINES_ADJACENCY,
                                   GL_TRIANGLE_STRIP,
                                   GL_TRIANGLE_FAN,
                                   GL_TRIANGLES,
                                   GL_TRIANGLE_STRIP_ADJACENCY,
                                   GL_TRIANGLES_ADJACENCY,
                                   GL_PATCHES};
  unsigned index_mode = static_cast<unsigned int>(mode);
  return (gl_primitive_modes[index_mode]);
}

void Renderer::setState(const RenderState &new_state) {
  switchPolygonMode(new_state.polygonMode);
  switchDepthTestFunc(new_state.depthTestFunc);
  switchDepthTestState(new_state.depthTest);
  switchBlendingState(new_state.blending);
  switchBlendingFunc(new_state.blendFunc);
}

void Renderer::switchPolygonMode(enum PolygonMode mode) {
  GLenum gl_polygon_modes[3] = {GL_POINT, GL_LINE, GL_FILL};
  if (mode != _state.polygonMode) {
    unsigned int index_mode = static_cast<unsigned int>(mode);
    glPolygonMode(GL_FRONT_AND_BACK, gl_polygon_modes[index_mode]);
    _state.polygonMode = mode;
  }
}

void Renderer::switchDepthTestFunc(enum DepthTestFunc mode) {
  GLenum gl_depth_funcs[8] = {GL_LESS,    GL_NEVER,    GL_EQUAL,  GL_LEQUAL,
                              GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
  if (mode != _state.depthTestFunc) {
    unsigned int index_func = static_cast<unsigned int>(mode);
    glDepthFunc(gl_depth_funcs[index_func]);
    _state.depthTestFunc = mode;
  }
}

void Renderer::switchBlendingFunc(enum BlendFunc mode) {
  GLenum gl_blend_funcs[14] = {GL_ZERO,           GL_ONE,
                               GL_SRC_COLOR,      GL_ONE_MINUS_SRC_COLOR,
                               GL_DST_COLOR,      GL_ONE_MINUS_DST_COLOR,
                               GL_SRC_ALPHA,      GL_ONE_MINUS_SRC_ALPHA,
                               GL_DST_ALPHA,      GL_ONE_MINUS_DST_ALPHA,
                               GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR,
                               GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA};
  if (mode != _state.blendFunc) {
    unsigned int index_func = static_cast<unsigned int>(mode);
    glBlendFunc(GL_SRC_ALPHA, gl_blend_funcs[index_func]);
    _state.blendFunc = mode;
  }
}

void Renderer::switchDepthTestState(bool depth_test) {
  if (depth_test != _state.depthTest) {
    if (depth_test) {
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }
    _state.depthTest = depth_test;
  }
}

void Renderer::switchBlendingState(bool blending) {
  if (blending != _state.blending) {
    if (blending) {
      glEnable(GL_BLEND);
    } else {
      glDisable(GL_BLEND);
    }
    _state.blending = blending;
  }
}

bool Attrib::operator<(const struct Attrib &rhs) const {
  if (this->vaos[0] != nullptr && rhs.vaos[0] != nullptr) {
    return (this->state.depthTest == false);
  } else {
    return (true);
  }
}

}  // namespace render
