#include <madrona/state.hpp>
#include <madrona/utils.hpp>
#include <madrona/dyn_array.hpp>

#include <cassert>
#include <functional>
#include <mutex>
#include <string_view>

namespace madrona {

StateManager::StateManager()
    : component_infos_(0),
      archetype_components_(0),
      archetype_stores_(0),
      query_data_(0)
{}

struct StateManager::ArchetypeStore::Init {
    uint32_t componentOffset;
    uint32_t numComponents;
    uint32_t id;
    HeapArray<TypeInfo> types;
    HeapArray<IntegerMapPair> lookupInputs;
};

StateManager::ArchetypeStore::ArchetypeStore(Init &&init)
    : componentOffset(init.componentOffset),
      numComponents(init.numComponents),
      tbl(init.types.data(), init.types.size(), init.id),
      columnLookup(init.lookupInputs.data(), init.lookupInputs.size())
{
}

void StateManager::registerComponent(uint32_t id,
                                     uint32_t alignment,
                                     uint32_t num_bytes)
{
    // IDs are globally assigned, technically there is an edge case where
    // there are gaps in the IDs assigned to a specific StateManager
    // for component_infos_ just use default initialization of the
    // unregistered components
    if (id >= component_infos_.size()) {
        component_infos_.resize(id + 1, [](auto) {});
    }

    component_infos_[id] = TypeInfo {
        .alignment = alignment,
        .numBytes = num_bytes,
    };
}

void StateManager::registerArchetype(uint32_t id, Span<ComponentID> components)
{
    uint32_t offset = archetype_components_.size();

    // FIXME Candidates for tmp allocator
    HeapArray<TypeInfo> type_infos(components.size());
    HeapArray<IntegerMapPair> lookup_input(components.size());

    for (int i = 0; i < (int)components.size(); i++) {
        ComponentID component_id = components[i];

        archetype_components_.push_back(component_id);
        type_infos[i] = component_infos_[component_id.id];

        lookup_input[i] = IntegerMapPair {
            .key = component_id.id,
            .value = (uint32_t)i,
        };
    }

    // IDs are globally assigned, technically there is an edge case where
    // there are gaps in the IDs assigned to a specific StateManager
    if (id >= archetype_stores_.size()) {
        archetype_stores_.resize(id + 1, [](auto ptr) {
            Optional<ArchetypeStore>::noneAt(ptr);
        });
    }

    archetype_stores_[id].emplace(ArchetypeStore::Init {
        offset,
        components.size(),
        id,
        std::move(type_infos),
        std::move(lookup_input),
    });
}

uint32_t StateManager::makeQuery(const ComponentID *components,
                                 uint32_t num_components,
                                 uint32_t *offset)
{
    DynArray<uint32_t, TmpAlloc> query_indices(0);

    uint32_t matching_archetypes = 0;
    for (int archetype_idx = 0, num_archetypes = archetype_stores_.size();
         archetype_idx < num_archetypes; archetype_idx++) {
        auto &archetype = *archetype_stores_[archetype_idx];

        bool has_components = true;
        for (int component_idx = 0; component_idx < (int)num_components; 
             component_idx++) {
            ComponentID component = components[component_idx];
            if (!archetype.columnLookup.exists(component.id)) {
                has_components = false;
                break;
            }
        }

        if (!has_components) {
            continue;
        }

        matching_archetypes += 1;

        query_indices.push_back(uint32_t(archetype_idx));

        for (int component_idx = 0; component_idx < (int)num_components;
             component_idx++) {
            ComponentID component = components[component_idx];
            query_indices.push_back(archetype.columnLookup[component.id]);
        }
    }

    *offset = query_data_.size();
    query_data_.resize(*offset + query_indices.size(), [](auto) {});
    memcpy(&query_data_[*offset], query_indices.data(),
           sizeof(uint32_t) * query_indices.size());

    return matching_archetypes;
}

uint32_t StateManager::next_component_id_ = 0;
uint32_t StateManager::next_archetype_id_ = 0;

}
