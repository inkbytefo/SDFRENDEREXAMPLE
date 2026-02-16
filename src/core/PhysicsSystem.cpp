#include "PhysicsSystem.hpp"
#include <Jolt/Core/Memory.h>
#include <iostream>

namespace engine::core {

PhysicsSystem::PhysicsSystem() {
    std::cout << "Physics: Registering Default Allocator..." << std::endl;
    JPH::RegisterDefaultAllocator();
    std::cout << "Physics: Creating Factory..." << std::endl;
    JPH::Factory::sInstance = new JPH::Factory();
    std::cout << "Physics: Registering Types..." << std::endl;
    JPH::RegisterTypes();

    std::cout << "Physics: Creating Filters and Interfaces..." << std::endl;
    bp_layer_interface = std::make_unique<BPLayerInterfaceImpl>();
    object_layer_pair_filter = std::make_unique<ObjectLayerPairFilterImpl>();
    object_vs_broadphase_layer_filter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();

    std::cout << "Physics: Creating Allocator/JobSystem..." << std::endl;
    temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    
    int num_threads = std::max(1, (int)JPH::thread::hardware_concurrency() - 1);
    job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, num_threads);

    std::cout << "Physics: Initializing PhysicsSystem..." << std::endl;
    physics_system = std::make_unique<JPH::PhysicsSystem>();
    
    const uint32_t cMaxBodies = 1024;
    const uint32_t cMaxBodyPairs = 1024;
    const uint32_t cMaxContactConstraints = 1024;

    physics_system->Init(cMaxBodies, 0, cMaxBodyPairs, cMaxContactConstraints, *bp_layer_interface, *object_vs_broadphase_layer_filter, *object_layer_pair_filter);
}

PhysicsSystem::~PhysicsSystem() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void PhysicsSystem::update(float deltaTime) {
    physics_system->Update(deltaTime, 1, temp_allocator.get(), job_system.get());
}

} // namespace engine::core
