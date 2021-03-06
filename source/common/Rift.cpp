#include "Common.h"
#include <json/json.h>
#include <fstream>

const float Rift::MONO_FOV = 65.0;
const float Rift::FRAMEBUFFER_OBJECT_SCALE = 2;
const float DISPLACEMENT_MAP_SCALE = 0.02f;
const float Rift::ZFAR = 10000.0f;
const float Rift::ZNEAR = 0.01f;

glm::quat parseQuaternion(const Json::Value & node) {
  glm::quat out;
  out.x = node.get("x", 0).asFloat();
  out.y = node.get("y", 0).asFloat();
  out.z = node.get("z", 0).asFloat();
  out.w = node.get("w", 1).asFloat();
  return out;
}

std::string getConfigFile() {
  static OVR::String ovrBasePath = OVR::GetBaseOVRPath(false);
  static OVR::String ovrConfigFile = ovrBasePath + OVR::String("/profile2.json");
  static std::string configFile = (const char*)ovrConfigFile;
  return configFile;
}

Json::Value readConfig() {
  std::string file = getConfigFile();
  Json::Value result;
  if (Files::exists(file)) {
    Json::Reader reader;
    if (!reader.parse(Files::read(file), result)) {
      SAY_ERR("Failed to parse config %s", reader.getFormattedErrorMessages().c_str());
    }
  }
  return result;
}

void writeConfig(const Json::Value & value) {
  std::string file = getConfigFile();
  Json::StyledWriter writer;
  std::string json = writer.write(value);
  std::ofstream out(file);
  if (out) {
    out << json << std::endl;
    out.close();
  }
}

glm::quat Rift::getStrabismusCorrection() {
  Json::Value config = readConfig();
  Json::Value sc = config.get("StrabismusCorrection", Json::Value::null);
  if (sc.isNull()) {
    return glm::quat();
  }
  return parseQuaternion(sc);
}

void Rift::setStrabismusCorrection(const glm::quat & q) {
  Json::Value config = readConfig();
  config["StrabismusCorrection"]["x"] = q.x;
  config["StrabismusCorrection"]["y"] = q.y;
  config["StrabismusCorrection"]["z"] = q.z;
  config["StrabismusCorrection"]["w"] = q.w;
  writeConfig(config);
}


void Rift::getDefaultDk1HmdValues(OVR::HMDInfo & hmdInfo) {
  hmdInfo.HResolution = 1280;
  hmdInfo.VResolution = 800;
  hmdInfo.HScreenSize = 0.14976f;
  hmdInfo.VScreenSize = 0.09360f;
  hmdInfo.VScreenCenter = 0.04680f;
  hmdInfo.EyeToScreenDistance = 0.04100f;
  hmdInfo.LensSeparationDistance = 0.06350f;
  hmdInfo.InterpupillaryDistance = 0.06400f;
  hmdInfo.DistortionK[0] = 1;
  hmdInfo.DistortionK[1] = 0.22f;
  hmdInfo.DistortionK[2] = 0.24f;
  hmdInfo.DistortionK[3] = 0;
  hmdInfo.DesktopX = 100;
  hmdInfo.DesktopY = 100;
  hmdInfo.ChromaAbCorrection[0] = 0.99600f;
  hmdInfo.ChromaAbCorrection[1] = -0.00400f;
  hmdInfo.ChromaAbCorrection[2] = 1.01400f;
  hmdInfo.ChromaAbCorrection[3] = 0;
}

void Rift::getHmdInfo(
  const OVR::Ptr<OVR::DeviceManager> & ovrManager,
  OVR::HMDInfo & out) {
  if (!ovrManager) {
    FAIL("Unable to create Rift device manager");
  }
  OVR::Ptr<OVR::HMDDevice> ovrHmd = *ovrManager->
      EnumerateDevices<OVR::HMDDevice>().CreateDevice();
  if (!ovrHmd) {
    getDefaultDk1HmdValues(out);
    return;
  }
  ovrHmd->GetDeviceInfo(&out);
  ovrHmd = nullptr;
}

// Fetch a glm style quaternion from an OVR sensor fusion object
glm::quat Rift::getQuaternion(OVR::SensorFusion & sensorFusion) {
  return glm::quat(getEulerAngles(sensorFusion));
}

// Fetch a glm style quaternion from an OVR sensor fusion object
glm::mat4 Rift::getMat4(OVR::SensorFusion & sensorFusion) {
  return glm::mat4_cast(getQuaternion(sensorFusion));
}

