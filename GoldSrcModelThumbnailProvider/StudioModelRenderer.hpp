#pragma once

#include <Windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>

using Microsoft::WRL::ComPtr;

using DirectX::XMVECTOR;
using DirectX::XMMATRIX;
using DirectX::XMVectorSet;
using DirectX::XMMatrixIdentity;
using DirectX::XMMatrixSet;
using DirectX::XMMatrixLookAtLH;
using DirectX::XMMatrixLookAtRH;
using DirectX::XMMatrixPerspectiveFovLH;
using DirectX::XMMatrixPerspectiveFovRH;
using DirectX::XMMatrixRotationX;
using DirectX::XMMatrixRotationY;
using DirectX::XMMatrixRotationZ;
using DirectX::XMMatrixScaling;


#include "./hlsdk/mathlib.h"
#include "./hlsdk/studio.h"

#include "Shaders.hpp"


class StudioModel
{
public:

	struct VEC2
	{
		float x;
		float y;

		bool operator==(const VEC2& v) const
		{
			return (v.x == this->x) && (v.y == this->y);
		}
	};


	struct VEC3
	{
		float x;
		float y;
		float z;

		bool operator==(const VEC3& v) const
		{
			return (v.x == this->x) && (v.y == this->y) && (v.z == this->z);
		}
	};


	struct Vertex
	{
		VEC3 Position;
		VEC3 Normal;
		VEC2 TexCoord;
		uint32_t Bone;

		bool operator==(const Vertex& v) const
		{
			return (v.Position == this->Position) && (v.Normal == this->Normal) && (v.TexCoord == this->TexCoord);
		}
	};


	struct Mesh
	{
		std::vector<uint32_t> Indices;
		int TextureId;

		Mesh()
			: TextureId{}
		{ }

		Mesh(Mesh&& other) noexcept
		{
			this->Indices = std::move(other.Indices);
			this->TextureId = other.TextureId;
		}
	};


	struct Model
	{
		std::vector<Vertex> Vertices;
		std::vector<Mesh> Meshes;

		Model() = default;

		Model(Model&& other) noexcept
		{
			this->Vertices = std::move(other.Vertices);
			this->Meshes = std::move(other.Meshes);
		}
	};


	struct BodyPart
	{
		std::vector<Model> Models;

		BodyPart() = default;

		BodyPart(BodyPart&& other) noexcept
		{
			this->Models = std::move(other.Models);
		}
	};


	struct Texture
	{
		int Width;
		int Height;
		std::vector<uint8_t> Data;

		Texture()
			: Width{}
			, Height{}
		{ }

		Texture(Texture&& other) noexcept
		{
			this->Width = other.Width;
			this->Height = other.Height;
			this->Data = std::move(other.Data);
		}
	};


private:

