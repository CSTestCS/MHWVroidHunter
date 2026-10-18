#include "vrm.h"

#include "fsutil.h"

#include <cgltf.h>
#include <stb_image.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace vh {

namespace {

using json = nlohmann::json;

struct CgltfDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

Mat4 toMat4(const float* m) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = m[i];
    return r;
}

// VRM 0.x thumb names differ from VRM 1.0; everything else matches.
std::string normalizeHumanName(const std::string& n) {
    static const std::map<std::string, std::string> thumbs = {
        {"leftThumbProximal", "leftThumbMetacarpal"},   {"leftThumbIntermediate", "leftThumbProximal"},
        {"rightThumbProximal", "rightThumbMetacarpal"}, {"rightThumbIntermediate", "rightThumbProximal"},
    };
    auto it = thumbs.find(n);
    return it == thumbs.end() ? n : it->second;
}

void readHumanoid(const cgltf_data* d, VrmModel& m) {
    for (cgltf_size i = 0; i < d->data_extensions_count; ++i) {
        const cgltf_extension& e = d->data_extensions[i];
        std::string name = e.name ? e.name : "";
        if (name != "VRM" && name != "VRMC_vrm") continue;
        json j = json::parse(e.data ? e.data : "{}", nullptr, false);
        if (j.is_discarded()) continue;
        if (name == "VRMC_vrm") {
            m.vrmVersion = 1;
            const json& hb = j["humanoid"]["humanBones"];
            if (hb.is_object())
                for (auto it = hb.begin(); it != hb.end(); ++it)
                    if (it.value().contains("node")) m.humanBones[it.key()] = it.value()["node"].get<int>();
            if (j.contains("meta") && j["meta"].contains("name") && j["meta"]["name"].is_string())
                m.title = j["meta"]["name"].get<std::string>();
        } else {
            m.vrmVersion = 0;
            const json& hb = j["humanoid"]["humanBones"];
            if (hb.is_array())
                for (const auto& b : hb)
                    if (b.contains("bone") && b.contains("node") && b["node"].is_number_integer() && b["node"].get<int>() >= 0)
                        m.humanBones[normalizeHumanName(b["bone"].get<std::string>())] = b["node"].get<int>();
            if (j.contains("meta") && j["meta"].contains("title") && j["meta"]["title"].is_string())
                m.title = j["meta"]["title"].get<std::string>();
        }
        return;
    }
}

Image decodeImage(const cgltf_image* img, const std::string& baseDir) {
    Image out;
    int w = 0, h = 0, n = 0;
    stbi_uc* px = nullptr;
    if (img->buffer_view) {
        const uint8_t* data = cgltf_buffer_view_data(img->buffer_view);
        if (data) px = stbi_load_from_memory(data, int(img->buffer_view->size), &w, &h, &n, 4);
    } else if (img->uri && std::string(img->uri).rfind("data:", 0) != 0) {
        px = stbi_load((baseDir + img->uri).c_str(), &w, &h, &n, 4);
    }
    if (!px) return Image::solid(4, 4, 255, 255, 255, 255);
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    return out;
}

} // namespace