// Fetch a glm vector containing Euler angles from an OVR sensor fusion object
glm::vec3 Rift::getEulerAngles(OVR::SensorFusion & sensorFusion) {
  return getEulerAngles(sensorFusion.GetPredictedOrientation());
}

glm::vec4 Rift::fromOvr(const OVR::Color & in) {
  glm::vec4 c;
  in.GetRGBA(&c.r, &c.g, &c.b, &c.a);
  return c;
}

glm::vec3 Rift::fromOvr(const OVR::Vector3f & in) {
  return glm::vec3(in.x, in.y, in.z);
}

glm::quat Rift::fromOvr(const OVR::Quatf & in) {
  return glm::quat(getEulerAngles(in));
}

glm::vec3 Rift::getEulerAngles(const OVR::Quatf & in) {
  glm::vec3 eulerAngles;
  in.GetEulerAngles<
      OVR::Axis_X, OVR::Axis_Y, OVR::Axis_Z,
      OVR::Rotate_CW, OVR::Handed_R
      >(&eulerAngles.x, &eulerAngles.y, &eulerAngles.z);
  return eulerAngles;
}

void Rift::getRiftPositionAndSize(
    const OVR::HMDInfo & hmdInfo,
    glm::ivec2 & windowPosition,
    glm::uvec2 & windowSize) {

  windowPosition = glm::ivec2(hmdInfo.DesktopX, hmdInfo.DesktopY);
  GLFWmonitor * hmdMonitor =
    GlfwApp::getMonitorAtPosition(windowPosition);

  if (!hmdMonitor) {
    FAIL("Unable to find Rift display");
  }

  const GLFWvidmode * videoMode =
    glfwGetVideoMode(hmdMonitor);
  windowSize = glm::ivec2(videoMode->width, videoMode->height);
}

double RiftDistortionHelper::getLensOffset(StereoEye eye) const{
  return (eye == LEFT) ? -lensOffset : lensOffset;
}

glm::dvec2 RiftDistortionHelper::screenToTexture(const glm::dvec2 & v) {
    return ((v + 1.0) / 2.0);
  }

glm::dvec2 RiftDistortionHelper::textureToScreen(const glm::dvec2 & v) {
    return ((v * 2.0) - 1.0);
  }

glm::dvec2 RiftDistortionHelper::screenToRift(const glm::dvec2 & v, StereoEye eye) const{
  return glm::dvec2(v.x + getLensOffset(eye), v.y / eyeAspect);
}

glm::dvec2 RiftDistortionHelper::riftToScreen(const glm::dvec2 & v, StereoEye eye) const{
  return glm::dvec2(v.x - getLensOffset(eye), v.y * eyeAspect);
  }

glm::dvec2 RiftDistortionHelper::textureToRift(const glm::dvec2 & v, StereoEye eye) const {
  return screenToRift(textureToScreen(v), eye);
  }

glm::dvec2 RiftDistortionHelper::riftToTexture(const glm::dvec2 & v, StereoEye eye) const{
  return screenToTexture(riftToScreen(v, eye));
  }

double RiftDistortionHelper::getUndistortionScaleForRadiusSquared(double rSq) const {
    double distortionScale = K[0]
      + rSq * (K[1] + rSq * (K[2] + rSq * K[3]));
    return distortionScale;
  }

double RiftDistortionHelper::getUndistortionScale(const glm::dvec2 & v) const {
    double rSq = glm::length2(v);
    return getUndistortionScaleForRadiusSquared(rSq);
  }

double RiftDistortionHelper::getUndistortionScaleForRadius(double r) const{
    return getUndistortionScaleForRadiusSquared(r * r);
  }

glm::dvec2 RiftDistortionHelper::getUndistortedPosition(const glm::dvec2 & v) const {
    return v * getUndistortionScale(v);
  }

glm::dvec2 RiftDistortionHelper::getTextureLookupValue(const glm::dvec2 & texCoord, StereoEye eye) const {
    glm::dvec2 riftPos = textureToRift(texCoord, eye);
    glm::dvec2 distorted = getUndistortedPosition(riftPos);
    glm::dvec2 result = riftToTexture(distorted, eye);
    return result;
  }

double RiftDistortionHelper::getDistortionScaleForRadius(double rTarget) const {
    double max = rTarget * 2;
    double min = 0;
    double distortionScale;
    while (true) {
      double rSource = ((max - min) / 2.0) + min;
      distortionScale = getUndistortionScaleForRadiusSquared(
        rSource * rSource);
      double rResult = distortionScale * rSource;
      if (glm::epsilonEqual(rResult, rTarget, 1e-6)) {
        break;
      }
      if (rResult < rTarget) {
        min = rSource;
      }
      else {
        max = rSource;
      }
    }
    return 1.0 / distortionScale;
  }

