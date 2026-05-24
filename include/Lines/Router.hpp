#ifndef LINES_ROUTER_HPP
#define LINES_ROUTER_HPP

#include <Rho/Railway.hpp>
#include <Collection/Array.hpp>
#include <Resource/Path.hpp>

using namespace Xi;

namespace Lines {

using namespace Rho;

// ---------------------------------------------------------------------------
// TreeRoutingEntry — A node in the routing tree.
// Each node represents a single port number (addressPart) in a NumericalAddress.
// The tree mirrors the hierarchical address space.
// ---------------------------------------------------------------------------
struct TreeRoutingEntry {
  Station* station = nullptr;
  u64 addressPart = 0;

  Array<TreeRoutingEntry> children;
  TreeRoutingEntry* parent = nullptr;

  i32 weight = -10; // Default weight. Higher = preferred. Accumulated along path.

  TreeRoutingEntry() {}
  TreeRoutingEntry(u64 part, Station* st, TreeRoutingEntry* p, i32 w = -10)
    : station(st), addressPart(part), parent(p), weight(w) {}
};

// ---------------------------------------------------------------------------
// RoutingEntry — Flat representation (address + station).
// ---------------------------------------------------------------------------
struct RoutingEntry {
  Resource::NumericalAddress address;
  Station* station = nullptr;
};

// ---------------------------------------------------------------------------
// Router
//
// Tree-based routing engine. NOT a Station.
// Routes carts to the best matching entry based on target address proximity.
//
// Routing algorithm:
//   Given a cart with source S and target T:
//   - Walk the tree to find entries on the path from S up to the common
//     ancestor, then down toward T.
//   - The most specific match toward T wins (deepest match + highest weight sum).
//   - Entries outside this scope are ignored.
//   - No retries — once routed, the cart is gone.
// ---------------------------------------------------------------------------
class Router {
public:
  Array<TreeRoutingEntry> entries; // Top-level roots (first port of addresses)

  // -----------------------------------------------------------------------
  // hook — Register a station at an exact address. No duplicates allowed.
  // Returns true on success, false if address already exists.
  // -----------------------------------------------------------------------
  bool hook(Station* station, const Resource::NumericalAddress& address) {
    if (!station || address.size() == 0) return false;
    
    // Check for duplicate
    TreeRoutingEntry* existing = _findExact(address);
    if (existing && existing->station) return false;
    
    // Insert into tree, creating intermediate nodes as needed
    _insertPath(address, station);
    return true;
  }

  // -----------------------------------------------------------------------
  // generate — Find the next unused address under `parent` (parent.length + 1).
  // -----------------------------------------------------------------------
  Resource::NumericalAddress generate(const Resource::NumericalAddress& parent) {
    TreeRoutingEntry* node = nullptr;
    if (parent.size() > 0) {
      node = _findExact(parent);
    }

    u64 candidate = 1;
    if (node) {
      // Scan children for used parts
      while (true) {
        bool found = false;
        for (usz i = 0; i < node->children.size(); ++i) {
          if (node->children[i].addressPart == candidate) {
            candidate++;
            found = true;
            break;
          }
        }
        if (!found) break;
      }
    } else if (parent.size() == 0) {
      // Top-level
      while (true) {
        bool found = false;
        for (usz i = 0; i < entries.size(); ++i) {
          if (entries[i].addressPart == candidate) {
            candidate++;
            found = true;
            break;
          }
        }
        if (!found) break;
      }
    }

    Resource::NumericalAddress result;
    for (usz i = 0; i < parent.size(); ++i) result.push(parent[i]);
    result.push(candidate);
    return result;
  }

  // -----------------------------------------------------------------------
  // hookUnder — Find an unused address of length parent.size()+1 and hook there.
  // -----------------------------------------------------------------------
  void hookUnder(Station* station, const Resource::NumericalAddress& parent) {
    Resource::NumericalAddress addr = generate(parent);
    hook(station, addr);
  }