VrmModel loadVrm(const std::string& path) {
    cgltf_options opts{};
    cgltf_data* raw = nullptr;
    // Read through our UTF-8 aware helper (cgltf/fopen cannot open non-ASCII paths on Windows).
    std::vector<uint8_t> fileData = readFile(path);
    if (cgltf_parse(&opts, fileData.data(), fileData.size(), &raw) != cgltf_result_success)
        throw std::runtime_error("Could not read the file as a VRM/glTF model: " + path);
    std::unique_ptr<cgltf_data, CgltfDeleter> data(raw);
    if (cgltf_load_buffers(&opts, raw, path.c_str()) != cgltf_result_success)
        throw std::runtime_error("The VRM file is damaged (buffers could not be loaded).");

    VrmModel m;
    std::string baseDir;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) baseDir = path.substr(0, slash + 1);

    readHumanoid(raw, m);
    if (m.humanBones.empty()) throw std::runtime_error("This file has no VRM humanoid information. Please export it as .vrm from VRoid Studio.");
    for (const char* req : {"hips", "spine", "head", "leftUpperArm", "rightUpperArm", "leftLowerArm", "rightLowerArm",
                            "leftHand", "rightHand", "leftUpperLeg", "rightUpperLeg", "leftLowerLeg", "rightLowerLeg",
                            "leftFoot", "rightFoot"})
        if (!m.humanBones.count(req)) throw std::runtime_error(std::string("The VRM humanoid is missing the required bone: ") + req);

    // nodes
    m.nodes.resize(raw->nodes_count);
    for (cgltf_size i = 0; i < raw->nodes_count; ++i) {
        const cgltf_node* n = &raw->nodes[i];
        VrmNode& vn = m.nodes[i];
        vn.name = n->name ? n->name : ("node" + std::to_string(i));
        vn.parent = n->parent ? int(n->parent - raw->nodes) : -1;
        float w[16];
        cgltf_node_transform_world(n, w);
        vn.world = toMat4(w);
    }

    // images and materials
    for (cgltf_size i = 0; i < raw->images_count; ++i) m.images.push_back(decodeImage(&raw->images[i], baseDir));
    for (cgltf_size i = 0; i < raw->materials_count; ++i) {
        const cgltf_material& cm = raw->materials[i];
        VrmMaterial vm;
        vm.name = cm.name ? cm.name : ("material" + std::to_string(i));
        if (cm.has_pbr_metallic_roughness) {
            const auto& pbr = cm.pbr_metallic_roughness;
            vm.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                vm.image = int(pbr.base_color_texture.texture->image - raw->images);
        }
        vm.alphaMode = cm.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                       : cm.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                 : AlphaMode::Opaque;
        vm.alphaCutoff = cm.alpha_cutoff;
        vm.doubleSided = cm.double_sided;
        m.materials.push_back(vm);
    }
    int defaultMaterial = -1;

    // meshes
    for (cgltf_size ni = 0; ni < raw->nodes_count; ++ni) {
        const cgltf_node* node = &raw->nodes[ni];
        if (!node->mesh) continue;
        const cgltf_skin* skin = node->skin;
        std::vector<Mat4> skinMats;
        if (skin) {
            for (cgltf_size j = 0; j < skin->joints_count; ++j) {
                Mat4 ibm;
                if (skin->inverse_bind_matrices) {
                    float f[16];
                    cgltf_accessor_read_float(skin->inverse_bind_matrices, j, f, 16);
                    ibm = toMat4(f);
                }
                skinMats.push_back(m.nodes[size_t(skin->joints[j] - raw->nodes)].world * ibm);
            }
        }
        const Mat4& nodeWorld = m.nodes[ni].world;
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& p = node->mesh->primitives[pi];
            if (p.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tan = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
            for (cgltf_size a = 0; a < p.attributes_count; ++a) {
                const cgltf_attribute& at = p.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_tangent) tan = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
            }
            if (!pos) continue;
            VrmPrimitive prim;
            if (p.material) {
                prim.material = int(p.material - raw->materials);
            } else {
                if (defaultMaterial < 0) {
                    defaultMaterial = int(m.materials.size());
                    m.materials.push_back(VrmMaterial{"default", -1, {1, 1, 1, 1}, AlphaMode::Opaque, 0.5f, false});
                }
                prim.material = defaultMaterial;
            }
            const cgltf_size vc = pos->count;
            prim.vertices.resize(vc);
            for (cgltf_size v = 0; v < vc; ++v) {
                VrmVertex& out = prim.vertices[v];
                float tmp[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(pos, v, tmp, 3);
                Vec3 lp{tmp[0], tmp[1], tmp[2]};
                Vec3 ln{0, 1, 0};
                Vec4 lt{1, 0, 0, 1};
                if (nrm) { cgltf_accessor_read_float(nrm, v, tmp, 3); ln = {tmp[0], tmp[1], tmp[2]}; }
                if (tan) { cgltf_accessor_read_float(tan, v, tmp, 4); lt = {tmp[0], tmp[1], tmp[2], tmp[3] < 0 ? -1.0f : 1.0f}; }
                if (uv) { cgltf_accessor_read_float(uv, v, tmp, 2); out.uv = {tmp[0], tmp[1]}; }
                if (skin && jnt && wgt) {
                    cgltf_uint ji[4] = {0, 0, 0, 0};
                    float wi[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jnt, v, ji, 4);
                    cgltf_accessor_read_float(wgt, v, wi, 4);
                    Mat4 acc;
                    for (int k = 0; k < 16; ++k) acc.m[k] = 0;
                    float wsum = 0;
                    for (int k = 0; k < 4; ++k) {
                        if (wi[k] <= 0 || ji[k] >= skin->joints_count) continue;
                        const Mat4& sm = skinMats[ji[k]];
                        for (int e = 0; e < 16; ++e) acc.m[e] += sm.m[e] * wi[k];
                        out.joints[k] = int(skin->joints[ji[k]] - raw->nodes);
                        out.weights[k] = wi[k];
                        wsum += wi[k];
                    }
                    if (wsum <= 0) {
                        acc = nodeWorld;
                        out.joints[0] = int(ni);
                        out.weights[0] = 1;
                    } else {
                        for (int e = 0; e < 16; ++e) acc.m[e] /= wsum;
                        for (float& w : out.weights) w /= wsum;
                    }
                    out.pos = acc.transformPoint(lp);
                    Mat3 nm = acc.linear().inverse().transposed();
                    out.normal = normalize(nm * ln);
                    Vec3 t = normalize(acc.linear() * lt.xyz(), {1, 0, 0});
                    out.tangent = {t.x, t.y, t.z, lt.w};
                } else {
                    out.pos = nodeWorld.transformPoint(lp);
                    Mat3 nm = nodeWorld.linear().inverse().transposed();
                    out.normal = normalize(nm * ln);
                    Vec3 t = normalize(nodeWorld.linear() * lt.xyz(), {1, 0, 0});
                    out.tangent = {t.x, t.y, t.z, lt.w};
                    out.joints[0] = int(ni); // rigidly attached to its own node
                    out.weights[0] = 1;
                }
            }
            if (p.indices) {
                prim.indices.resize(p.indices->count);
                for (cgltf_size k = 0; k < p.indices->count; ++k) prim.indices[k] = uint32_t(cgltf_accessor_read_index(p.indices, k));
            } else {
                prim.indices.resize(vc);
                for (cgltf_size k = 0; k < vc; ++k) prim.indices[k] = uint32_t(k);
            }
            // node with a mirroring transform flips winding
            if (!skin && nodeWorld.linear().det() < 0)
                for (size_t k = 0; k + 2 < prim.indices.size(); k += 3) std::swap(prim.indices[k + 1], prim.indices[k + 2]);
            m.primitives.push_back(std::move(prim));
        }
    }
    if (m.primitives.empty()) throw std::runtime_error("The VRM file contains no meshes.");
    return m;
}

} // namespace vh