glm::dvec2 RiftDistortionHelper::findDistortedVertexPosition(const glm::dvec2 & source,
    StereoEye eye) const {
    const glm::dvec2 rift = screenToRift(source, eye);
    double rTarget = glm::length(rift);
    double distortionScale = getDistortionScaleForRadius(rTarget);
    glm::dvec2 result = rift * distortionScale;
    glm::dvec2 resultScreen = riftToScreen(result, eye);
    return resultScreen;
  }

RiftDistortionHelper::RiftDistortionHelper(const OVR::HMDInfo & hmdInfo) {
    OVR::Util::Render::DistortionConfig distortion;
    OVR::Util::Render::StereoConfig stereoConfig;
    stereoConfig.SetHMDInfo(hmdInfo);
    distortion = stereoConfig.GetDistortionConfig();

    // The Rift examples use a post-distortion scale to resize the
    // image upward after distorting it because their K values have
    // been chosen such that they always result in a scale > 1.0, and
    // thus shrink the image.  However, we can correct for that by
    // finding the distortion scale the same way the OVR examples do,
    // and then pre-multiplying the constants by it.
    double postDistortionScale = 1.0 / stereoConfig.GetDistortionScale();
    for (int i = 0; i < 4; ++i) {
      K[i] = distortion.K[i] * postDistortionScale;
    }
    lensOffset = distortion.XCenterOffset;
    eyeAspect = hmdInfo.HScreenSize / 2.0f / hmdInfo.VScreenSize;
  }