  // -----------------------------------------------------------------------
  // unhook — Remove a station or address from the tree.
  // -----------------------------------------------------------------------
  void unhook(Station* station) {
    if (!station) return;
    _unhookStation(entries, station);
  }

  void unhook(const Resource::NumericalAddress& address) {
    if (address.size() == 0) return;
    TreeRoutingEntry* node = _findExact(address);
    if (node) {
      node->station = nullptr;
      _pruneEmpty(node);
    }
  }

  // -----------------------------------------------------------------------
  // unhookAll — Remove address and all children, or clear everything.
  // -----------------------------------------------------------------------
  void unhookAll(const Resource::NumericalAddress& address) {
    if (address.size() == 0) { unhookAll(); return; }
    
    if (address.size() == 1) {
      for (usz i = 0; i < entries.size(); ++i) {
        if (entries[i].addressPart == (u64)address[0]) {
          entries.splice(i, 1);
          return;
        }
      }
      return;
    }
    
    // Find parent, then remove the child subtree
    Resource::NumericalAddress parentAddr;
    for (usz i = 0; i < address.size() - 1; ++i) parentAddr.push(address[i]);
    TreeRoutingEntry* parent = _findExact(parentAddr);
    if (parent) {
      u64 lastPart = (u64)address[address.size() - 1];
      for (usz i = 0; i < parent->children.size(); ++i) {
        if (parent->children[i].addressPart == lastPart) {
          parent->children.splice(i, 1);
          return;
        }
      }
    }
  }

  void unhookAll() {
    entries.clear();
  }

  // -----------------------------------------------------------------------
  // route — Send cart to the best matching entry.
  //
  // Algorithm:
  //   1. Walk the tree matching the target address as deep as possible.
  //   2. If exact or prefix match found, use it.
  //   3. Otherwise, backtrack from the source's position in the tree up
  //      to the root, and pick the best branch toward the target.
  //   4. Weight is accumulated (summed) along the path.
  //   5. Best = deepest match toward target, with highest accumulated weight.
  // -----------------------------------------------------------------------
  void route(Cart& cart) {
    if (!cart.isAddressed || cart.target.size() == 0) return;

    TreeRoutingEntry* best = nullptr;
    i32 bestScore = 0;
    usz bestDepth = 0;

    // Forward match: try to match target directly in the tree
    _findBestMatch(entries, cart.target, 0, 0, best, bestScore, bestDepth);

    // Source-aware: route UP to parent if no forward match
    if (!best && cart.source.size() > 0) {
      _findBestViaSource(cart, best, bestScore, bestDepth);
    }

    if (best && best->station) {
      best->station->receive(cart);
    }
  }

  // -----------------------------------------------------------------------
  // address — Get the address registered for a station.
  // -----------------------------------------------------------------------
  Resource::NumericalAddress address(Station* station) {
    Resource::NumericalAddress result;
    _findAddress(entries, station, result);
    return result;
  }

  // -----------------------------------------------------------------------
  // list — List immediate children under an address.
  // -----------------------------------------------------------------------
  Array<TreeRoutingEntry*> list(const Resource::NumericalAddress& addr) {
    Array<TreeRoutingEntry*> result;
    if (addr.size() == 0) {
      for (usz i = 0; i < entries.size(); ++i) {
        result.push(&entries[i]);
      }
      return result;
    }
    TreeRoutingEntry* node = _findExact(addr);
    if (node) {
      for (usz i = 0; i < node->children.size(); ++i) {
        result.push(&node->children[i]);
      }
    }
    return result;
  }

  void destroy() {
    entries.clear();
  }

private:
  // -----------------------------------------------------------------------
  // Tree manipulation helpers
  // -----------------------------------------------------------------------

