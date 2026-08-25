# Built-in allocator strategies

| Strategy | Per-object free | Realloc | Alignment | Notes |
| --- | --- | --- | --- | --- |
| `system` | yes | yes | natural only (up to `max_align_t`) | Direct system allocation; simplest baseline |
| `aligned` | yes | yes | up to configured max (power-of-two) | Explicit alignment |
| `fixed_pool` | yes | no | object alignment | Fixed-size object pool; reuse tracking |
| `size_class_pool` | yes | no | up to 256 | Multiple bounded size classes; per-class waste/hits |
| `slab` | yes | no | 16 | Page-backed fixed category; releases empty slabs on trim |
| `arena` | no | no | up to 4096 | Bump allocator; reset semantics; no per-object free |
| `free_list` | yes | no | natural | Variable-size blocks; boundary-tag coalescing |
| `buddy` | yes | no | 16 | Power-of-two split/coalesce over an aligned arena |
| `segregated_fit` | yes | no | natural | Bounded segregated free lists; no coalescing (fragmentation visible); experimental |
| `pinned` | yes | no | up to 4096 | `cudaMallocHost`; constrained budget |
| `cuda_direct` | yes | no | up to 256 | `cudaMalloc`/`cudaFree` |
| `cuda_pool` | yes | no | up to 256 | `cudaMallocAsync`/`cudaFreeAsync`; completed work must be synchronized |
| `shared` | no | no | up to 4096 | Windows file mapping; arena-like allocation; interprocess |
| `mapped` | no | no | up to 4096 | File-backed mapping; records setup latency |

Semantic caveat: the arena allocator cannot free individual objects, so comparing it to general-purpose allocators must account for that semantic difference. The segregated-fit allocator is an experimental control that deliberately skips coalescing to expose fragmentation.
