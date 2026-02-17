#include "vkinternal.h"
#include <unordered_map>

// --- Shaders ---

ShaderBuilder::ShaderBuilder() : stage(VK_SHADER_STAGE_FRAGMENT_BIT) {}
ShaderBuilder& ShaderBuilder::fragment() { stage = VK_SHADER_STAGE_FRAGMENT_BIT; return *this; }
ShaderBuilder& ShaderBuilder::compute() { stage = VK_SHADER_STAGE_COMPUTE_BIT; return *this; }
ShaderBuilder& ShaderBuilder::mesh() { stage = VK_SHADER_STAGE_MESH_BIT_EXT; return *this; }

ShaderBuilder& ShaderBuilder::fromFile(const char * name) {
    fileName = name;
    std::ifstream file(name, std::ios::ate|std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open shader file");
    size_t fileSize = (size_t)file.tellg();
    file.seekg(0);
    code.resize(fileSize);
    file.read((char*)&code[0], fileSize);
    file.close();
    return *this;
}

ShaderBuilder& ShaderBuilder::fromBuffer(const uint8_t * data, size_t size) {
    code.clear();
    code.insert(code.end(), data, data + size);
    return *this;
}

// --- SPIR-V reflection ---

static uint32_t spirvTypeSize(uint32_t typeId,
    const std::unordered_map<uint32_t, uint32_t> & scalarWidths,
    const std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> & vectorTypes,
    const std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> & matrixTypes)
{
    if (scalarWidths.count(typeId)) return scalarWidths.at(typeId) / 8;
    if (vectorTypes.count(typeId)) {
        auto [comp, count] = vectorTypes.at(typeId);
        return spirvTypeSize(comp, scalarWidths, vectorTypes, matrixTypes) * count;
    }
    if (matrixTypes.count(typeId)) {
        auto [col, count] = matrixTypes.at(typeId);
        return spirvTypeSize(col, scalarWidths, vectorTypes, matrixTypes) * count;
    }
    return 4; // fallback
}

static VkShaderStageFlagBits executionModelToStage(uint32_t model) {
    switch (model) {
        case 4:    return VK_SHADER_STAGE_FRAGMENT_BIT;
        case 5:    return VK_SHADER_STAGE_COMPUTE_BIT;
        case 5267: return VK_SHADER_STAGE_TASK_BIT_EXT;  // TaskNV
        case 5268: return VK_SHADER_STAGE_MESH_BIT_EXT;  // MeshNV
        case 5364: return VK_SHADER_STAGE_TASK_BIT_EXT;  // TaskEXT
        case 5365: return VK_SHADER_STAGE_MESH_BIT_EXT;  // MeshEXT
        default:   return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
    }
}

static ShaderReflection parseSpirv(const std::vector<uint8_t> & code) {
    ShaderReflection r;
    if (code.size() < 20) return r;

    const uint32_t * words = (const uint32_t *)code.data();
    size_t wordCount = code.size() / 4;

    if (words[0] != 0x07230203) return r; // bad magic

    // maps for type resolution
    std::unordered_map<uint32_t, uint32_t> scalarWidths;       // id -> bit width
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> vectorTypes; // id -> (compId, count)
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> matrixTypes; // id -> (colId, count)
    std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers;       // id -> member type ids
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> pointerTypes; // id -> (storageClass, typeId)

    // variable info
    struct VarInfo { uint32_t typeId; uint32_t storageClass; };
    std::unordered_map<uint32_t, VarInfo> variables;

    // decoration info
    std::unordered_map<uint32_t, uint32_t> locationDecos;     // id -> location
    std::unordered_map<uint32_t, uint32_t> bindingDecos;      // id -> binding
    std::unordered_map<uint32_t, uint32_t> descriptorSetDecos; // id -> set
    std::set<uint32_t> builtinIds;

    // member offsets: structId -> (member -> offset)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> memberOffsets;

    size_t pos = 5;
    while (pos < wordCount) {
        uint32_t wc = words[pos] >> 16;
        uint32_t op = words[pos] & 0xffff;
        if (wc == 0) break;

        switch (op) {
        case 15: { // OpEntryPoint
            uint32_t model = words[pos + 1];
            r.executionModel = executionModelToStage(model);
            break;
        }
        case 16: { // OpExecutionMode
            uint32_t mode = words[pos + 2];
            if (mode == 17 && wc >= 6) { // LocalSize
                r.localSize = { words[pos + 3], words[pos + 4], words[pos + 5] };
            } else if (mode == 26 && wc >= 4) { // OutputVertices
                r.maxVertices = words[pos + 3];
            } else if (mode == 5270 && wc >= 4) { // OutputPrimitivesEXT
                r.maxPrimitives = words[pos + 3];
            }
            break;
        }
        case 21: // OpTypeInt
        case 22: // OpTypeFloat
            scalarWidths[words[pos + 1]] = words[pos + 2];
            break;
        case 23: // OpTypeVector
            vectorTypes[words[pos + 1]] = { words[pos + 2], words[pos + 3] };
            break;
        case 24: // OpTypeMatrix
            matrixTypes[words[pos + 1]] = { words[pos + 2], words[pos + 3] };
            break;
        case 30: { // OpTypeStruct
            std::vector<uint32_t> members;
            for (uint32_t i = 2; i < wc; ++i) members.push_back(words[pos + i]);
            structMembers[words[pos + 1]] = members;
            break;
        }
        case 32: // OpTypePointer
            pointerTypes[words[pos + 1]] = { words[pos + 2], words[pos + 3] };
            break;
        case 59: // OpVariable
            variables[words[pos + 2]] = { words[pos + 1], words[pos + 3] };
            break;
        case 71: { // OpDecorate
            uint32_t target = words[pos + 1];
            uint32_t deco = words[pos + 2];
            if (deco == 11) builtinIds.insert(target);          // BuiltIn
            else if (deco == 30 && wc >= 4) locationDecos[target] = words[pos + 3]; // Location
            else if (deco == 33 && wc >= 4) bindingDecos[target] = words[pos + 3];  // Binding
            else if (deco == 34 && wc >= 4) descriptorSetDecos[target] = words[pos + 3]; // DescriptorSet
            break;
        }
        case 72: { // OpMemberDecorate
            uint32_t structId = words[pos + 1];
            uint32_t member = words[pos + 2];
            uint32_t deco = words[pos + 3];
            if (deco == 35 && wc >= 5) { // Offset
                memberOffsets[structId][member] = words[pos + 4];
            }
            break;
        }
        }
        pos += wc;
    }

    // find push constant struct and compute size
    for (auto & [varId, var] : variables) {
        if (var.storageClass != 9) continue; // PushConstant = 9
        uint32_t ptrType = var.typeId;
        if (!pointerTypes.count(ptrType)) continue;
        uint32_t structId = pointerTypes[ptrType].second;
        if (!structMembers.count(structId)) continue;
        auto & members = structMembers[structId];
        auto & offsets = memberOffsets[structId];
        uint32_t maxEnd = 0;
        for (uint32_t i = 0; i < members.size(); ++i) {
            uint32_t off = offsets.count(i) ? offsets[i] : 0;
            uint32_t sz = spirvTypeSize(members[i], scalarWidths, vectorTypes, matrixTypes);
            if (off + sz > maxEnd) maxEnd = off + sz;
        }
        r.pushConstantSize = maxEnd;
        break;
    }

    // collect descriptor bindings and locations
    for (auto & [varId, var] : variables) {
        if (builtinIds.count(varId)) continue;
        if (descriptorSetDecos.count(varId) && bindingDecos.count(varId)) {
            r.descriptorBindings.insert({ descriptorSetDecos[varId], bindingDecos[varId] });
        }
        if (locationDecos.count(varId)) {
            if (var.storageClass == 1) r.inputLocations.insert(locationDecos[varId]);   // Input
            if (var.storageClass == 3) r.outputLocations.insert(locationDecos[varId]);   // Output
        }
    }

    return r;
}

ShaderModule::ShaderModule(ShaderBuilder & builder) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = builder.code.size();
    createInfo.pCode = (uint32_t*)builder.code.data();
    if (VK_SUCCESS != vkCreateShaderModule(g_context().device, &createInfo, nullptr, &module)) {
        throw std::runtime_error("failed to create shader module");
    }
    fileName = builder.fileName;
    reflection = parseSpirv(builder.code);
}
ShaderModule::~ShaderModule() { vkDestroyShaderModule(g_context().device, module, nullptr); }
ShaderModule::operator VkShaderModule() const { return module; }