  TreeRoutingEntry* _findExact(const Resource::NumericalAddress& addr) {
    if (addr.size() == 0) return nullptr;
    
    Array<TreeRoutingEntry>* level = &entries;
    TreeRoutingEntry* current = nullptr;

    for (usz depth = 0; depth < addr.size(); ++depth) {
      u64 part = (u64)addr[depth];
      bool found = false;
      for (usz i = 0; i < level->size(); ++i) {
        if ((*level)[i].addressPart == part) {
          current = &(*level)[i];
          level = &current->children;
          found = true;
          break;
        }
      }
      if (!found) return nullptr;
    }
    return current;
  }

  void _insertPath(const Resource::NumericalAddress& addr, Station* station) {
    Array<TreeRoutingEntry>* level = &entries;
    TreeRoutingEntry* current = nullptr;

    for (usz depth = 0; depth < addr.size(); ++depth) {
      u64 part = (u64)addr[depth];
      bool found = false;
      for (usz i = 0; i < level->size(); ++i) {
        if ((*level)[i].addressPart == part) {
          current = &(*level)[i];
          level = &current->children;
          found = true;
          break;
        }
      }
      if (!found) {
        TreeRoutingEntry newNode;
        newNode.addressPart = part;
        newNode.parent = current;
        if (depth == addr.size() - 1) {
          newNode.station = station;
        }
        level->push(Move(newNode));
        current = &(*level)[level->size() - 1];
        level = &current->children;
      }
    }
    // Ensure the final node has the station set
    if (current) current->station = station;
  }

  void _unhookStation(Array<TreeRoutingEntry>& level, Station* station) {
    for (usz i = 0; i < level.size(); ++i) {
      if (level[i].station == station) {
        level[i].station = nullptr;
        if (level[i].children.size() == 0) {
          level.splice(i, 1);
          --i;
          continue;
        }
      }
      _unhookStation(level[i].children, station);
      // Prune if node is now empty (no station, no children)
      if (!level[i].station && level[i].children.size() == 0) {
        level.splice(i, 1);
        --i;
      }
    }
  }

  void _pruneEmpty(TreeRoutingEntry* node) {
    while (node && !node->station && node->children.size() == 0) {
      TreeRoutingEntry* parent = node->parent;
      if (parent) {
        for (usz i = 0; i < parent->children.size(); ++i) {
          if (&parent->children[i] == node) {
            parent->children.splice(i, 1);
            break;
          }
        }
      } else {
        // Top-level
        for (usz i = 0; i < entries.size(); ++i) {
          if (&entries[i] == node) {
            entries.splice(i, 1);
            break;
          }
        }
      }
      node = parent;
    }
  }

  // -----------------------------------------------------------------------
  // Routing helpers — fast tree traversal
  // -----------------------------------------------------------------------

  /// Forward match: walk the tree following target address parts.
  /// Accumulate weight, track deepest match with a station.
  void _findBestMatch(Array<TreeRoutingEntry>& level,
                      const Resource::NumericalAddress& target,
                      usz depth, i32 accumWeight,
                      TreeRoutingEntry*& best, i32& bestScore, usz& bestDepth) {
    if (depth >= target.size()) return;

    u64 part = (u64)target[depth];
    for (usz i = 0; i < level.size(); ++i) {
      if (level[i].addressPart == part) {
        i32 w = accumWeight + level[i].weight;
        if (level[i].station) {
          if (depth + 1 > bestDepth || (depth + 1 == bestDepth && w > bestScore)) {
            best = &level[i];
            bestScore = w;
            bestDepth = depth + 1;
          }
        }
        // Recurse deeper
        _findBestMatch(level[i].children, target, depth + 1, w, best, bestScore, bestDepth);
        return; // Only one child can match at each level
      }
    }
    // No match at this depth — that's fine, we already tracked best
  }

