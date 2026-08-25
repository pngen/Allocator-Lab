// Example: register a caller-provided custom allocator strategy.
#include "allocator_lab/allocator_registry.hpp"
#include "allocator_lab/allocators/system_allocator.hpp"
#include <iostream>
using namespace allocator_lab;
// A custom strategy that wraps the system allocator but labels itself.
class MyAllocator : public AllocatorStrategy {
public:
    AllocatorId id() const noexcept override { return id_; }
    void set_id(AllocatorId i) noexcept override { id_ = i; }
    const std::string& name() const noexcept override { return name_; }
    AllocatorKind kind() const noexcept override { return AllocatorKind::Custom; }
    const AllocatorCapabilities& capabilities() const noexcept override { return caps_; }
    AllocationResult allocate(const AllocationRequest& req) override { return inner_.allocate(req); }
    Error free(AllocationHandle h) override { return inner_.free(h); }
    AllocationResult reallocate(AllocationHandle h, const AllocationRequest& r) override { return inner_.reallocate(h, r); }
    Error query(AllocationHandle h, AllocationInfo& o) override { return inner_.query(h, o); }
    AllocatorStatistics statistics() const override { return inner_.statistics(); }
    void shutdown() noexcept override { inner_.shutdown(); }
    MyAllocator() { caps_ = inner_.capabilities(); caps_.notes = "custom caller-provided strategy"; }
private:
    SystemAllocator inner_;
    AllocatorId id_ = 0;
    std::string name_ = "my_custom";
    AllocatorCapabilities caps_;
};
int main() {
    AllocatorRegistry reg;
    Error e = reg.register_allocator_at(std::make_unique<MyAllocator>(), 999);
    if (!e.ok()) { std::cerr << "register failed: " << e.message << "\n"; return 1; }
    AllocatorStrategy* a = reg.find(999);
    if (!a) { std::cerr << "not found\n"; return 2; }
    AllocationRequest req; req.size = 64;
    AllocationResult r = a->allocate(req);
    std::cout << "custom allocator allocate: " << (r.error.ok() ? "ok" : r.error.code_name()) << "\n";
    if (r.error.ok()) a->free(r.id);
    return 0;
}
