#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)

// CGD6214 - Interactive Solar System Simulator
// Features: Sun + 4 planets + moon, hierarchical transforms, Phong lighting,
//           procedural textures, orbit rings, starfield skybox, keyboard/mouse controls
//           Camera Modes: Free, Orbit, Follow Planet, Top View
//           Meteor shower, wireframe, lighting toggle, info panel

#define STB_IMAGE_IMPLEMENTATION

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <string>
#include <sstream>
#include "stb_image.h"
#include <direct.h>
#include <cstdio>
#include <ranges>

const float PI = 3.14159265358979323846f;

// -----------------------------------------------------------
//  SHADERS
// -----------------------------------------------------------

const char* planetVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;

void main(){
    FragPos    = vec3(model * vec4(aPos, 1.0));
    Normal     = normalMatrix * aNormal;
    UV         = aUV;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* planetFragSrc = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;

uniform vec3  objectColor;
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform vec3  viewPos;
uniform bool  isSun;
uniform int   planetType;
uniform bool  lightingEnabled;

uniform sampler2D planetTexture;
uniform bool useTexture;

float hash(vec2 p){
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p+45.32);
    return fract(p.x * p.y);
}
float smoothNoise(vec2 p){
    vec2 i = floor(p); vec2 f = fract(p);
    f = f*f*(3.0-2.0*f);
    float a=hash(i), b=hash(i+vec2(1,0)), c=hash(i+vec2(0,1)), d=hash(i+vec2(1,1));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
float fbm(vec2 p){
    float v=0.0, a=0.5;
    for(int i=0;i<5;i++){ v+=a*smoothNoise(p); p*=2.0; a*=0.5; }
    return v;
}

vec3 sunTexture(vec2 uv){
    float n=fbm(uv*4.0+vec2(0.3,0.7)), n2=fbm(uv*8.0-vec2(0.5,0.2));
    vec3 col=mix(mix(vec3(1.0,0.9,0.1),vec3(1.0,0.4,0.0),n),vec3(0.9,0.1,0.0),n2*0.5);
    col*=0.7+0.3*smoothstep(0.45,0.5,length(fract(uv*3.0)-0.5));
    return col;
}
vec3 mercuryTexture(vec2 uv){ float n=fbm(uv*6.0); return mix(vec3(0.5,0.45,0.4),vec3(0.35,0.3,0.28),n); }
vec3 venusTexture(vec2 uv){ float n=fbm(uv*3.0); return mix(vec3(0.9,0.75,0.4),vec3(0.7,0.55,0.25),sin(uv.y*20.0+n*5.0)*0.5+0.5); }
vec3 earthTexture(vec2 uv){
    float ocean=fbm(uv*3.5), land=fbm(uv*5.0+vec2(1.3,2.1)), ice=smoothstep(0.35,0.5,abs(uv.y-0.5)*2.0);
    vec3 col=mix(mix(vec3(0.05,0.25,0.7),vec3(0.1,0.45,0.85),ocean),mix(vec3(0.15,0.55,0.15),vec3(0.55,0.45,0.2),land),smoothstep(0.45,0.55,land));
    col=mix(col,vec3(0.95),smoothstep(0.5,0.65,fbm(uv*6.0+vec2(0.5)))*0.7);
    return mix(col,vec3(0.98),ice);
}
vec3 marsTexture(vec2 uv){ float n=fbm(uv*4.0); return mix(mix(vec3(0.7,0.3,0.1),vec3(0.85,0.5,0.2),n),vec3(0.5,0.2,0.08),smoothstep(0.65,0.7,fbm(uv*12.0))*0.6); }
vec3 moonTexture(vec2 uv){ float n=fbm(uv*5.0); return mix(mix(vec3(0.6,0.6,0.6),vec3(0.75,0.75,0.72),n),vec3(0.35,0.35,0.35),smoothstep(0.6,0.65,fbm(uv*14.0))); }
vec3 saturnTexture(vec2 uv){ return mix(vec3(0.9,0.8,0.6),vec3(0.75,0.6,0.4),sin(uv.y*30.0)*0.3+fbm(uv*4.0)*0.4); }
vec3 ringTexture(vec2 uv){ return vec3(0.75,0.65,0.5)*( sin(uv.x*80.0)*0.4+0.6)*(0.7+0.3*fbm(vec2(uv.x*10.0,uv.y*5.0))); }

void main(){
    vec3 texColor;
    if(planetType==0)      texColor=sunTexture(UV);
    else if(planetType==1) texColor=mercuryTexture(UV);
    else if(planetType==2) texColor=venusTexture(UV);
    else if(planetType==3) texColor=earthTexture(UV);
    else if(planetType==4) texColor=marsTexture(UV);
    else if(planetType==5) texColor=moonTexture(UV);
    else if(planetType==6) texColor=saturnTexture(UV);
    else                   texColor=ringTexture(UV);

    if(useTexture) texColor = texture(planetTexture, UV).rgb;
    else           texColor *= objectColor;

    if(isSun){
        FragColor = vec4(texColor + vec3(0.05*fbm(UV*3.0+vec2(0.1))), 1.0);
        return;
    }

    if(!lightingEnabled){
        float alpha = (planetType==7) ? 0.75 : 1.0;
        FragColor = vec4(texColor, alpha);
        return;
    }

    vec3 norm     = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir  = normalize(viewPos  - FragPos);
    vec3 reflDir  = reflect(-lightDir, norm);
    float diff    = max(dot(norm, lightDir), 0.0);
    float spec    = pow(max(dot(viewDir, reflDir), 0.0), 32.0) * 0.4;
    vec3 result   = (0.08 + diff + spec) * lightColor * texColor;
    float alpha   = (planetType==7) ? 0.75 : 1.0;
    FragColor     = vec4(result, alpha);
}
)";