RiftLookupTexturePtr RiftDistortionHelper::createLookupTexture(const glm::uvec2 & lookupTextureSize, StereoEye eye) const {
    size_t lookupDataSize = lookupTextureSize.x * lookupTextureSize.y * 2;
    float * lookupData = new float[lookupDataSize];
    // The texture coordinates are actually from the center of the pixel, so thats what we need to use for the calculation.
    glm::dvec2 texCenterOffset = glm::dvec2(0.5f) / glm::dvec2(lookupTextureSize);
    size_t rowSize = lookupTextureSize.x * 2;
    for (size_t y = 0; y < lookupTextureSize.y; ++y) {
      for (size_t x = 0; x < lookupTextureSize.x; ++x) {
        size_t offset = (y * rowSize) + (x * 2);
        glm::dvec2 texCoord = (glm::dvec2(x, y) / glm::dvec2(lookupTextureSize)) + texCenterOffset;
        glm::dvec2 riftCoord = textureToRift(texCoord, eye);
        glm::dvec2 undistortedRiftCoord = getUndistortedPosition(riftCoord);
        glm::dvec2 undistortedTexCoord = riftToTexture(undistortedRiftCoord, eye);
        lookupData[offset] = (float)undistortedTexCoord.x;
        lookupData[offset + 1] = (float)undistortedTexCoord.y;
      }
    }

    RiftLookupTexturePtr outTexture(new RiftLookupTexture());
    outTexture->bind();
    outTexture->image2d(lookupTextureSize, lookupData, 0, GL_RG, GL_FLOAT);
    outTexture->parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    outTexture->parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    outTexture->parameter(GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    outTexture->parameter(GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    delete[] lookupData;
    return outTexture;
  }

gl::GeometryPtr RiftDistortionHelper::createDistortionMesh(
    const glm::uvec2 & distortionMeshResolution, StereoEye eye) const{
    std::vector<glm::vec4> vertexData;
    vertexData.reserve(
      distortionMeshResolution.x * distortionMeshResolution.y * 2);
    // The texture coordinates are actually from the center of the pixel, so thats what we need to use for the calculation.
    for (size_t y = 0; y < distortionMeshResolution.y; ++y) {
      for (size_t x = 0; x < distortionMeshResolution.x; ++x) {
        // Create a texture coordinate that goes from [0, 1]
        glm::dvec2 texCoord = (glm::dvec2(x, y)
          / glm::dvec2(distortionMeshResolution - glm::uvec2(1)));
        // Create the vertex coordinate in the range [-1, 1]
        glm::dvec2 vertexPos = (texCoord * 2.0) - 1.0;

        // now find the distorted vertex position from the original
        // scene position
        vertexPos = findDistortedVertexPosition(vertexPos, eye);
        vertexData.push_back(glm::vec4(vertexPos, 0, 1));
        vertexData.push_back(glm::vec4(texCoord, 0, 1));
      }
    }

    std::vector<GLuint> indexData;
    for (size_t y = 0; y < distortionMeshResolution.y - 1; ++y) {
      size_t rowStart = y * distortionMeshResolution.x;
      size_t nextRowStart = rowStart + distortionMeshResolution.x;
      for (size_t x = 0; x < distortionMeshResolution.x; ++x) {
        indexData.push_back(nextRowStart + x);
        indexData.push_back(rowStart + x);
      }
      indexData.push_back(UINT_MAX);

    }
    return gl::GeometryPtr(
      new gl::Geometry(vertexData, indexData, indexData.size(),
      gl::Geometry::Flag::HAS_TEXTURE,
      GL_TRIANGLE_STRIP));
  }


RiftApp::RiftApp(bool fullscreen) :  RiftGlfwApp(fullscreen) {

  OVR::Util::Render::StereoConfig ovrStereoConfig;
  ovrStereoConfig.SetHMDInfo(ovrHmdInfo);
  {
    OVR::Ptr<OVR::ProfileManager> profileManager =
      *OVR::ProfileManager::Create();
    OVR::Ptr<OVR::Profile> profile =
      *(profileManager->GetDeviceDefaultProfile(
      OVR::ProfileType::Profile_RiftDK1));
    float ipd = profile->GetIPD();
    ovrStereoConfig.SetIPD(ipd);
  }

  gl::Stacks::projection().top() =
    glm::perspective(ovrStereoConfig.GetYFOVRadians(),
    glm::aspect(eyeSize), 0.01f, 1000.0f);
  ovrStereoConfig.GetIPD();

  eyes[LEFT].viewportPosition =
    glm::uvec2(0, 0);
  eyes[LEFT].modelviewOffset = glm::translate(glm::mat4(),
    glm::vec3(ovrStereoConfig.GetIPD() / 2.0f, 0, 0));
  eyes[LEFT].projectionOffset = glm::translate(glm::mat4(),
    glm::vec3(ovrStereoConfig.GetProjectionCenterOffset(), 0, 0));

  eyes[RIGHT].viewportPosition =
    glm::uvec2(hmdNativeResolution.x / 2, 0);
  eyes[RIGHT].modelviewOffset = glm::translate(glm::mat4(),
    glm::vec3(-ovrStereoConfig.GetIPD() / 2.0f, 0, 0));
  eyes[RIGHT].projectionOffset = glm::translate(glm::mat4(),
    glm::vec3(-ovrStereoConfig.GetProjectionCenterOffset(), 0, 0));

  {
    glm::quat strabismusCorrection = Rift::getStrabismusCorrection();
    eyes[LEFT].strabsimusCorrection = glm::mat4_cast(strabismusCorrection);
    eyes[RIGHT].strabsimusCorrection = glm::mat4_cast(glm::inverse(strabismusCorrection));
  }

  distortionScale = ovrStereoConfig.GetDistortionScale();

  ovrSensor =
    *ovrManager->EnumerateDevices<OVR::SensorDevice>().
    CreateDevice();
  if (ovrSensor) {
    sensorFusion.AttachToSensor(ovrSensor);
  }

  if (!sensorFusion.IsAttachedToSensor()) {
    SAY_ERR("Could not attach to sensor device");
  }

  float eyeHeight = 1.5f;
  player = glm::inverse(glm::lookAt(
    glm::vec3(0, eyeHeight, 4),
    glm::vec3(0, eyeHeight, 0),
    glm::vec3(0, 1, 0)));
}

void RiftApp::createRenderingTarget() {
//  glfwWindowHint(GLFW_SAMPLES, 4);
  RiftGlfwApp::createRenderingTarget();
}

RiftApp::~RiftApp() {
  sensorFusion.AttachToSensor(nullptr);
  ovrSensor.Clear();
  ovrManager.Clear();
}

void RiftApp::initGl() {
  RiftGlfwApp::initGl();
  query = gl::TimeQueryPtr(new gl::TimeQuery());
  GL_CHECK_ERROR;

  ///////////////////////////////////////////////////////////////////////////
  // Initialize OpenGL settings and variables
  // Anti-alias lines (hopefully)
  glEnable(GL_BLEND);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
  GL_CHECK_ERROR;

  // Allocate the frameBuffer that will hold the scene, and then be
  // re-rendered to the screen with distortion
  glm::uvec2 frameBufferSize = glm::uvec2(
    glm::vec2(eyeSize) *
    distortionScale);
  frameBuffer.init(frameBufferSize);
  GL_CHECK_ERROR;

  // Create the buffers for the texture quad we will draw
  quadGeometry = GlUtils::getQuadGeometry();

  // Create the rendering displacement map
  RiftDistortionHelper helper(ovrHmdInfo);
  for_each_eye([&](StereoEye eye){
    eyes[eye].distortionTexture =
      helper.createLookupTexture(glm::uvec2(512, 512), eye);
  });

#ifdef RIFT_MULTISAMPLE
  distortProgram = GlUtils::getProgram(
    Resource::SHADERS_TEXTURED_VS,
    Resource::SHADERS_RIFTWARPMULTISAMPLE_FS);
#else
  distortProgram = GlUtils::getProgram(
    Resource::SHADERS_TEXTURED_VS,
    Resource::SHADERS_RIFTWARP_FS);
#endif

  // Create the rendering shaders
  distortProgram->use();
  distortProgram->setUniform1i("OffsetMap", 1);
  distortProgram->setUniform1i("Scene", 0);
  gl::Program::clear();
  GL_CHECK_ERROR;
}

void RiftApp::onKey(int key, int scancode, int action, int mods) {
  if (GLFW_PRESS == action) switch (key) {
  case GLFW_KEY_R:
    sensorFusion.Reset();
    return;
  }

  // Allow the camera controller to intercept the input
  if (CameraControl::instance().onKey(player, key, scancode, action, mods)) {
    return;
  }
  RiftGlfwApp::onKey(key, scancode, action, mods);
}


void RiftApp::draw() {
  glClear(GL_COLOR_BUFFER_BIT);
  gl::MatrixStack & mv = gl::Stacks::modelview();
  gl::MatrixStack & pr = gl::Stacks::projection();

  for_each_eye([&](StereoEye eye){
    const RiftPerEyeArg & eyeArgs = eyes[eye];
    frameBuffer.activate();
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl::Stacks::with_push(pr, mv, [&]{
      mv.preMultiply(eyeArgs.modelviewOffset);
      pr.preMultiply(eyeArgs.projectionOffset);
#ifdef RIFT_MULTISAMPLE
      glEnable(GL_MULTISAMPLE);
#endif

      renderScene();
      GL_CHECK_ERROR;

#ifdef RIFT_MULTISAMPLE
      glDisable(GL_MULTISAMPLE);
#endif

    });

    frameBuffer.deactivate();
    glDisable(GL_DEPTH_TEST);

    viewport(eye);
    distortProgram->use();
    glActiveTexture(GL_TEXTURE1);
    eyeArgs.distortionTexture->bind();
    glActiveTexture(GL_TEXTURE0);
    frameBuffer.color->bind();
    quadGeometry->bindVertexArray();
    quadGeometry->draw();
    gl::VertexArray::unbind();
    gl::Program::clear();
    GL_CHECK_ERROR;
  });
}

void RiftApp::update() {
  RiftGlfwApp::update();
  CameraControl::instance().applyInteraction(player);

  riftOrientation = glm::mat4_cast(Rift::fromOvr(sensorFusion.GetPredictedOrientation()));

  static const glm::vec4 EYE_ROTATION_OFFSET(0, 0.15f, -0.09f, 1);
  glm::vec3 riftImposedTranslation =
    glm::vec3(glm::inverse(riftOrientation) * EYE_ROTATION_OFFSET);
  riftImposedTranslation -= glm::vec3(EYE_ROTATION_OFFSET);
  glm::mat4 playerRiftTranslation =
    glm::translate(glm::mat4(), riftImposedTranslation);
  gl::Stacks::modelview().top() = riftOrientation *
    glm::inverse(playerRiftTranslation * player);

  gl::Stacks::modelview().top() =
    riftOrientation * glm::inverse(player);
}

void RiftApp::renderStringAt(const std::string & str, float x, float y) {
  gl::MatrixStack & mv = gl::Stacks::modelview();
  gl::MatrixStack & pr = gl::Stacks::projection();
  gl::Stacks::with_push(mv, pr, [&]{
    mv.identity();
    pr.top() = 1.0f * glm::ortho(
      -1.0f, 1.0f,
      -windowAspectInverse * 2.0f, windowAspectInverse * 2.0f,
      -100.0f, 100.0f);
    glm::vec2 cursor(x, windowAspectInverse * y);
    GlUtils::renderString(str, cursor, 18.0f);
  });
}
