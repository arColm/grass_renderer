#include "./vk_loader.hpp"
#include "stb_image.h"
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include "./vk_engine.hpp"
#include "./vk_initializers.hpp"
#include "./vk_types.hpp"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/core.hpp>

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath)
{
	std::cout << "loading GLTF: " << filePath << '\n';

	fastgltf::Parser parser;
	auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
	if (!data)
	{
		fmt::print("failed to laod file");
		return {};
	}
		
	

	constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;

	fastgltf::Asset gltf;
	auto load = parser.loadGltfBinary(data.get(), filePath.parent_path(), gltfOptions);
	if (load)
	{
		gltf = std::move(load.get());
	}
	else
	{
		fmt::print("failed to load GLTF: {}\n", fastgltf::to_underlying(load.error()));
		return {};
	}

	std::vector<std::shared_ptr<MeshAsset>> meshes;

	//use the same vertices for all meshes so memory doesnt reallocate as often
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;
	for (fastgltf::Mesh& mesh : gltf.meshes)
	{
		MeshAsset newmesh;

		newmesh.name = mesh.name;

		//clear mesh arrays each mesh, we dont want to merge by error
		indices.clear();
		vertices.clear();

		//iterate through primitives in mesh
		for (auto&& p : mesh.primitives)
		{
			GeoSurface newSurface;
			newSurface.startIndex = (uint32_t)indices.size();
			newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

			size_t initialVtx = vertices.size();

			//load indices
			{
				fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
				indices.reserve(indices.size() + indexAccessor.count);
				
				fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, 
					[&](std::uint32_t idx) {
						indices.push_back(idx + initialVtx);
					});
			}

			//load vertex positions
			{
				fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
				vertices.resize(vertices.size() + posAccessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, 
					[&](glm::vec3 v, size_t index) {
						Vertex newvtx;
						newvtx.position = v;
						newvtx.normal = { 1,0,0 };
						newvtx.color = glm::vec4{ 1.f };
						newvtx.uv_x = 0;
						newvtx.uv_y = 0;
						vertices[initialVtx + index] = newvtx;
					});
			}

			//load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).accessorIndex],
					[&](glm::vec3 v, size_t index) {
						vertices[initialVtx + index].normal = v;
					});
			}

			//load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).accessorIndex],
					[&](glm::vec2 v, size_t index) {
						vertices[initialVtx + index].uv_x = v.x;
						vertices[initialVtx + index].uv_y = v.y;
					});
			}

			//load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*colors).accessorIndex],
					[&](glm::vec3 v, size_t index) {
						vertices[initialVtx + index].color = glm::vec4(v,1.f);
					});
			}

			newmesh.surfaces.push_back(newSurface);
		}

		// display the vertex normals
		constexpr bool OVERRIDE_COLORS = false;
		if (OVERRIDE_COLORS)
		{
			for (Vertex& v : vertices)
			{
				v.color = glm::vec4(v.normal, 0.5f);
			}
		}
		newmesh.meshBuffers = engine->uploadMesh(indices, vertices);
		meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
	}

	return meshes;
}