// --- Meteor shader ---
const char* meteorVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main(){ gl_Position = projection * view * model * vec4(aPos,1.0); }
)";
const char* meteorFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 meteorColor;
void main(){ FragColor = vec4(meteorColor, 1.0); }
)";

// --- Orbit line shader ---
const char* orbitVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main(){ gl_Position = projection * view * model * vec4(aPos,1.0); }
)";
const char* orbitFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 lineColor;
void main(){ FragColor = lineColor; }
)";

// --- Starfield shader ---
const char* starVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main(){
    mat4 v = mat4(mat3(view));
    gl_Position = (projection * v * vec4(aPos,1.0)).xyww;
}
)";
const char* starFragSrc = R"(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,1.0,1.0); }
)";

// --- Skybox shader ---
const char* skyboxVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
out vec3 TexCoords;
uniform mat4 projection;
uniform mat4 view;
void main(){
    TexCoords   = aPos;
    vec4 pos    = projection * view * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";
const char* skyboxFragSrc = R"(
#version 330 core
out vec4 FragColor;
in vec3 TexCoords;
uniform samplerCube skybox;
void main(){
    FragColor = texture(skybox, TexCoords);
}
)";

// -----------------------------------------------------------
//  DATA STRUCTURES
// -----------------------------------------------------------

struct CelestialBody {
    std::string  name;
    int          planetType;
    glm::vec3    color;
    float        radius;
    float        orbitRadius;
    float        orbitSpeed;
    float        spinSpeed;
    float        orbitAngle;
    float        spinAngle;
    float        orbitTilt;
    bool         hasSaturnRing;
    int          parentIndex;

    unsigned int VAO, VBO, EBO;
    std::vector<float>        verts;
    std::vector<unsigned int> inds;

    unsigned int orbitVAO, orbitVBO;
    int          orbitVertCount;

    unsigned int ringVAO, ringVBO, ringEBO;
    std::vector<float>        ringVerts;
    std::vector<unsigned int> ringInds;

    glm::vec3 worldPos;

    CelestialBody() : planetType(0), color(1.0f), radius(1.0f),
        orbitRadius(0.0f), orbitSpeed(0.5f), spinSpeed(1.0f),
        orbitAngle(0.0f), spinAngle(0.0f), orbitTilt(0.0f),
        hasSaturnRing(false), parentIndex(-1),
        VAO(0), VBO(0), EBO(0),
        orbitVAO(0), orbitVBO(0), orbitVertCount(0),
        ringVAO(0), ringVBO(0), ringEBO(0),
        worldPos(0.0f) {
    }
};

// Meteor
struct Meteor {
    glm::vec3 pos;
    glm::vec3 vel;
    float     life;      // seconds remaining
    float     maxLife;
    glm::vec3 color;
    bool      active;
};

// -----------------------------------------------------------
//  GLOBALS
// -----------------------------------------------------------
std::vector<CelestialBody> bodies;

// Camera
enum CameraMode { CAM_FREE = 0, CAM_ORBIT = 1, CAM_FOLLOW = 2, CAM_TOP = 3 };
CameraMode camMode = CAM_FREE;
int        selectedPlanet = 3;   // Earth by default (index into bodies)

glm::vec3 camPos = glm::vec3(0.0f, 12.0f, 28.0f);
glm::vec3 camFront = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
glm::vec3 camUp = glm::vec3(0.0f, 1.0f, 0.0f);
float     camYaw = -90.0f;
float     camPitch = -18.0f;
float     camSpeed = 8.0f;
float     fov = 45.0f;

// Orbit-mode state
float orbitAngleH = 0.0f;   // horizontal angle around target
float orbitAngleV = 30.0f;  // vertical angle
float orbitDist = 20.0f;

// Mouse
bool  firstMouse = true;
float lastX = 800.0f;
float lastY = 450.0f;
bool  mouseLook = false;

// State
bool  paused = false;
bool  showOrbits = true;
bool  wireframe = false;
bool  lightingOn = true;
bool  showInfo = true;
float timeScale = 1.0f;
float globalTime = 0.0f;

// Meteor shower
std::vector<Meteor> meteors;
unsigned int meteorVAO = 0, meteorVBO = 0;
unsigned int meteorProg = 0;
bool meteorShowerActive = false;

// Programs
unsigned int planetProg = 0;
unsigned int orbitProg = 0;
unsigned int starProg = 0;

// Textures
unsigned int sunTex = 0;
unsigned int mercuryTex = 0;
unsigned int venusTex = 0;
unsigned int earthTex = 0;
unsigned int moonTex = 0;
unsigned int marsTex = 0;
unsigned int saturnTex = 0;
unsigned int ringTex = 0;

// Starfield
unsigned int starVAO = 0, starVBO = 0;
int          starCount = 0;

// Skybox
unsigned int skyboxVAO = 0, skyboxVBO = 0;
unsigned int skyboxCubemap = 0;
unsigned int skyboxProg = 0;

float skyboxVerts[] = {
    -1, 1,-1,  -1,-1,-1,   1,-1,-1,   1,-1,-1,   1, 1,-1,  -1, 1,-1,
    -1,-1, 1,  -1,-1,-1,  -1, 1,-1,  -1, 1,-1,  -1, 1, 1,  -1,-1, 1,
     1,-1,-1,   1,-1, 1,   1, 1, 1,   1, 1, 1,   1, 1,-1,   1,-1,-1,
    -1,-1, 1,  -1, 1, 1,   1, 1, 1,   1, 1, 1,   1,-1, 1,  -1,-1, 1,
    -1, 1,-1,   1, 1,-1,   1, 1, 1,   1, 1, 1,  -1, 1, 1,  -1, 1,-1,
    -1,-1,-1,  -1,-1, 1,   1,-1,-1,   1,-1,-1,  -1,-1, 1,   1,-1, 1
};