	template<typename T>
	inline T* GetPtr(int offset)
	{
		return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(m_StudioHeader) + offset);
	}


	template<typename T>
	inline T* AdjustPtr(void* base, int offset)
	{
		return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(base) + offset);
	}


	static uint32_t InsertVertex(std::vector<Vertex>& vertices, const Vertex& vertex)
	{
		auto it = std::find_if(vertices.cbegin(), vertices.cend(), [&vertex](const auto& elem) {
			return elem == vertex;
		});

		if (it != vertices.cend())
		{
			return static_cast<uint32_t>(it - vertices.cbegin());
		}

		vertices.push_back(vertex);

		return static_cast<uint32_t>(vertices.size() - 1);
	}


	Mesh LoadMesh(mstudiomesh_t* studioMesh, mstudiomodel_t* studioModel, std::vector<Vertex>& vertices)
	{
		Mesh mesh{};
		mesh.Indices.reserve(2048);

		auto studioVertices = GetPtr<VEC3>(studioModel->vertindex);
		auto studioVertexBones = GetPtr<uint8_t>(studioModel->vertinfoindex);
		auto studioNormals = GetPtr<VEC3>(studioModel->normindex);
		auto studioTextures = AdjustPtr<mstudiotexture_t>(m_StudioTextureHeader, m_StudioTextureHeader->textureindex);
		auto studioSkinRef = AdjustPtr<uint16_t>(m_StudioTextureHeader, m_StudioTextureHeader->skinindex);

		mesh.TextureId = studioSkinRef[studioMesh->skinref];

		auto s = 1.0f / static_cast<float>(studioTextures[mesh.TextureId].width);
		auto t = 1.0f / static_cast<float>(studioTextures[mesh.TextureId].height);

		std::vector<uint32_t> indices;
		indices.reserve(2048);

		auto tricmds = GetPtr<int16_t>(studioMesh->triindex);
		int16_t i;

		while (i = *(tricmds++))
		{
			bool strip = true;

			// If the command is negative, it's a triangle fan.
			if (i < 0)
			{
				i = -i;
				strip = false;
			}

			indices.clear();

			for (; i > 0; i--, tricmds += 4)
			{
				Vertex vert{};

				vert.Position = studioVertices[tricmds[0]];
				vert.Normal = studioNormals[tricmds[1]];
				vert.TexCoord = VEC2{ s * tricmds[2] , t * tricmds[3] };
				vert.Bone = studioVertexBones[tricmds[0]];

				indices.push_back(InsertVertex(vertices, vert));
			}

			if (strip)
			{
				for (size_t j = 2; j < indices.size(); j++)
				{
					if (j % 2)
					{
						mesh.Indices.push_back(indices[j - 1]);
						mesh.Indices.push_back(indices[j - 2]);
						mesh.Indices.push_back(indices[j]);
					}
					else
					{
						mesh.Indices.push_back(indices[j - 2]);
						mesh.Indices.push_back(indices[j - 1]);
						mesh.Indices.push_back(indices[j]);
					}
				}
			}
			else
			{
				for (size_t j = 2; j < indices.size(); j++)
				{
					mesh.Indices.push_back(indices[0]);
					mesh.Indices.push_back(indices[j - 1]);
					mesh.Indices.push_back(indices[j]);
				}
			}
		}

		return mesh;
	}


	Model LoadModel(mstudiomodel_t* studioModel)
	{
		Model model{};

		if (studioModel->nummesh > 0)
		{
			model.Meshes.reserve(static_cast<size_t>(studioModel->nummesh));

			for (int i = 0; i < studioModel->nummesh; i++)
			{
				auto studioMesh = GetPtr<mstudiomesh_t>(studioModel->meshindex) + i;
				auto mesh = LoadMesh(studioMesh, studioModel, model.Vertices);
				model.Meshes.push_back(std::move(mesh));
			}
		}

		return model;
	}


	BodyPart LoadBodyPart(mstudiobodyparts_t* studioBodyPart)
	{
		BodyPart bodypart{};

		if (studioBodyPart->nummodels > 0)
		{
			bodypart.Models.reserve(static_cast<size_t>(studioBodyPart->nummodels));

			for (int i = 0; i < studioBodyPart->nummodels; i++)
			{
				auto studioModel = GetPtr<mstudiomodel_t>(studioBodyPart->modelindex) + i;
				auto model = LoadModel(studioModel);
				bodypart.Models.push_back(std::move(model));
			}
		}

		return bodypart;
	}


	Texture LoadTexture(mstudiotexture_t* studioTexture)
	{
		Texture texture{};

		texture.Width = studioTexture->width;
		texture.Height = studioTexture->height;

		auto size = studioTexture->width * studioTexture->height;

		auto indices = AdjustPtr<uint8_t>(m_StudioTextureHeader, studioTexture->index);
		auto palette = indices + size;

		texture.Data.resize(static_cast<size_t>(size * 4));
		auto pixels = texture.Data.data();

		for (int i = 0; i < size; i++)
		{
			auto color_offset = indices[i] * 3;
			auto pixel_offset = i * 4;

			pixels[pixel_offset + 0] = palette[color_offset + 0];
			pixels[pixel_offset + 1] = palette[color_offset + 1];
			pixels[pixel_offset + 2] = palette[color_offset + 2];
			pixels[pixel_offset + 3] = 0xff;
		}

		return texture;
	}


	static std::vector<uint8_t> ReadAllBytes(const std::wstring& filePath)
	{
		std::vector<uint8_t> buffer;

		std::ifstream file(filePath, std::ios::binary);

		if (!file)
			return {};

		file.seekg(0, std::ios::end);
		size_t file_size = file.tellg();
		file.seekg(0, std::ios::beg);

		buffer.resize(file_size);
		file.read(reinterpret_cast<char*>(buffer.data()), file_size);

		if (!file)
			return {};

		return buffer;
	}


	static std::wstring AddSuffixToFileName(const std::wstring& filePath, const std::wstring& suffix)
	{
		std::filesystem::path path(filePath);

		std::wstring fileName = path.stem().wstring();
		std::wstring extension = path.extension().wstring();

		std::wstring newFileName = fileName + suffix + extension;

		return (path.parent_path() / newFileName).wstring();
	}


	static bool VerifyStudioFile(std::vector<uint8_t>& buffer)
	{
		if (buffer.size() < sizeof(studiohdr_t))
			return false;

		auto signature = *reinterpret_cast<int*>(buffer.data());

		if (signature != 0x54534449) // "IDST"
			return false;

		auto version = *reinterpret_cast<int*>(buffer.data() + 4);

		if (version != 10)
			return false;

		return true;
	}


	static bool VerifySequenceStudioFile(std::vector<uint8_t>& buffer)
	{
		if (buffer.size() < sizeof(studioseqhdr_t))
			return false;

		auto signature = *reinterpret_cast<int*>(buffer.data());

		if (signature != 0x51534449) // "IDSQ"
			return false;

		auto version = *reinterpret_cast<int*>(buffer.data() + 4);

		if (version != 10)
			return false;

		return true;
	}


