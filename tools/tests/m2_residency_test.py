#!/usr/bin/env python3
"""Run production M2 collision methods against the previous cell-list algorithm."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
header = (root/'include/rendering/m2_renderer.hpp').read_text()
source = (root/'src/rendering/m2_renderer.cpp').read_text()
collision = header[header.index('    struct CollisionMesh {'):header.index('    CollisionMesh collision;')]
methods = source[source.index('void M2ModelGPU::CollisionMesh::build()'):source.index('bool M2Renderer::hasModel(',source.index('void M2ModelGPU::CollisionMesh::build()'))]
terrain = (root/'include/rendering/terrain_manager.hpp').read_text()
retire = source[source.index('void M2Renderer::retireFailedUploadModel()'):source.index('bool M2Renderer::loadModel(')]
rollback = source[source.index('    } catch (const std::bad_alloc&)',source.index('bool M2Renderer::loadModel(')):source.index('\n} // namespace rendering',source.index('bool M2Renderer::loadModel('))]
record = terrain[terrain.index('    struct WMODoodadReady {'):terrain.index('    platform::CpuGeometryVector<WMODoodadReady>')]
prefix = r"""
#include "pipeline/m2_loader.hpp"
#include "rendering/triangle_cell_index.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>
using namespace wowee;
using namespace wowee::rendering;
struct M2ModelGPU {
"""
ownership = r"""
struct FakeContext {
    unsigned ends=0,waits=0;bool completed=true;
    void endUploadBatch() { ++ends; }
    bool waitAllUploads() { ++waits;return completed; }
};
struct M2Renderer {
    FakeContext* vkCtx_;
    std::optional<M2ModelGPU> failedUploadModel_;
    std::optional<M2ModelGPU> live;
    unsigned destroys=0;
    void destroyModelGPU(M2ModelGPU& model) { assert(model.gpuHandle==7);model.gpuHandle=0;++destroys; }
    void retireFailedUploadModel();
    void forceFailure(bool afterPublication) {
        M2ModelGPU gpuModel;gpuModel.gpuHandle=7;
        bool published=false,ownBatchOpen=true;
        try {
            if(afterPublication) { live.emplace(std::move(gpuModel));published=true; }
            throw std::bad_alloc();
"""
cases = r"""
int main() {
    FakeContext context;M2Renderer owner{&context};
    try {owner.forceFailure(false);assert(false);}catch(const std::bad_alloc&){}
    assert(owner.failedUploadModel_&&owner.failedUploadModel_->gpuHandle==7&&context.ends==1&&owner.destroys==0);
    context.completed=false;
    try {owner.retireFailedUploadModel();assert(false);}catch(const std::runtime_error&){}
    assert(owner.failedUploadModel_&&owner.destroys==0);
    context.completed=true;owner.retireFailedUploadModel();owner.retireFailedUploadModel();
    assert(!owner.failedUploadModel_&&owner.destroys==1&&context.waits==2);
    try {owner.forceFailure(true);assert(false);}catch(const std::bad_alloc&){}
    assert(!owner.failedUploadModel_&&owner.live&&owner.live->gpuHandle==7&&owner.destroys==1);
    printf("PASS production M2 rollback/retirement: prepublication ownership, fence failure retains, successful completion destroys once, postpublication failure keeps live owner\n");
    M2ModelGPU::CollisionMesh mesh;
    std::mt19937 random(254);
    for (unsigned i=0;i<1800;++i) {
        float x=random()%100,y=random()%100,z=random()%20,span=1+random()%20;
        for(auto v : {glm::vec3(x,y,z),glm::vec3(x+span,y,z), i%3 ? glm::vec3(x,y+span,z) : glm::vec3(x,y,z+span)}) {
            mesh.indices.push_back(mesh.vertices.size());mesh.vertices.push_back(v);
        }
    }
    mesh.build();
    mesh.build(); // Rebuilding retains identical cell ordering and query semantics.
    size_t count=mesh.gridCellsX*mesh.gridCellsY,oldBytes=0;
    std::vector<std::vector<uint32_t>> reference[2];
    for(auto& cells:reference) { cells.resize(count);oldBytes+=cells.capacity()*sizeof(cells[0]); }
    for(uint32_t i=0;i<mesh.triCount;++i) {
        const auto a=mesh.vertices[mesh.indices[i*3]],b=mesh.vertices[mesh.indices[i*3+1]],c=mesh.vertices[mesh.indices[i*3+2]];
        auto normal=glm::cross(b-a,c-a);float length=glm::length(normal),nz=length>0.001f?std::abs(normal.z/length):0;
        int x0=std::clamp(int((std::min({a.x,b.x,c.x})-mesh.gridOrigin.x)/4),0,mesh.gridCellsX-1);
        int x1=std::clamp(int((std::max({a.x,b.x,c.x})-mesh.gridOrigin.x)/4),0,mesh.gridCellsX-1);
        int y0=std::clamp(int((std::min({a.y,b.y,c.y})-mesh.gridOrigin.y)/4),0,mesh.gridCellsY-1);
        int y1=std::clamp(int((std::max({a.y,b.y,c.y})-mesh.gridOrigin.y)/4),0,mesh.gridCellsY-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            if(nz>=0.35f)reference[0][y*mesh.gridCellsX+x].push_back(i);
            if(nz<0.65f)reference[1][y*mesh.gridCellsX+x].push_back(i);
        }
    }
    for(unsigned type=0;type<2;++type) {
        const auto& cells=type?mesh.cellWallTris:mesh.cellFloorTris;
        for(size_t cell=0;cell<count;++cell) {
            auto got=cells[cell];assert(std::equal(got.begin(),got.end(),reference[type][cell].begin(),reference[type][cell].end()));
            oldBytes+=reference[type][cell].capacity()*sizeof(uint32_t);
        }
        for(unsigned q=0;q<1000;++q) {
            float x=int(random()%150)-25,y=int(random()%150)-25,w=random()%30;
            std::vector<uint32_t> got,expected;
            mesh.gatherTrisInRange(cells,x,y,x+w,y+w,got);
            int x0=std::clamp(int((x-mesh.gridOrigin.x)/4),0,mesh.gridCellsX-1),x1=std::clamp(int((x+w-mesh.gridOrigin.x)/4),0,mesh.gridCellsX-1);
            int y0=std::clamp(int((y-mesh.gridOrigin.y)/4),0,mesh.gridCellsY-1),y1=std::clamp(int((y+w-mesh.gridOrigin.y)/4),0,mesh.gridCellsY-1);
            for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
                const auto& cell=reference[type][cy*mesh.gridCellsX+cx];expected.insert(expected.end(),cell.begin(),cell.end());
            }
            std::sort(expected.begin(),expected.end());expected.erase(std::unique(expected.begin(),expected.end()),expected.end());assert(got==expected);
        }
    }
    const size_t compactBytes=mesh.cellFloorTris.storageBytes()+mesh.cellWallTris.storageBytes();
    assert(compactBytes<oldBytes);
    struct OldChild { uint32_t modelId,parent;pipeline::M2Model model;glm::vec3 pos;glm::mat4 matrix; };
    static_assert(sizeof(WMODoodadReady)<sizeof(OldChild)/3);
    platform::CpuGeometryVector<WMODoodadReady> children(6000);
    children[0].model=std::make_unique<pipeline::M2Model>();children[0].model->vertices.resize(2000);
    for(unsigned i=1;i<6000;++i)assert(!children[i].model);
    auto moved=std::move(children);assert(moved[0].model->vertices.size()==2000);
    moved.erase(moved.begin()+1,moved.begin()+5000);assert(moved.size()==1001&&moved[0].model->vertices.size()==2000);
    printf("PASS production M2 collision: %zu cell lists, 2000 query comparisons, compact=%zu old=%zu bytes\n",count*2,compactBytes,oldBytes);
    printf("PASS production child record: old=%zu new=%zu bytes; 6000 placements save %zu bytes before allocator; unique model survives move/erase\n",sizeof(OldChild),sizeof(WMODoodadReady),(sizeof(OldChild)-sizeof(WMODoodadReady))*6000);
}
"""
with tempfile.TemporaryDirectory(prefix='wowps-m2-residency-') as temp:
    src=Path(temp)/'test.cpp';src.write_text(prefix+collision+' int gpuHandle=0; };\n'+record+methods+ownership+rollback+'};\n'+retire+cases)
    executable=Path(temp)/'test'
    command=shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(src),'-o',str(executable)]
    subprocess.run(command,check=True);subprocess.run([str(executable)],check=True)