int WIN_W = 1280, WIN_H = 720;

// Planet names for HUD
const char* planetNames[] = { "Sun","Mercury","Venus","Earth","Moon","Mars","Saturn" };

// -----------------------------------------------------------
//  UTILITY
// -----------------------------------------------------------

unsigned int compileShader(unsigned int type, const char* src) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &src, NULL);
    glCompileShader(id);
    int ok; char log[512];
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(id, 512, NULL, log); std::cerr << "Shader error: " << log << "\n"; }
    return id;
}

unsigned int makeProgram(const char* vs, const char* fs) {
    unsigned int v = compileShader(GL_VERTEX_SHADER, vs);
    unsigned int f = compileShader(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    int ok; char log[512];
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, 512, NULL, log); std::cerr << "Link error: " << log << "\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

unsigned int loadTexture(const char* path) {
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 1) ? GL_RED : (ch == 3) ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "[OK] Loaded: " << path << " (" << w << "x" << h << ")\n";
        stbi_image_free(data);
    }
    else {
        std::cout << "[ERROR] Failed: " << path << " -> " << stbi_failure_reason() << "\n";
        unsigned char white[3] = { 255,255,255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
        glGenerateMipmap(GL_TEXTURE_2D);
        texID = 0;   // signal failure so procedural fallback kicks in
    }
    return texID;
}

unsigned int loadCubemap(std::vector<std::string> faces) {
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

    int w, h, ch;
    bool anyLoaded = false;
    for (int i = 0; i < 6; i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &w, &h, &ch, 0);
        if (data) {
            GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
            std::cout << "[OK] Cubemap face " << i << ": " << faces[i] << "\n";
            anyLoaded = true;
        }
        else {
            // Deep-space dark blue fallback pixel
            unsigned char dark[3] = { 0, 0, 8 };
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, dark);
            std::cout << "[WARN] Missing cubemap face: " << faces[i] << "\n";
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (!anyLoaded) { std::cout << "[ERROR] No cubemap faces loaded!\n"; }
    return texID;
}

// -----------------------------------------------------------
//  GEOMETRY BUILDERS
// -----------------------------------------------------------

void buildSphere(CelestialBody& b, float r, int stacks = 32, int slices = 64) {
    b.verts.clear(); b.inds.clear();
    for (int i = 0; i <= stacks; i++) {
        float phi = PI / 2.0f - i * (PI / (float)stacks);
        float cosP = cos(phi), sinP = sin(phi);
        for (int j = 0; j <= slices; j++) {
            float theta = j * 2.0f * PI / (float)slices;
            float x = cosP * cos(theta), y = sinP, z = cosP * sin(theta);
            b.verts.push_back(r * x); b.verts.push_back(r * y); b.verts.push_back(r * z);
            b.verts.push_back(x);   b.verts.push_back(y);   b.verts.push_back(z);
            b.verts.push_back((float)j / slices);
            b.verts.push_back(1.0f - (float)i / stacks);
        }
    }
    for (int i = 0; i < stacks; i++) {
        int k1 = i * (slices + 1), k2 = k1 + slices + 1;
        for (int j = 0; j < slices; j++, k1++, k2++) {
            b.inds.push_back(k1);   b.inds.push_back(k2);   b.inds.push_back(k1 + 1);
            b.inds.push_back(k1 + 1); b.inds.push_back(k2);   b.inds.push_back(k2 + 1);
        }
    }
}

void buildRing(CelestialBody& b, float inner, float outer, int segs = 64) {
    b.ringVerts.clear(); b.ringInds.clear();
    for (int i = 0; i <= segs; i++) {
        float a = i * 2.0f * PI / segs, ca = cos(a), sa = sin(a);
        float u = (float)i / segs;
        b.ringVerts.push_back(outer * ca); b.ringVerts.push_back(0); b.ringVerts.push_back(outer * sa);
        b.ringVerts.push_back(0); b.ringVerts.push_back(1); b.ringVerts.push_back(0);
        b.ringVerts.push_back(1); b.ringVerts.push_back(u);
        b.ringVerts.push_back(inner * ca); b.ringVerts.push_back(0); b.ringVerts.push_back(inner * sa);
        b.ringVerts.push_back(0); b.ringVerts.push_back(1); b.ringVerts.push_back(0);
        b.ringVerts.push_back(0); b.ringVerts.push_back(u);
    }
    for (int i = 0; i < segs; i++) {
        unsigned int o = i * 2;
        b.ringInds.push_back(o);   b.ringInds.push_back(o + 1); b.ringInds.push_back(o + 2);
        b.ringInds.push_back(o + 1); b.ringInds.push_back(o + 3); b.ringInds.push_back(o + 2);
    }
}

void buildOrbitLine(CelestialBody& b, float radius, float tilt, int segs = 128) {
    std::vector<float> pts;
    for (int i = 0; i <= segs; i++) {
        float a = i * 2.0f * PI / segs;
        float x = radius * cos(a), z = radius * sin(a);
        float y = x * sin(glm::radians(tilt));
        x *= cos(glm::radians(tilt));
        pts.push_back(x); pts.push_back(y); pts.push_back(z);
    }
    glGenVertexArrays(1, &b.orbitVAO);
    glGenBuffers(1, &b.orbitVBO);
    glBindVertexArray(b.orbitVAO);
    glBindBuffer(GL_ARRAY_BUFFER, b.orbitVBO);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(float), pts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    b.orbitVertCount = (int)pts.size() / 3;
}