public:

	void LoadFromFile(const std::wstring& filePath)
	{
		m_FileData = ReadAllBytes(filePath);

		if (m_FileData.empty())
			return;

		if (!VerifyStudioFile(m_FileData))
			return;

		m_FilePath = filePath;

		m_StudioHeader = reinterpret_cast<studiohdr_t*>(m_FileData.data());

		m_StudioTextureHeader = m_StudioHeader;

		if (m_StudioHeader->numtextures == 0)
		{
			// "testT.mdl"
			auto externalFileName = AddSuffixToFileName(filePath, L"T");

			m_StudioTextureFileData = ReadAllBytes(externalFileName);

			if (VerifyStudioFile(m_StudioTextureFileData))
			{
				m_StudioTextureHeader = reinterpret_cast<studiohdr_t*>(m_StudioTextureFileData.data());
			}
		}

		if (m_StudioTextureHeader->numtextures > 0)
		{
			m_Textures.reserve(static_cast<size_t>(m_StudioTextureHeader->numtextures));

			for (int i = 0; i < m_StudioTextureHeader->numtextures; i++)
			{
				auto studioTexture = AdjustPtr<mstudiotexture_t>(m_StudioTextureHeader, m_StudioTextureHeader->textureindex) + i;
				auto texture = LoadTexture(studioTexture);
				m_Textures.push_back(std::move(texture));
			}
		}

		if (m_StudioHeader->numbodyparts > 0)
		{
			m_BodyParts.reserve(static_cast<size_t>(m_StudioHeader->numbodyparts));

			for (int i = 0; i < m_StudioHeader->numbodyparts; i++)
			{
				auto studiobodypart = GetPtr<mstudiobodyparts_t>(m_StudioHeader->bodypartindex) + i;
				auto bodypart = LoadBodyPart(studiobodypart);
				m_BodyParts.push_back(std::move(bodypart));
			}
		}

		if (m_StudioHeader->numseqgroups > 1)
		{
			for (int i = 1; i < m_StudioHeader->numseqgroups; i++)
			{
				wchar_t suffix[4];

				// "test01.mdl"
				swprintf_s(suffix, L"%02d", i);

				auto seqGroupFileName = AddSuffixToFileName(filePath, suffix);

				auto buffer = ReadAllBytes(seqGroupFileName);

				if (!VerifySequenceStudioFile(buffer))
					continue;

				m_StudioSequenceGroupFileData[i] = std::move(buffer);
				m_StudioSequenceGroupHeaders[i] = reinterpret_cast<studioseqhdr_t*>(m_StudioSequenceGroupFileData[i].data());
			}
		}
	}


	const std::wstring& GetFilePath() const
	{
		return m_FilePath;
	}


	studiohdr_t* GetStudioHeader() const
	{
		return m_StudioHeader;
	}


	const std::vector<BodyPart>& GetBodyParts() const
	{
		return m_BodyParts;
	}


	const std::vector<Texture>& GetTextures() const
	{
		return m_Textures;
	}


	auto GetSequenceGroupHeaders()
	{
		return (studioseqhdr_t**)m_StudioSequenceGroupHeaders;
	}


	StudioModel()
		: m_StudioHeader{}
		, m_StudioTextureHeader{}
		, m_StudioSequenceGroupHeaders{}
	{
		// TODO
	}


private:

	std::wstring m_FilePath;

	std::vector<uint8_t> m_FileData;
	studiohdr_t* m_StudioHeader;

	std::vector<uint8_t> m_StudioTextureFileData;
	studiohdr_t* m_StudioTextureHeader;

	std::vector<uint8_t> m_StudioSequenceGroupFileData[32];
	studioseqhdr_t* m_StudioSequenceGroupHeaders[32];

	std::vector<BodyPart> m_BodyParts;
	std::vector<Texture> m_Textures;
};


class StudioModelAnimating
{
private:

	void CalcBoneAdj()
	{
		int i, j;
		float value;
		mstudiobonecontroller_t* pbonecontroller;

		pbonecontroller = (mstudiobonecontroller_t*)((byte*)m_StudioHeader + m_StudioHeader->bonecontrollerindex);

		for (j = 0; j < m_StudioHeader->numbonecontrollers; j++)
		{
			i = pbonecontroller[j].index;
			if (i <= 3)
			{
				if (pbonecontroller[j].type & STUDIO_RLOOP)
				{
					value = m_Controllers[i] * (360.0f / 256.0f) + pbonecontroller[j].start;
				}
				else
				{
					value = m_Controllers[i] / 255.0f;
					if (value < 0) value = 0;
					if (value > 1.0f) value = 1.0f;
					value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
				}
			}
			else
			{
				value = m_Mouth / 64.0f;
				if (value > 1.0f) value = 1.0f;
				value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
			switch (pbonecontroller[j].type & STUDIO_TYPES)
			{
				case STUDIO_XR:
				case STUDIO_YR:
				case STUDIO_ZR:
					m_BoneAdjust[j] = (float)(value * (Q_PI / 180.0));
					break;
				case STUDIO_X:
				case STUDIO_Y:
				case STUDIO_Z:
					m_BoneAdjust[j] = value;
					break;
			}
		}
	}


	void CalcBoneQuaternion(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, float* q) const
	{
		int j, k;
		vec4_t q1, q2;
		vec3_t angle1{}, angle2{};
		mstudioanimvalue_t* panimvalue;

		for (j = 0; j < 3; j++)
		{
			if (panim->offset[j + 3] == 0)
			{
				angle2[j] = angle1[j] = pbone->value[j + 3]; // default;
			}
			else
			{
				panimvalue = (mstudioanimvalue_t*)((byte*)panim + panim->offset[j + 3]);
				k = frame;
				while (panimvalue->num.total <= k)
				{
					k -= panimvalue->num.total;
					panimvalue += panimvalue->num.valid + 1;
				}
				// Bah, missing blend!
				if (panimvalue->num.valid > k)
				{
					angle1[j] = panimvalue[k + 1].value;

					if (panimvalue->num.valid > k + 1)
					{
						angle2[j] = panimvalue[k + 2].value;
					}
					else
					{
						if (panimvalue->num.total > k + 1)
							angle2[j] = angle1[j];
						else
							angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
					}
				}
				else
				{
					angle1[j] = panimvalue[panimvalue->num.valid].value;
					if (panimvalue->num.total > k + 1)
						angle2[j] = angle1[j];
					else
						angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
				angle1[j] = pbone->value[j + 3] + angle1[j] * pbone->scale[j + 3];
				angle2[j] = pbone->value[j + 3] + angle2[j] * pbone->scale[j + 3];
			}

			if (pbone->bonecontroller[j + 3] != -1)
			{
				angle1[j] += m_BoneAdjust[pbone->bonecontroller[j + 3]];
				angle2[j] += m_BoneAdjust[pbone->bonecontroller[j + 3]];
			}
		}

		if (!VectorCompare(angle1, angle2))
		{
			AngleQuaternion(angle1, q1);
			AngleQuaternion(angle2, q2);
			QuaternionSlerp(q1, q2, s, q);
		}
		else
		{
			AngleQuaternion(angle1, q);
		}
	}


	void CalcBonePosition(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, float* pos) const
	{
		int j, k;
		mstudioanimvalue_t* panimvalue;

		for (j = 0; j < 3; j++)
		{
			pos[j] = pbone->value[j];

			if (panim->offset[j] != 0)
			{
				panimvalue = (mstudioanimvalue_t*)((byte*)panim + panim->offset[j]);

				k = frame;
				// find span of values that includes the frame we want
				while (panimvalue->num.total <= k)
				{
					k -= panimvalue->num.total;
					panimvalue += panimvalue->num.valid + 1;
				}
				// if we're inside the span
				if (panimvalue->num.valid > k)
				{
					// and there's more data in the span
					if (panimvalue->num.valid > k + 1)
						pos[j] += (panimvalue[k + 1].value * (1.0f - s) + s * panimvalue[k + 2].value) * pbone->scale[j];
					else
						pos[j] += panimvalue[k + 1].value * pbone->scale[j];
				}
				else
				{
					// are we at the end of the repeating values section and there's another section with data?
					if (panimvalue->num.total <= k + 1)
						pos[j] += (panimvalue[panimvalue->num.valid].value * (1.0f - s) + s * panimvalue[panimvalue->num.valid + 2].value) * pbone->scale[j];
					else
						pos[j] += panimvalue[panimvalue->num.valid].value * pbone->scale[j];
				}
			}
			if (pbone->bonecontroller[j] != -1)
			{
				pos[j] += m_BoneAdjust[pbone->bonecontroller[j]];
			}
		}
	}


	void CalcRotations(vec3_t* pos, vec4_t* q, mstudioseqdesc_t* pseqdesc, mstudioanim_t* panim, float f)
	{
		int i;
		int frame;
		mstudiobone_t* pbone;
		float s;

		frame = (int)f;
		s = (f - frame);

		// add in programatic controllers
		CalcBoneAdj();

		pbone = (mstudiobone_t*)((byte*)m_StudioHeader + m_StudioHeader->boneindex);
		for (i = 0; i < m_StudioHeader->numbones; i++, pbone++, panim++)
		{
			CalcBoneQuaternion(frame, s, pbone, panim, q[i]);
			CalcBonePosition(frame, s, pbone, panim, pos[i]);
		}

		if (pseqdesc->motiontype & STUDIO_X)
			pos[pseqdesc->motionbone][0] = 0.0f;
		if (pseqdesc->motiontype & STUDIO_Y)
			pos[pseqdesc->motionbone][1] = 0.0f;
		if (pseqdesc->motiontype & STUDIO_Z)
			pos[pseqdesc->motionbone][2] = 0.0f;
	}


	mstudioanim_t* GetAnim(mstudioseqdesc_t* pseqdesc)
	{
		mstudioseqgroup_t* pseqgroup;
		pseqgroup = (mstudioseqgroup_t*)((byte*)m_StudioHeader + m_StudioHeader->seqgroupindex) + pseqdesc->seqgroup;

		if (pseqdesc->seqgroup == 0)
		{
			return (mstudioanim_t*)((byte*)m_StudioHeader + pseqgroup->data + pseqdesc->animindex);
		}

		if (!m_StudioSequenceGroupHeaders)
			return NULL;

		if (!m_StudioSequenceGroupHeaders[pseqdesc->seqgroup])
			return NULL;

		return (mstudioanim_t*)((byte*)m_StudioSequenceGroupHeaders[pseqdesc->seqgroup] + pseqdesc->animindex);
	}


	void SlerpBones(vec4_t q1[], vec3_t pos1[], vec4_t q2[], vec3_t pos2[], float s)
	{
		int i;
		vec4_t q3;
		float s1;

		if (s < 0) {
			s = 0;
		}
		else if (s > 1.0f) {
			s = 1.0f;
		}

		s1 = 1.0f - s;

		for (i = 0; i < m_StudioHeader->numbones; i++)
		{
			QuaternionSlerp(q1[i], q2[i], s, q3);
			q1[i][0] = q3[0];
			q1[i][1] = q3[1];
			q1[i][2] = q3[2];
			q1[i][3] = q3[3];
			pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s;
			pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s;
			pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s;
		}
	}


public:

	void AdvanceFrame(double dt)
	{
		mstudioseqdesc_t* pseqdesc;
		pseqdesc = (mstudioseqdesc_t*)((byte*)m_StudioHeader + m_StudioHeader->seqindex) + m_Sequence;

		if (dt > 0.1)
			dt = 0.1;
		m_Frame += (float)(dt * pseqdesc->fps);

		if (pseqdesc->numframes <= 1)
		{
			m_Frame = 0;
		}
		else
		{
			// wrap
			m_Frame -= (int)(m_Frame / (pseqdesc->numframes - 1)) * (pseqdesc->numframes - 1);
		}
	}


	void SetUpBones()
	{
		int i;

		mstudiobone_t* pbones;
		mstudioseqdesc_t* pseqdesc;
		mstudioanim_t* panim;

		float bonematrix[3][4];

		if (!m_StudioHeader)
			return;

		if (m_Sequence >= m_StudioHeader->numseq)
			m_Sequence = 0;

		pseqdesc = (mstudioseqdesc_t*)((byte*)m_StudioHeader + m_StudioHeader->seqindex) + m_Sequence;

		panim = GetAnim(pseqdesc);
		if (!panim)
			return;

		CalcRotations(tmp_pos, tmp_q, pseqdesc, panim, m_Frame);

		if (pseqdesc->numblends > 1)
		{
			float s;

			panim += m_StudioHeader->numbones;
			CalcRotations(tmp_pos2, tmp_q2, pseqdesc, panim, m_Frame);
			s = m_Blendings[0] / 255.0f;

			SlerpBones(tmp_q, tmp_pos, tmp_q2, tmp_pos2, s);

			if (pseqdesc->numblends == 4) {
				panim += m_StudioHeader->numbones;
				CalcRotations(tmp_pos3, tmp_q3, pseqdesc, panim, m_Frame);

				panim += m_StudioHeader->numbones;
				CalcRotations(tmp_pos4, tmp_q4, pseqdesc, panim, m_Frame);

				s = m_Blendings[0] / 255.0f;
				SlerpBones(tmp_q3, tmp_pos3, tmp_q4, tmp_pos4, s);

				s = m_Blendings[1] / 255.0f;
				SlerpBones(tmp_q, tmp_pos, tmp_q3, tmp_pos3, s);
			}
		}

		pbones = (mstudiobone_t*)((byte*)m_StudioHeader + m_StudioHeader->boneindex);

		for (i = 0; i < m_StudioHeader->numbones; i++)
		{
			QuaternionMatrix(tmp_q[i], bonematrix);

			bonematrix[0][3] = tmp_pos[i][0];
			bonematrix[1][3] = tmp_pos[i][1];
			bonematrix[2][3] = tmp_pos[i][2];

			if (pbones[i].parent == -1)
				memcpy(m_BoneTransforms[i], bonematrix, sizeof(float) * 12);
			else
				R_ConcatTransforms(m_BoneTransforms[pbones[i].parent], bonematrix, m_BoneTransforms[i]);
		}
	}


	void SetStudioHeader(studiohdr_t* studioHeader)
	{
		m_StudioHeader = studioHeader;
	}


	void SetStudioSequenceGroupHeaders(studioseqhdr_t** studioSequenceGroupHeaders)
	{
		m_StudioSequenceGroupHeaders = studioSequenceGroupHeaders;
	}


	void SetSequence(int seq)
	{
		m_Sequence = seq;
	}


	void SetFrame(float frame)
	{
		m_Frame = frame;
	}


	auto GetBoneTransforms() const
	{
		return m_BoneTransforms;
	}


	StudioModelAnimating()
		: m_StudioHeader{}
		, m_StudioSequenceGroupHeaders{}
		, m_Sequence{}
		, m_Frame{}
		, m_Body{}
		, m_Skin{}
		, m_Controllers{}
		, m_Blendings{}
		, m_Mouth{}
		, m_BoneAdjust{}
		, m_BoneTransforms{}
		, tmp_pos{}
		, tmp_q{}
		, tmp_pos2{}
		, tmp_q2{}
		, tmp_pos3{}
		, tmp_q3{}
		, tmp_pos4{}
		, tmp_q4{}
	{
		// TODO
	}


private:

	studiohdr_t* m_StudioHeader;
	studioseqhdr_t** m_StudioSequenceGroupHeaders;
	int m_Sequence;
	float m_Frame;
	int m_Body;
	int m_Skin;
	byte m_Controllers[4];
	byte m_Blendings[2];
	byte m_Mouth;
	float m_BoneAdjust[4];
	float m_BoneTransforms[MAXSTUDIOBONES][3][4];

	// Big array moved from SetUpBones()
	vec3_t tmp_pos[MAXSTUDIOBONES];
	vec4_t tmp_q[MAXSTUDIOBONES];
	vec3_t tmp_pos2[MAXSTUDIOBONES];
	vec4_t tmp_q2[MAXSTUDIOBONES];
	vec3_t tmp_pos3[MAXSTUDIOBONES];
	vec4_t tmp_q3[MAXSTUDIOBONES];
	vec3_t tmp_pos4[MAXSTUDIOBONES];
	vec4_t tmp_q4[MAXSTUDIOBONES];
};


class D3DStudioModel
{
public:

	struct D3DMesh
	{
		ComPtr<ID3D11Buffer> IndexBuffer;
		UINT NumIndices;
		int TextureId;

		D3DMesh()
			: NumIndices{}
			, TextureId{}
		{}

		D3DMesh(D3DMesh&& other) noexcept
		{
			this->IndexBuffer = std::move(other.IndexBuffer);
			this->NumIndices = other.NumIndices;
			this->TextureId = other.TextureId;
		}
	};


	struct D3DModel
	{
		ComPtr<ID3D11Buffer> VertexBuffer;
		std::vector<D3DMesh> Meshes;

		D3DModel() = default;

		D3DModel(D3DModel&& other) noexcept
		{
			this->VertexBuffer = std::move(other.VertexBuffer);
			this->Meshes = std::move(other.Meshes);
		}
	};


	struct D3DBodyPart
	{
		std::vector<D3DModel> Models;

		D3DBodyPart() = default;

		D3DBodyPart(D3DBodyPart&& other) noexcept
		{
			this->Models = std::move(other.Models);
		}
	};


	struct D3DTexture
	{
		ComPtr<ID3D11Texture2D> Texture;
		ComPtr<ID3D11ShaderResourceView> View;

		D3DTexture() = default;

		D3DTexture(D3DTexture&& other) noexcept
		{
			this->Texture = std::move(other.Texture);
			this->View = std::move(other.View);
		}
	};


private:

	D3DMesh LoadMesh(ID3D11Device* device, const StudioModel::Mesh& studioMesh)
	{
		D3DMesh mesh{};

		mesh.NumIndices = static_cast<UINT>(studioMesh.Indices.size());
		mesh.TextureId = studioMesh.TextureId;

		D3D11_BUFFER_DESC ibd{};
		ibd.Usage = D3D11_USAGE_IMMUTABLE;
		ibd.ByteWidth = static_cast<UINT>(sizeof(UINT) * studioMesh.Indices.size());
		ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		ibd.CPUAccessFlags = 0;
		ibd.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA bufferData{};
		bufferData.pSysMem = studioMesh.Indices.data();

		auto hr = device->CreateBuffer(&ibd, &bufferData, mesh.IndexBuffer.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return {};

		return mesh;
	}


	D3DModel LoadModel(ID3D11Device* device, const StudioModel::Model& studioModel)
	{
		D3DModel model{};

		D3D11_BUFFER_DESC vbd{};
		vbd.Usage = D3D11_USAGE_IMMUTABLE;
		vbd.ByteWidth = static_cast<UINT>(sizeof(StudioModel::Vertex) * studioModel.Vertices.size());
		vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbd.CPUAccessFlags = 0;
		vbd.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA bufferData{};
		bufferData.pSysMem = studioModel.Vertices.data();

		auto hr = device->CreateBuffer(&vbd, &bufferData, model.VertexBuffer.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return {};

		const auto& studioMeshes = studioModel.Meshes;

		if (!studioMeshes.empty())
		{
			model.Meshes.reserve(studioMeshes.size());

			for (const auto& studioMesh : studioMeshes)
			{
				auto mesh = LoadMesh(device, studioMesh);
				model.Meshes.push_back(std::move(mesh));
			}
		}

		return model;
	}


	D3DBodyPart LoadBodyPart(ID3D11Device* device, const StudioModel::BodyPart& studioBodyPart)
	{
		D3DBodyPart bodyPart{};

		const auto& studioModels = studioBodyPart.Models;

		if (!studioModels.empty())
		{
			bodyPart.Models.reserve(studioModels.size());

			for (const auto& studioModel : studioModels)
			{
				auto model = LoadModel(device, studioModel);
				bodyPart.Models.push_back(std::move(model));
			}
		}

		return bodyPart;
	}


	D3DTexture LoadTexture(ID3D11Device* device, const StudioModel::Texture& studioTexture)
	{
		D3DTexture texture{};

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = studioTexture.Width;
		desc.Height = studioTexture.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA textureData{};
		textureData.pSysMem = studioTexture.Data.data();
		textureData.SysMemPitch = static_cast<UINT>(studioTexture.Width * 4);
		textureData.SysMemSlicePitch = static_cast<UINT>(studioTexture.Width * studioTexture.Height * 4);

		HRESULT hr;

		hr = device->CreateTexture2D(&desc, &textureData, texture.Texture.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return {};

		hr = device->CreateShaderResourceView(texture.Texture.Get(), nullptr, texture.View.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return {};

		return texture;
	}


public:

	void Load(ID3D11Device* device, const std::wstring& filePath)
	{
		m_StudioModel = std::make_unique<StudioModel>();

		m_StudioModel->LoadFromFile(filePath);

		const auto& studioBodyParts = m_StudioModel->GetBodyParts();

		if (!studioBodyParts.empty())
		{
			m_BodyParts.reserve(studioBodyParts.size());

			for (const auto& studioBodyPart : studioBodyParts)
			{
				auto bodypart = LoadBodyPart(device, studioBodyPart);
				m_BodyParts.push_back(std::move(bodypart));
			}
		}

		const auto& studioTextures = m_StudioModel->GetTextures();

		if (!studioTextures.empty())
		{
			m_Textures.reserve(studioTextures.size());

			for (const auto& studioTexture : studioTextures)
			{
				auto texture = LoadTexture(device, studioTexture);
				m_Textures.push_back(std::move(texture));
			}
		}
	}


	StudioModel* GetStudioModel() const
	{
		return m_StudioModel.get();
	}


	const std::vector<D3DBodyPart>& GetBodyParts() const
	{
		return m_BodyParts;
	}


	const std::vector<D3DTexture>& GetTextures() const
	{
		return m_Textures;
	}


private:

	std::unique_ptr<StudioModel> m_StudioModel;
	std::vector<D3DBodyPart> m_BodyParts;
	std::vector<D3DTexture> m_Textures;
};


class D3DStudioModelRenderer
{
private:

	struct MatrixBuffer
	{
		XMMATRIX World;
		XMMATRIX View;
		XMMATRIX Projection;
	};


	struct BoneBuffer
	{
		XMMATRIX BoneTransforms[128];
	};


	enum class ModelCategory
	{
		Normal,
		Gun,
	};


	static std::vector<uint8_t> ReadAllBytes(const std::string& filePath)
	{
		std::vector<uint8_t> buffer;

		std::ifstream file(filePath, std::ios::binary);

		if (!file)
			throw std::runtime_error("Cannot open file: " + filePath);

		file.seekg(0, std::ios::end);
		size_t file_size = file.tellg();
		file.seekg(0, std::ios::beg);

		buffer.resize(file_size);
		file.read(reinterpret_cast<char*>(buffer.data()), file_size);

		if (!file)
			throw std::runtime_error("Failed to read file: " + filePath);

		return buffer;
	}


	HRESULT InitPipeline()
	{
		HRESULT hr;

		//
		// Load shaders
		//

		// auto vertexShaderBytecode = ReadAllBytes("VertexShader.cso");
		// auto pixelShaderBytecode = ReadAllBytes("PixelShader.cso");

		hr = m_D3DDevice->CreateVertexShader(vertexShaderBytecode, sizeof(vertexShaderBytecode), nullptr, m_VertexShader.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		hr = m_D3DDevice->CreatePixelShader(pixelShaderBytecode, sizeof(pixelShaderBytecode), nullptr, m_PixelShader.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		//
		// Create input layout
		//

		D3D11_INPUT_ELEMENT_DESC ied[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		hr = m_D3DDevice->CreateInputLayout(ied, ARRAYSIZE(ied), vertexShaderBytecode, sizeof(vertexShaderBytecode), m_InputLayout.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		return S_OK;
	}


	HRESULT InitGraphics()
	{
		HRESULT hr;

		//
		// Create constant buffers
		//

		D3D11_BUFFER_DESC bd{};
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(MatrixBuffer);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;

		hr = m_D3DDevice->CreateBuffer(&bd, nullptr, m_MatrixBuffer.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(BoneBuffer);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;

		hr = m_D3DDevice->CreateBuffer(&bd, nullptr, m_BoneBuffer.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		//
		// Create sampler state
		//

		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = D3D11_FLOAT32_MAX;

		hr = m_D3DDevice->CreateSamplerState(&sd, m_SamplerState.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		return S_OK;
	}


	ModelCategory GuessModelCategory()
	{
		if (!m_D3DStudioModel)
			return ModelCategory::Normal;

		if (!m_D3DStudioModel->GetStudioModel())
			return ModelCategory::Normal;

		const auto& filePath = m_D3DStudioModel->GetStudioModel()->GetFilePath();

		std::filesystem::path path(filePath);

		std::wstring fileName = path.stem().wstring();

		_wcslwr_s(fileName.data(), fileName.capacity());

		if (fileName.starts_with(L"v_"))
			return ModelCategory::Gun;

		return ModelCategory::Normal;
	}


	void SetCamera()
	{
		m_World = XMMatrixScaling(-1, 1, 1); // Make Right-Handed Coordinate System

		XMVECTOR Eye = XMVectorSet(-50.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR Up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // Z up

		float fov = 65;

		auto category = GuessModelCategory();

		switch (category)
		{
			case ModelCategory::Gun:
				Eye = XMVectorSet(-1.0f, 1.4f, 1.0f, 0.0f);
				At = XMVectorSet(-5.0f, 1.4f, 1.0f, 0.0f);
				fov = 90;
				break;
		}

		m_View = XMMatrixLookAtLH(Eye, At, Up);

		m_Projection = XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), m_ViewportWidth / (float)m_ViewportHeight, 0.01f, 1000.0f);
	}


	void DrawModel()
	{
		m_D3DDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const auto& textures = m_D3DStudioModel->GetTextures();

		if (textures.empty())
			return;

		for (const auto& bodyPart : m_D3DStudioModel->GetBodyParts())
		{
			for (const auto& model : bodyPart.Models)
			{
				if (!model.VertexBuffer)
					continue;

				ID3D11Buffer* vertexBuffers[] = { model.VertexBuffer.Get() };
				UINT strides[] = { sizeof(StudioModel::Vertex) };
				UINT offsets[] = { 0 };

				m_D3DDeviceContext->IASetVertexBuffers(0, ARRAYSIZE(vertexBuffers), vertexBuffers, strides, offsets);

				for (const auto& mesh : model.Meshes)
				{
					if (!mesh.IndexBuffer)
						continue;
					if (!mesh.NumIndices)
						continue;

					const auto& texture = textures[mesh.TextureId];

					if (!texture.Texture)
						continue;
					if (!texture.View)
						continue;

					ID3D11ShaderResourceView* shaderResourceViews[] = { texture.View.Get() };

					m_D3DDeviceContext->IASetIndexBuffer(mesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

					m_D3DDeviceContext->PSSetShaderResources(0, ARRAYSIZE(shaderResourceViews), shaderResourceViews);

					m_D3DDeviceContext->DrawIndexed(mesh.NumIndices, 0, 0);
				}
			}
		}
	}


public:

	void Draw()
	{
		// We need to validate all objects to ensure they work properly.

		if (!m_D3DDevice)
			return;
		if (!m_D3DDeviceContext)
			return;

		if (!m_D3DStudioModel)
			return;
		if (!m_D3DStudioModel->GetStudioModel())
			return;

		if (!m_InputLayout)
			return;
		if (!m_MatrixBuffer)
			return;
		if (!m_BoneBuffer)
			return;
		if (!m_VertexShader)
			return;
		if (!m_PixelShader)
			return;
		if (!m_SamplerState)
			return;

		//
		// Activate input layout
		//

		m_D3DDeviceContext->IASetInputLayout(m_InputLayout.Get());

		//
		// Update MVP matrix
		//

		SetCamera();

		MatrixBuffer matrixBuffer{};
		matrixBuffer.World = XMMatrixTranspose(m_World);
		matrixBuffer.View = XMMatrixTranspose(m_View);
		matrixBuffer.Projection = XMMatrixTranspose(m_Projection);

		m_D3DDeviceContext->UpdateSubresource(m_MatrixBuffer.Get(), 0, nullptr, &matrixBuffer, 0, 0);

		//
		// Update bone matrix
		//

		m_Animating.SetStudioHeader(m_D3DStudioModel->GetStudioModel()->GetStudioHeader());
		m_Animating.SetStudioSequenceGroupHeaders(m_D3DStudioModel->GetStudioModel()->GetSequenceGroupHeaders());
		m_Animating.SetSequence(0);
		m_Animating.SetFrame(0);
		m_Animating.SetUpBones();
		auto boneTransforms = m_Animating.GetBoneTransforms();

		// auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_LastUpdateTime).count();
		// auto sec = ms / 1000.0;
		// m_LastUpdateTime = std::chrono::steady_clock::now();
		// m_Animating.AdvanceFrame(sec);

		BoneBuffer boneBuffer{};

		for (int i = 0; i < 128; i++)
		{
			XMMATRIX matrix
			{
				boneTransforms[i][0][0],
				boneTransforms[i][1][0],
				boneTransforms[i][2][0],
				0.0f,
				boneTransforms[i][0][1],
				boneTransforms[i][1][1],
				boneTransforms[i][2][1],
				0.0f,
				boneTransforms[i][0][2],
				boneTransforms[i][1][2],
				boneTransforms[i][2][2],
				0.0f,
				boneTransforms[i][0][3],
				boneTransforms[i][1][3],
				boneTransforms[i][2][3],
				1.0f,
			};

			boneBuffer.BoneTransforms[i] = XMMatrixTranspose(matrix);
		}

		m_D3DDeviceContext->UpdateSubresource(m_BoneBuffer.Get(), 0, nullptr, &boneBuffer, 0, 0);

		//
		// Set pixel shader
		//

		m_D3DDeviceContext->VSSetShader(m_VertexShader.Get(), 0, 0);

		ID3D11Buffer* constantBuffers[] =
		{
			m_MatrixBuffer.Get(),
			m_BoneBuffer.Get(),
		};

		m_D3DDeviceContext->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);

		//
		// Set pixel shader
		//

		m_D3DDeviceContext->PSSetShader(m_PixelShader.Get(), 0, 0);

		ID3D11SamplerState* samplerStates[] =
		{
			m_SamplerState.Get()
		};

		m_D3DDeviceContext->PSSetSamplers(0, ARRAYSIZE(samplerStates), samplerStates);

		//
		// Draw
		//

		DrawModel();
	}


	void SetModel(D3DStudioModel* d3dStudioModel)
	{
		m_D3DStudioModel = d3dStudioModel;
	}


	void SetViewport(UINT viewWidth, UINT viewHeight)
	{
		m_ViewportWidth = viewWidth;
		m_ViewportHeight = viewHeight;
	}


	HRESULT Init(ID3D11Device* device, ID3D11DeviceContext* deviceContext)
	{
		HRESULT hr;

		m_D3DDevice = device;
		m_D3DDeviceContext = deviceContext;

		hr = InitPipeline();

		if (FAILED(hr))
			return hr;

		hr = InitGraphics();

		if (FAILED(hr))
			return hr;

		return S_OK;
	}


	D3DStudioModelRenderer()
		: m_World{}
		, m_View{}
		, m_Projection{}
		, m_ViewportWidth{}
		, m_ViewportHeight{}
		, m_D3DStudioModel{}
		, m_Animating{}
	{
	}


private:

	ComPtr<ID3D11Device> m_D3DDevice;
	ComPtr<ID3D11DeviceContext> m_D3DDeviceContext;
	ComPtr<ID3D11VertexShader> m_VertexShader;
	ComPtr<ID3D11PixelShader> m_PixelShader;
	ComPtr<ID3D11InputLayout> m_InputLayout;
	ComPtr<ID3D11Buffer> m_MatrixBuffer;
	ComPtr<ID3D11Buffer> m_BoneBuffer;
	ComPtr<ID3D11SamplerState> m_SamplerState;

	XMMATRIX m_World;
	XMMATRIX m_View;
	XMMATRIX m_Projection;

	UINT m_ViewportWidth;
	UINT m_ViewportHeight;

	D3DStudioModel* m_D3DStudioModel;

	StudioModelAnimating m_Animating;
	std::chrono::steady_clock::time_point m_LastUpdateTime;
};