  /// Source-aware routing: walk up from source path toward parent.
  ///
  /// Priority (from router.md):
  ///   1.2.3 < 1.2 < 1 < 5 < 5.8.9 < 5.8.9.9
  ///   (< means less preferred)
  ///
  /// Forward matches (5, 5.8.9, 5.8.9.9) are always better — they're
  /// already checked in _findBestMatch before this is called.
  ///
  /// Among source-path ancestors (1, 1.2, 1.2.3), SHALLOWEST is most
  /// preferred: closer to the root = closer to the other side of the
  /// network. In the strict tree, the shallowest ancestor with a
  /// station is always the immediate parent gateway.
  void _findBestViaSource(Cart& cart, TreeRoutingEntry*& best,
                          i32& bestScore, usz& bestDepth) {
    // Build the source path: walk the tree following the source address
    Array<TreeRoutingEntry*> sourcePath;
    {
      Array<TreeRoutingEntry>* level = &entries;
      for (usz d = 0; d < cart.source.size(); ++d) {
        u64 part = (u64)cart.source[d];
        bool found = false;
        for (usz i = 0; i < level->size(); ++i) {
          if ((*level)[i].addressPart == part) {
            sourcePath.push(&(*level)[i]);
            level = &(*level)[i].children;
            found = true;
            break;
          }
        }
        if (!found) break;
      }
    }

    // -------------------------------------------------------------------
    // Step 1: Try sibling branches that lead toward the target.
    // At each ancestor level, check if any sibling matches a target part.
    // This handles cases like cross-routes at the same level.
    // -------------------------------------------------------------------
    for (long long si = (long long)sourcePath.size() - 1; si >= 0; --si) {
      TreeRoutingEntry* ancestor = sourcePath[(usz)si];
      Array<TreeRoutingEntry>* siblings;
      if (ancestor->parent) {
        siblings = &ancestor->parent->children;
      } else {
        siblings = &entries;
      }
      
      i32 baseWeight = 0;
      for (usz k = 0; k <= (usz)si; ++k) {
        baseWeight += sourcePath[k]->weight;
      }

      for (usz j = 0; j < siblings->size(); ++j) {
        if (&(*siblings)[j] == ancestor) continue; // skip our own branch
        
        if (cart.target.size() > 0 && (*siblings)[j].addressPart == (u64)cart.target[(usz)si > 0 ? (usz)si : 0]) {
          i32 w = baseWeight + (*siblings)[j].weight;
          if ((*siblings)[j].station) {
            usz matchDepth = (usz)si + 1;
            if (matchDepth > bestDepth || (matchDepth == bestDepth && w > bestScore)) {
              best = &(*siblings)[j];
              bestScore = w;
              bestDepth = matchDepth;
            }
          }
          usz nextTargetIdx = (usz)si + 1;
          if (nextTargetIdx < cart.target.size()) {
            _findBestMatch((*siblings)[j].children, cart.target, nextTargetIdx, w,
                          best, bestScore, bestDepth);
          }
        }
      }
    }

    // -------------------------------------------------------------------
    // Step 2: Ancestor fallback — route UP to parent.
    //
    // If no forward or sibling match was found, the target is outside
    // our subtree entirely. Route to the shallowest ancestor on the
    // source path that has a station — that's the parent gateway.
    //
    // Shallowest first because: 1 > 1.2 > 1.2.3 in priority
    // (closer to root = closer to the other side of the network).
    //
    // In the strict tree, each gateway only hooks its immediate parent
    // and its immediate children. The shallowest ancestor with a station
    // IS the parent. Children are deeper and would match via forward.
    // -------------------------------------------------------------------
    if (!best) {
      for (usz i = 0; i < sourcePath.size(); ++i) {
        if (sourcePath[i]->station) {
          best = sourcePath[i];
          bestScore = sourcePath[i]->weight;
          bestDepth = 0; // Ancestor match — lowest priority class
          break; // Shallowest first
        }
      }
    }
  }

  bool _findAddress(Array<TreeRoutingEntry>& level, Station* station,
                    Resource::NumericalAddress& result) {
    for (usz i = 0; i < level.size(); ++i) {
      result.push(level[i].addressPart);
      if (level[i].station == station) return true;
      if (_findAddress(level[i].children, station, result)) return true;
      result.pop();
    }
    return false;
  }
};

} // namespace Lines

#endif // LINES_ROUTER_HPP