void setupBodyBuffers(CelestialBody& b) {
    glGenVertexArrays(1, &b.VAO);
    glGenBuffers(1, &b.VBO);
    glGenBuffers(1, &b.EBO);
    glBindVertexArray(b.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, b.VBO);
    glBufferData(GL_ARRAY_BUFFER, b.verts.size() * sizeof(float), b.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.inds.size() * sizeof(unsigned int), b.inds.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void setupRingBuffers(CelestialBody& b) {
    glGenVertexArrays(1, &b.ringVAO);
    glGenBuffers(1, &b.ringVBO);
    glGenBuffers(1, &b.ringEBO);
    glBindVertexArray(b.ringVAO);
    glBindBuffer(GL_ARRAY_BUFFER, b.ringVBO);
    glBufferData(GL_ARRAY_BUFFER, b.ringVerts.size() * sizeof(float), b.ringVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ringEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.ringInds.size() * sizeof(unsigned int), b.ringInds.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void buildStarfield() {
    std::vector<float> pts;
    srand(42);
    starCount = 3000;
    for (int i = 0; i < starCount; i++) {
        float theta = ((float)rand() / RAND_MAX) * 2.0f * PI;
        float phi = acos(2.0f * ((float)rand() / RAND_MAX) - 1.0f);
        float r = 90.0f;
        pts.push_back(r * sin(phi) * cos(theta));
        pts.push_back(r * cos(phi));
        pts.push_back(r * sin(phi) * sin(theta));
    }
    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(float), pts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

// -----------------------------------------------------------
//  SKYBOX  
// -----------------------------------------------------------
void initSkybox() {
    skyboxProg = makeProgram(skyboxVertSrc, skyboxFragSrc);

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVerts), skyboxVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Paths relative to the exe (x64\Debug\)
    std::vector<std::string> faces = {
        "textures/skybox/right.jpg",
        "textures/skybox/left.jpg",
        "textures/skybox/top.jpg",
        "textures/skybox/bottom.jpg",
        "textures/skybox/front.jpg",
        "textures/skybox/back.jpg"
    };
    skyboxCubemap = loadCubemap(faces);
    std::cout << "Skybox cubemap ID: " << skyboxCubemap << "\n";
}

void drawSkybox(const glm::mat4& view, const glm::mat4& proj) {
    if (skyboxCubemap == 0) return;

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(skyboxProg);

    // Strip translation from view matrix (skybox stays fixed)
    glm::mat4 skyView = glm::mat4(glm::mat3(view));
    glUniformMatrix4fv(glGetUniformLocation(skyboxProg, "view"), 1, GL_FALSE, glm::value_ptr(skyView));
    glUniformMatrix4fv(glGetUniformLocation(skyboxProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);
    glUniform1i(glGetUniformLocation(skyboxProg, "skybox"), 0);

    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

// -----------------------------------------------------------
//  METEOR SHOWER
// -----------------------------------------------------------
void initMeteorBuffers() {
    // Simple unit sphere for each meteor (tiny)
    float sphere[3] = { 0,0,0 };
    glGenVertexArrays(1, &meteorVAO);
    glGenBuffers(1, &meteorVBO);
    glBindVertexArray(meteorVAO);
    glBindBuffer(GL_ARRAY_BUFFER, meteorVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sphere), sphere, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void spawnMeteorShower() {
    meteors.clear();
    meteorShowerActive = true;
    int count = 40;
    for (int i = 0; i < count; i++) {
        Meteor m;
        // Spawn in a band above the solar system
        float angle = ((float)rand() / RAND_MAX) * 2.0f * PI;
        float dist = 15.0f + ((float)rand() / RAND_MAX) * 10.0f;
        m.pos = glm::vec3(dist * cos(angle), 8.0f + ((float)rand() / RAND_MAX) * 4.0f, dist * sin(angle));
        // Velocity toward the sun with random spread
        glm::vec3 toSun = glm::normalize(-m.pos);
        m.vel = toSun * (6.0f + ((float)rand() / RAND_MAX) * 4.0f)
            + glm::vec3(((float)rand() / RAND_MAX - 0.5f) * 2.0f,
                -2.0f - ((float)rand() / RAND_MAX) * 2.0f,
                ((float)rand() / RAND_MAX - 0.5f) * 2.0f);
        m.maxLife = m.life = 2.0f + ((float)rand() / RAND_MAX) * 2.0f;
        // Orange/yellow/white colours
        float t = (float)rand() / RAND_MAX;
        m.color = glm::mix(glm::vec3(1.0f, 0.4f, 0.1f), glm::vec3(1.0f, 1.0f, 0.8f), t);
        m.active = true;
        meteors.push_back(m);
    }
}

void updateMeteors(float dt) {
    bool anyAlive = false;
    for (auto& m : meteors) {
        if (!m.active) continue;
        m.life -= dt;
        if (m.life <= 0.0f) { m.active = false; continue; }
        m.pos += m.vel * dt;
        anyAlive = true;
    }
    if (!anyAlive) { meteorShowerActive = false; meteors.clear(); }
}

void drawMeteors(const glm::mat4& view, const glm::mat4& proj) {
    if (meteors.empty()) return;
    glUseProgram(meteorProg);
    glUniformMatrix4fv(glGetUniformLocation(meteorProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(meteorProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glPointSize(4.0f);
    for (auto& m : meteors) {
        if (!m.active) continue;
        float fade = m.life / m.maxLife;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), m.pos);
        glUniformMatrix4fv(glGetUniformLocation(meteorProg, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(meteorProg, "meteorColor"), 1, glm::value_ptr(m.color * fade));
        glBindVertexArray(meteorVAO);
        glDrawArrays(GL_POINTS, 0, 1);
    }
}

// -----------------------------------------------------------
//  SOLAR SYSTEM INIT
// -----------------------------------------------------------
void initSolarSystem() {
    bodies.clear();

    auto addBody = [&](const char* name, int type, glm::vec3 col,
        float radius, float orbitR, float orbitSpd,
        float spinSpd, float tilt, float startAngle,
        int parent, bool ring = false)
        {
            CelestialBody b;
            b.name = name;
            b.planetType = type;
            b.color = col;
            b.radius = radius;
            b.orbitRadius = orbitR;
            b.orbitSpeed = orbitSpd;
            b.spinSpeed = spinSpd;
            b.orbitTilt = tilt;
            b.orbitAngle = startAngle;
            b.parentIndex = parent;
            b.hasSaturnRing = ring;
            buildSphere(b, radius, 48, 96);
            setupBodyBuffers(b);
            if (orbitR > 0.0f) buildOrbitLine(b, orbitR, tilt);
            if (ring) { buildRing(b, radius * 1.3f, radius * 2.3f); setupRingBuffers(b); }
            bodies.push_back(b);
        };

    //              name       type  color                            r     orbitR  spd   spin  tilt  angle  parent
    addBody("Sun", 0, glm::vec3(1.0f, 0.95f, 0.8f), 2.5f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, -1);
    addBody("Mercury", 1, glm::vec3(0.8f, 0.75f, 0.7f), 0.35f, 5.0f, 1.6f, 0.4f, 7.0f, 0.4f, 0);
    addBody("Venus", 2, glm::vec3(1.0f, 0.9f, 0.6f), 0.6f, 7.5f, 1.15f, 0.3f, 3.4f, 1.2f, 0);
    addBody("Earth", 3, glm::vec3(0.8f, 0.9f, 1.0f), 0.65f, 10.5f, 0.8f, 1.0f, 0.0f, 2.1f, 0);
    addBody("Moon", 5, glm::vec3(0.85f, 0.85f, 0.82f), 0.18f, 1.3f, 2.5f, 0.5f, 5.1f, 0.0f, 3);
    addBody("Mars", 4, glm::vec3(1.0f, 0.6f, 0.4f), 0.45f, 14.0f, 0.55f, 0.9f, 1.8f, 3.5f, 0);
    addBody("Saturn", 6, glm::vec3(1.0f, 0.9f, 0.7f), 1.1f, 19.0f, 0.32f, 1.2f, 2.5f, 1.0f, 0, true);
}

// -----------------------------------------------------------
//  UPDATE
// -----------------------------------------------------------
void updateBodies(float dt) {
    if (paused) return;
    float adt = dt * timeScale;
    globalTime += adt;
    for (auto& b : bodies) {
        b.spinAngle += b.spinSpeed * adt;
        b.orbitAngle += b.orbitSpeed * adt;
    }
}

glm::vec3 resolveWorldPos(int idx) {
    if (idx < 0 || idx >= (int)bodies.size()) return glm::vec3(0.0f);
    const auto& b = bodies[idx];
    glm::vec3 parentPos = (b.parentIndex >= 0) ? resolveWorldPos(b.parentIndex) : glm::vec3(0.0f);
    float tiltRad = glm::radians(b.orbitTilt);
    float x = b.orbitRadius * cos(b.orbitAngle);
    float z = b.orbitRadius * sin(b.orbitAngle);
    float y = x * sin(tiltRad);
    x *= cos(tiltRad);
    return parentPos + glm::vec3(x, y, z);
}

// -----------------------------------------------------------
//  CAMERA 
// -----------------------------------------------------------
void updateCamera(float dt) {
    if (camMode == CAM_FREE) return;   // free mode handled by input

    // Clamp selected planet index
    if (selectedPlanet < 0) selectedPlanet = 0;
    if (selectedPlanet >= (int)bodies.size()) selectedPlanet = (int)bodies.size() - 1;

    glm::vec3 target = bodies[selectedPlanet].worldPos;

    if (camMode == CAM_ORBIT) {
        // Slowly orbit around the selected planet
        orbitAngleH += 20.0f * dt;   // degrees/sec auto-rotate
        float hRad = glm::radians(orbitAngleH);
        float vRad = glm::radians(orbitAngleV);
        camPos = target + glm::vec3(
            orbitDist * cos(vRad) * cos(hRad),
            orbitDist * sin(vRad),
            orbitDist * cos(vRad) * sin(hRad));
        camFront = glm::normalize(target - camPos);
    }
    else if (camMode == CAM_FOLLOW) {
        // Fixed offset behind + above the planet
        float followDist = bodies[selectedPlanet].radius * 6.0f + 3.0f;
        camPos = target + glm::vec3(0.0f, followDist * 0.5f, followDist);
        camFront = glm::normalize(target - camPos);
    }
    else if (camMode == CAM_TOP) {
        camPos = glm::vec3(0.0f, 50.0f, 0.0f);
        camFront = glm::vec3(0.0f, -1.0f, 0.0f);
        camUp = glm::vec3(0.0f, 0.0f, -1.0f);
    }

    if (camMode != CAM_TOP)
        camUp = glm::vec3(0.0f, 1.0f, 0.0f);
}

// -----------------------------------------------------------
//  DRAW 
// -----------------------------------------------------------
void setMVP(unsigned int prog, const glm::mat4& model,
    const glm::mat4& view, const glm::mat4& proj) {
    glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(prog, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(prog, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(model)));
    glUniformMatrix3fv(glGetUniformLocation(prog, "normalMatrix"), 1, GL_FALSE, glm::value_ptr(nm));
}

void drawStars(const glm::mat4& view, const glm::mat4& proj) {
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(starProg);
    glUniformMatrix4fv(glGetUniformLocation(starProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(starProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glBindVertexArray(starVAO);
    glPointSize(2.0f);
    glDrawArrays(GL_POINTS, 0, starCount);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void drawOrbitLine(const CelestialBody& b, const glm::mat4& view, const glm::mat4& proj) {
    if (!showOrbits || b.orbitVAO == 0) return;
    glUseProgram(orbitProg);
    glm::mat4 model = glm::mat4(1.0f);
    if (b.parentIndex >= 0) model = glm::translate(model, bodies[b.parentIndex].worldPos);
    glUniformMatrix4fv(glGetUniformLocation(orbitProg, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(orbitProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(orbitProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    // Highlight selected planet orbit
    int idx = (int)(&b - &bodies[0]);
    if (idx == selectedPlanet)
        glUniform4f(glGetUniformLocation(orbitProg, "lineColor"), 1.0f, 0.8f, 0.2f, 0.9f);
    else
        glUniform4f(glGetUniformLocation(orbitProg, "lineColor"), 0.4f, 0.4f, 0.6f, 0.5f);
    glBindVertexArray(b.orbitVAO);
    glDrawArrays(GL_LINE_STRIP, 0, b.orbitVertCount);
}

void drawBody(CelestialBody& b, const glm::mat4& view, const glm::mat4& proj) {
    b.worldPos = resolveWorldPos((int)(&b - &bodies[0]));

    glUseProgram(planetProg);

    unsigned int currentTexture = 0;
    switch (b.planetType) {
    case 0: currentTexture = sunTex;     break;
    case 1: currentTexture = mercuryTex; break;
    case 2: currentTexture = venusTex;   break;
    case 3: currentTexture = earthTex;   break;
    case 4: currentTexture = marsTex;    break;
    case 5: currentTexture = moonTex;    break;
    case 6: currentTexture = saturnTex;  break;
    }

    bool texValid = (currentTexture != 0);
    glActiveTexture(GL_TEXTURE0);
    if (texValid) glBindTexture(GL_TEXTURE_2D, currentTexture);
    glUniform1i(glGetUniformLocation(planetProg, "planetTexture"), 0);
    glUniform1i(glGetUniformLocation(planetProg, "useTexture"), texValid ? 1 : 0);

    glUniform3fv(glGetUniformLocation(planetProg, "lightPos"), 1, glm::value_ptr(glm::vec3(0.0f)));
    glUniform3fv(glGetUniformLocation(planetProg, "lightColor"), 1, glm::value_ptr(glm::vec3(1.0f)));
    glUniform3fv(glGetUniformLocation(planetProg, "viewPos"), 1, glm::value_ptr(camPos));
    glUniform1i(glGetUniformLocation(planetProg, "isSun"), b.name == "Sun" ? 1 : 0);
    glUniform1i(glGetUniformLocation(planetProg, "planetType"), b.planetType);
    glUniform3fv(glGetUniformLocation(planetProg, "objectColor"), 1, glm::value_ptr(b.color));
    glUniform1i(glGetUniformLocation(planetProg, "lightingEnabled"), lightingOn ? 1 : 0);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), b.worldPos);
    model = glm::rotate(model, b.spinAngle, glm::vec3(0, 1, 0));
    setMVP(planetProg, model, view, proj);

    glBindVertexArray(b.VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)b.inds.size(), GL_UNSIGNED_INT, 0);

    // Saturn ring
    if (b.hasSaturnRing) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        bool ringTexValid = (ringTex != 0);
        glActiveTexture(GL_TEXTURE0);
        if (ringTexValid) glBindTexture(GL_TEXTURE_2D, ringTex);
        glUniform1i(glGetUniformLocation(planetProg, "planetTexture"), 0);
        glUniform1i(glGetUniformLocation(planetProg, "useTexture"), ringTexValid ? 1 : 0);

        glm::mat4 ringModel = glm::translate(glm::mat4(1.0f), b.worldPos);
        ringModel = glm::rotate(ringModel, glm::radians(25.0f), glm::vec3(0, 0, 1));
        glUniform1i(glGetUniformLocation(planetProg, "planetType"), 7);
        glUniform3f(glGetUniformLocation(planetProg, "objectColor"), 0.9f, 0.8f, 0.6f);
        glUniform1i(glGetUniformLocation(planetProg, "isSun"), 0);
        setMVP(planetProg, ringModel, view, proj);

        glBindVertexArray(b.ringVAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)b.ringInds.size(), GL_UNSIGNED_INT, 0);

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }
}

// -----------------------------------------------------------
//  HUD
// -----------------------------------------------------------
void printHUD() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    const char* modeNames[] = { "Free", "Orbit", "Follow", "Top" };
    std::cout << "|----------------------------------------------------------|\n";
    std::cout << "|       CGD6214 - Interactive Solar System Simulator       |\n";
    std::cout << "|----------------------------------------------------------|\n";
    std::cout << "|  W/A/S/D          : Move camera (Free mode)             |\n";
    std::cout << "|  Right-click+drag : Mouse look                          |\n";
    std::cout << "|  Scroll / +/-     : Zoom (FOV)                          |\n";
    std::cout << "|  Arrow UP/DOWN    : Fly up / down                       |\n";
    std::cout << "|  SPACE            : Pause / Resume                      |\n";
    std::cout << "|  O                : Toggle orbit lines                  |\n";
    std::cout << "|  R / F            : Speed up / slow down time           |\n";
    std::cout << "|  1-6              : Select planet (Mercury->Saturn)     |\n";
    std::cout << "|  F key            : Follow selected planet              |\n";
    std::cout << "|  V                : Cycle camera mode                   |\n";
    std::cout << "|  M                : Toggle wireframe                    |\n";
    std::cout << "|  L                : Toggle lighting                     |\n";
    std::cout << "|  I                : Toggle info panel                   |\n";
    std::cout << "|  B                : Spawn meteor shower                 |\n";
    std::cout << "|  C                : Reset camera                        |\n";
    std::cout << "|  ESC              : Quit                                |\n";
    std::cout << "|----------------------------------------------------------|\n";
    if (showInfo) {
        std::cout << "|  Status   : " << (paused ? "PAUSED " : "RUNNING")
            << "   TimeScale: " << std::fixed << std::setprecision(2) << timeScale << "x\n";
        std::cout << "|  FOV      : " << (int)fov << " deg\n";
        std::cout << "|  Camera   : (" << std::setprecision(1)
            << camPos.x << ", " << camPos.y << ", " << camPos.z << ")\n";
        std::cout << "|  Cam Mode : " << modeNames[camMode] << "\n";
        std::cout << "|  Selected : " << bodies[selectedPlanet].name << "\n";
        std::cout << "|  Lighting : " << (lightingOn ? "ON" : "OFF") << "\n";
        std::cout << "|  Wireframe: " << (wireframe ? "ON" : "OFF") << "\n";
        std::cout << "|  Meteors  : " << (meteorShowerActive ? "ACTIVE" : "idle") << "\n";
    }
    std::cout << "|----------------------------------------------------------|\n";
}

// -----------------------------------------------------------
//  INPUT CALLBACKS
// -----------------------------------------------------------
void keyCallback(GLFWwindow* win, int key, int /*sc*/, int action, int /*mods*/) {
    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, true); break;
        case GLFW_KEY_SPACE:  paused = !paused; break;
        case GLFW_KEY_O:      showOrbits = !showOrbits; break;
        case GLFW_KEY_I:      showInfo = !showInfo;   break;

            // wireframe
        case GLFW_KEY_M:
            wireframe = !wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            break;

            // lighting
        case GLFW_KEY_L:
            lightingOn = !lightingOn;
            break;

            // cycle camera mode
        case GLFW_KEY_V:
            camMode = (CameraMode)((camMode + 1) % 4);
            if (camMode == CAM_TOP) camUp = glm::vec3(0, 0, -1);
            else                    camUp = glm::vec3(0, 1, 0);
            break;

            // follow mode shortcut
        case GLFW_KEY_F:
            camMode = CAM_FOLLOW;
            camUp = glm::vec3(0, 1, 0);
            break;

            // reset camera
        case GLFW_KEY_C:
            camPos = glm::vec3(0.0f, 12.0f, 28.0f);
            camFront = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
            camYaw = -90.0f;
            camPitch = -18.0f;
            camUp = glm::vec3(0, 1, 0);
            camMode = CAM_FREE;
            fov = 45.0f;
            orbitAngleH = 0.0f;
            orbitAngleV = 30.0f;
            orbitDist = 20.0f;
            break;

            // planet selection: 1=Mercury,2=Venus,3=Earth,4=Moon,5=Mars,6=Saturn
        case GLFW_KEY_1: selectedPlanet = 1; break;
        case GLFW_KEY_2: selectedPlanet = 2; break;
        case GLFW_KEY_3: selectedPlanet = 3; break;
        case GLFW_KEY_4: selectedPlanet = 4; break;
        case GLFW_KEY_5: selectedPlanet = 5; break;
        case GLFW_KEY_6: selectedPlanet = 6; break;

            // meteor shower
        case GLFW_KEY_B:
            spawnMeteorShower();
            break;

            // time speed
        case GLFW_KEY_R: timeScale = glm::min(timeScale * 1.5f, 20.0f); break;
            // Note: F is taken by Follow, use G to slow down
        case GLFW_KEY_G: timeScale = glm::max(timeScale / 1.5f, 0.05f); break;

            // zoom
        case GLFW_KEY_KP_ADD:
        case GLFW_KEY_EQUAL: fov = glm::max(fov - 2.0f, 10.0f);  break;
        case GLFW_KEY_KP_SUBTRACT:
        case GLFW_KEY_MINUS: fov = glm::min(fov + 2.0f, 120.0f); break;

        case GLFW_KEY_UP:   camPos += glm::vec3(0, camSpeed * 0.4f, 0); break;
        case GLFW_KEY_DOWN: camPos -= glm::vec3(0, camSpeed * 0.4f, 0); break;
        }
    }

    // Hold R / G for continuous time change
    if (action == GLFW_REPEAT) {
        if (key == GLFW_KEY_R) timeScale = glm::min(timeScale * 1.05f, 20.0f);
        if (key == GLFW_KEY_G) timeScale = glm::max(timeScale / 1.05f, 0.05f);
    }
}

void processMovement(GLFWwindow* win, float dt) {
    if (camMode != CAM_FREE) return;   // only move manually in free mode
    float spd = camSpeed * dt;
    glm::vec3 right = glm::normalize(glm::cross(camFront, camUp));
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camPos += spd * camFront;
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camPos -= spd * camFront;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camPos -= spd * right;
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camPos += spd * right;
}

void mouseButtonCallback(GLFWwindow* win, int btn, int action, int /*mods*/) {
    if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
        mouseLook = (action == GLFW_PRESS);
        firstMouse = true;
        glfwSetInputMode(win, GLFW_CURSOR,
            mouseLook ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

void cursorCallback(GLFWwindow* /*win*/, double xpos, double ypos) {
    if (!mouseLook) return;
    float x = (float)xpos, y = (float)ypos;
    if (firstMouse) { lastX = x; lastY = y; firstMouse = false; }
    float dx = (x - lastX) * 0.1f;
    float dy = (lastY - y) * 0.1f;
    lastX = x; lastY = y;

    if (camMode == CAM_ORBIT) {
        orbitAngleH -= dx * 2.0f;
        orbitAngleV = glm::clamp(orbitAngleV + dy * 2.0f, -89.0f, 89.0f);
        return;
    }

    camYaw += dx;
    camPitch = glm::clamp(camPitch + dy, -89.0f, 89.0f);
    glm::vec3 dir;
    dir.x = cos(glm::radians(camYaw)) * cos(glm::radians(camPitch));
    dir.y = sin(glm::radians(camPitch));
    dir.z = sin(glm::radians(camYaw)) * cos(glm::radians(camPitch));
    camFront = glm::normalize(dir);
}

void scrollCallback(GLFWwindow* /*win*/, double /*xoff*/, double yoff) {
    if (camMode == CAM_ORBIT)
        orbitDist = glm::clamp(orbitDist - (float)yoff * 0.5f, 2.0f, 60.0f);
    else {
        fov = glm::clamp(fov - (float)yoff * 2.0f, 10.0f, 120.0f);
    }
}

void framebufferSizeCallback(GLFWwindow* /*win*/, int w, int h) {
    WIN_W = w; WIN_H = h;
    glViewport(0, 0, w, h);
}

// -----------------------------------------------------------
//  MAIN
// -----------------------------------------------------------
int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H,
        "CGD6214 - Interactive Solar System Simulator", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return -1; }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glLineWidth(1.2f);

    // Compile shaders
    planetProg = makeProgram(planetVertSrc, planetFragSrc);
    orbitProg = makeProgram(orbitVertSrc, orbitFragSrc);
    starProg = makeProgram(starVertSrc, starFragSrc);
    meteorProg = makeProgram(meteorVertSrc, meteorFragSrc);

    // Load textures
    stbi_set_flip_vertically_on_load(false);
    sunTex = loadTexture("textures/sun.jpg");
    mercuryTex = loadTexture("textures/mercury.jpg");
    venusTex = loadTexture("textures/venus.jpg");
    earthTex = loadTexture("textures/earth.jpg");
    moonTex = loadTexture("textures/moon.jpg");
    marsTex = loadTexture("textures/mars.jpg");
    saturnTex = loadTexture("textures/saturn.jpg");
    ringTex = loadTexture("textures/saturn_ring.png");

    // Build scene
    initSolarSystem();
    buildStarfield();
    initMeteorBuffers();
    initSkybox();   // loads cubemap — no crash even if files are missing

    float lastTime = 0.0f;
    int   hudTimer = 0;

    while (!glfwWindowShouldClose(window)) {
        float curTime = (float)glfwGetTime();
        float dt = glm::min(curTime - lastTime, 0.05f);   // cap dt
        lastTime = curTime;

        glfwPollEvents();
        processMovement(window, dt);
        updateBodies(dt);
        updateCamera(dt);
        if (meteorShowerActive) updateMeteors(dt);

        // Print HUD to console every ~60 frames
        if (++hudTimer > 60) { hudTimer = 0; printHUD(); }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
        glm::mat4 proj = glm::perspective(glm::radians(fov),
            (float)WIN_W / (float)WIN_H, 0.05f, 300.0f);

        // 1. Skybox (behind everything)
        drawSkybox(view, proj);

        // 2. Stars (point cloud)
        drawStars(view, proj);

        // 3. Orbit lines
        for (auto& b : bodies) drawOrbitLine(b, view, proj);

        // 4. Planets
        for (auto& b : bodies) drawBody(b, view, proj);

        // 5. Meteors
        drawMeteors(view, proj);

        glfwSwapBuffers(window);
    }

    // Cleanup
    for (auto& b : bodies) {
        glDeleteVertexArrays(1, &b.VAO);
        glDeleteBuffers(1, &b.VBO);
        glDeleteBuffers(1, &b.EBO);
        if (b.orbitVAO) { glDeleteVertexArrays(1, &b.orbitVAO); glDeleteBuffers(1, &b.orbitVBO); }
        if (b.ringVAO) { glDeleteVertexArrays(1, &b.ringVAO);  glDeleteBuffers(1, &b.ringVBO); glDeleteBuffers(1, &b.ringEBO); }
    }
    glDeleteVertexArrays(1, &starVAO);    glDeleteBuffers(1, &starVBO);
    glDeleteVertexArrays(1, &meteorVAO);  glDeleteBuffers(1, &meteorVBO);
    glDeleteVertexArrays(1, &skyboxVAO);  glDeleteBuffers(1, &skyboxVBO);
    glDeleteTextures(1, &skyboxCubemap);
    glDeleteProgram(planetProg);
    glDeleteProgram(orbitProg);
    glDeleteProgram(starProg);
    glDeleteProgram(meteorProg);
    glDeleteProgram(skyboxProg);
    glfwTerminate();
    return 0;
}